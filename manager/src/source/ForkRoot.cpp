#include "ForkRoot.h"

#include "Protocol.h"

using namespace std;

namespace
{
constexpr DWORD STOP_TIMEOUT_MS = 3000;

UniqueHandle CreateKillOnCloseJob()
{
    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    {
        return {};
    }
    return job;
}
}

ForkRoot::ForkRoot() : m_job(CreateKillOnCloseJob())
{
}

ForkRoot::~ForkRoot()
{
    Stop();
}

bool ForkRoot::Start(const wstring& executable, wstring& error)
{
    Stop();

    if (!m_job)
    {
        error = L"could not create the job object";
        return false;
    }

    SECURITY_ATTRIBUTES inheritable { sizeof(inheritable), nullptr, TRUE };
    HANDLE stdinRead = nullptr, stdinWrite = nullptr, stdoutRead = nullptr, stdoutWrite = nullptr;

    if (!CreatePipe(&stdinRead, &stdinWrite, &inheritable, 0))
    {
        error = L"CreatePipe error " + to_wstring(GetLastError());
        return false;
    }
    const UniqueHandle childStdin(stdinRead);
    m_stdin.Reset(stdinWrite);

    if (!CreatePipe(&stdoutRead, &stdoutWrite, &inheritable, 0))
    {
        error = L"CreatePipe error " + to_wstring(GetLastError());
        Stop();
        return false;
    }
    const UniqueHandle childStdout(stdoutWrite);
    m_stdout.Reset(stdoutRead);

    // Our ends must not be inherited, or the root would never see the end of its input.
    SetHandleInformation(m_stdin.Get(), HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(m_stdout.Get(), HANDLE_FLAG_INHERIT, 0);

    // Two attributes: inherit only the root's stdin and stdout (not every inheritable handle we
    // have), and start the root inside our job.
    SIZE_T attributesSize = 0;
    InitializeProcThreadAttributeList(nullptr, 2, 0, &attributesSize);
    vector<BYTE> attributesBuffer(attributesSize);
    const auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributesBuffer.data());
    HANDLE inherited[] = { childStdin.Get(), childStdout.Get() };
    HANDLE jobs[] = { m_job.Get() };

    if (!InitializeProcThreadAttributeList(attributes, 2, 0, &attributesSize))
    {
        error = L"InitializeProcThreadAttributeList error " + to_wstring(GetLastError());
        Stop();
        return false;
    }

    STARTUPINFOEXW startupInfo {};
    startupInfo.StartupInfo.cb = sizeof(startupInfo);
    startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.StartupInfo.hStdInput = childStdin.Get();
    startupInfo.StartupInfo.hStdOutput = childStdout.Get();
    startupInfo.StartupInfo.hStdError = childStdout.Get();
    startupInfo.lpAttributeList = attributes;

    PROCESS_INFORMATION processInfo {};
    wstring commandLine = L"\"" + executable + L"\"";

    // No console: the root talks only through the pipes and must not touch our screen.
    const BOOL created =
        UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)
        && UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, jobs, sizeof(jobs), nullptr, nullptr)
        && CreateProcessW(
            executable.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            EXTENDED_STARTUPINFO_PRESENT | DETACHED_PROCESS,
            nullptr,
            nullptr,
            &startupInfo.StartupInfo,
            &processInfo);
    const DWORD createError = GetLastError();
    DeleteProcThreadAttributeList(attributes);

    if (!created)
    {
        error = L"CreateProcess error " + to_wstring(createError);
        Stop();
        return false;
    }

    CloseHandle(processInfo.hThread);
    m_process.Reset(processInfo.hProcess);
    m_pid = processInfo.dwProcessId;
    return true;
}

void ForkRoot::Stop()
{
    if (m_process)
    {
        // Not waiting for the reply: if the root were stuck, we'd be stuck with it.
        if (WaitForSingleObject(m_process.Get(), 0) == WAIT_TIMEOUT)
        {
            const string kys = string(Protocol::COMMAND_KYS) + "\n";
            DWORD written = 0;
            WriteFile(m_stdin.Get(), kys.data(), static_cast<DWORD>(kys.size()), &written, nullptr);
        }

        m_stdin.Reset(); // The root also exits when its input ends
        WaitForSingleObject(m_process.Get(), STOP_TIMEOUT_MS);
    }

    // Whatever is still running (a stuck root, or processes it couldn't clean up) dies here.
    KillTree();

    m_stdin.Reset();
    m_stdout.Reset();
    m_process.Reset();
    m_pid = 0;
    m_pending.clear();
}

void ForkRoot::KillTree() const
{
    if (m_job)
    {
        TerminateJobObject(m_job.Get(), 1);
    }
}

DWORD ForkRoot::Pid() const
{
    return m_pid;
}

optional<vector<string>> ForkRoot::Send(string_view command)
{
    if (!m_stdin)
    {
        return nullopt;
    }

    const string line = string(command) + "\n";
    DWORD written = 0;
    if (!WriteFile(m_stdin.Get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr))
    {
        return nullopt;
    }

    vector<string> reply;
    while (optional<string> replyLine = ReadLine())
    {
        if (replyLine->empty())
        {
            return reply;
        }
        reply.push_back(move(*replyLine));
    }
    return nullopt;
}

optional<string> ForkRoot::ReadLine()
{
    while (true)
    {
        const size_t newline = m_pending.find('\n');
        if (newline != string::npos)
        {
            string line = m_pending.substr(0, newline);
            m_pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            return line;
        }

        char chunk[4096];
        DWORD bytesRead = 0;
        if (!ReadFile(m_stdout.Get(), chunk, sizeof(chunk), &bytesRead, nullptr) || bytesRead == 0)
        {
            return nullopt;
        }
        m_pending.append(chunk, bytesRead);
    }
}
