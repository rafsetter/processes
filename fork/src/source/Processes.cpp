#include "Processes.h"

#include "CommandLine.h"

using namespace std;

wstring GetExecutablePath()
{
    wstring path(MAX_PATH, L'\0');
    while (true)
    {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0)
        {
            return {};
        }

        if (length < path.size())
        {
            path.resize(length);
            return path;
        }

        path.resize(path.size() * 2);
    }
}

UniqueHandle CreateKillOnCloseJob()
{
    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job)
    {
        return {};
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    {
        return {};
    }

    return job;
}

SpawnResult SpawnChild(DWORD rootPid, bool killTree, HANDLE job)
{
    SpawnResult result;

    const wstring executable = GetExecutablePath();
    if (executable.empty())
    {
        result.error = GetLastError();
        return result;
    }

    // CreateProcess may write to the command line buffer, so it can't be a const string.
    wstring commandLine = BuildChildCommandLine(executable, rootPid, killTree);

    STARTUPINFOW startupInfo {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo {};

    // Children talk only through pipes; they must not share (and read from) the root's console.
    // Suspended until it's in the job, so it can't start a grandchild outside of it.
    const DWORD flags = DETACHED_PROCESS | (job ? CREATE_SUSPENDED : 0);

    if (!CreateProcessW(
        executable.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        flags,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo))
    {
        result.error = GetLastError();
        return result;
    }

    UniqueHandle process(processInfo.hProcess);
    UniqueHandle thread(processInfo.hThread);

    if (job && (!AssignProcessToJobObject(job, process.Get()) || ResumeThread(thread.Get()) == static_cast<DWORD>(-1)))
    {
        result.error = GetLastError();
        TerminateProcess(process.Get(), 1);
        return result;
    }

    result.pid = processInfo.dwProcessId;
    result.process = move(process);
    return result;
}
