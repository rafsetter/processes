#include "Pipes.h"

using namespace std;

namespace
{
constexpr DWORD PIPE_BUFFER_SIZE = 512;
constexpr DWORD PIPE_CONNECT_TIMEOUT_MS = 5000;
}

wstring InputPipeName(DWORD pid)
{
    return L"\\\\.\\pipe\\fork_input_pipe_" + to_wstring(pid);
}

wstring OutputPipeName(DWORD pid)
{
    return L"\\\\.\\pipe\\fork_output_pipe_" + to_wstring(pid);
}

UniqueHandle CreatePipeServer(const wstring& name, DWORD direction)
{
    return UniqueHandle(CreateNamedPipeW(
        name.c_str(),
        direction | FILE_FLAG_FIRST_PIPE_INSTANCE, // Fail if someone else already created this name
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1,                                         // Only the root may connect
        PIPE_BUFFER_SIZE,
        PIPE_BUFFER_SIZE,
        0,
        nullptr));
}

bool AcceptPipeClient(HANDLE pipe, DWORD expectedClientPid)
{
    if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED)
    {
        return false;
    }

    ULONG clientPid = 0;
    return GetNamedPipeClientProcessId(pipe, &clientPid) && clientPid == expectedClientPid;
}

UniqueHandle ConnectToPipe(const wstring& name, DWORD access, DWORD expectedServerPid, HANDLE serverProcess)
{
    const ULONGLONG deadline = GetTickCount64() + PIPE_CONNECT_TIMEOUT_MS;

    while (true)
    {
        UniqueHandle pipe(CreateFileW(
            name.c_str(),
            access,
            0,
            nullptr,
            OPEN_EXISTING,
            SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, // Don't let the server impersonate us beyond identification
            nullptr));

        if (pipe)
        {
            ULONG serverPid = 0;
            if (!GetNamedPipeServerProcessId(pipe.Get(), &serverPid) || serverPid != expectedServerPid)
            {
                return {};
            }

            if (access & GENERIC_READ)
            {
                DWORD mode = PIPE_READMODE_MESSAGE;
                if (!SetNamedPipeHandleState(pipe.Get(), &mode, nullptr, nullptr))
                {
                    return {};
                }
            }

            return pipe;
        }

        if (GetLastError() != ERROR_FILE_NOT_FOUND
            || GetTickCount64() >= deadline
            || WaitForSingleObject(serverProcess, 0) != WAIT_TIMEOUT)
        {
            return {};
        }

        Sleep(10);
    }
}

bool WriteMessage(HANDLE pipe, const string& message)
{
    DWORD bytesWritten = 0;
    return WriteFile(pipe, message.data(), static_cast<DWORD>(message.size()), &bytesWritten, nullptr)
        && bytesWritten == message.size();
}

optional<string> ReadMessage(HANDLE pipe)
{
    string message;
    char buffer[PIPE_BUFFER_SIZE];

    while (true)
    {
        DWORD bytesRead = 0;
        if (ReadFile(pipe, buffer, sizeof(buffer), &bytesRead, nullptr))
        {
            message.append(buffer, bytesRead);
            return message;
        }

        if (GetLastError() != ERROR_MORE_DATA)
        {
            return nullopt;
        }

        message.append(buffer, bytesRead);
    }
}
