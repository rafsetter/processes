#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "UniqueHandle.h"

// The fork.exe root process, driven through its stdin and stdout. Only the root's
// own thread may use it, since every command waits for its reply; KillTree() is the exception.
//
// The root runs in a job that Windows kills when manager ends, however it ends
// (closed, killed, crashed), and every fork.exe process is in it: they can't outlive manager.
class ForkRoot
{
public:
    ForkRoot();
    ~ForkRoot();

    bool Start(const std::wstring& executable, std::wstring& error);

    // Asks the root to kill every process and exit, waits a bit for it, then kills whatever is left.
    void Stop();

    // Kills the root and every process under it, right away. Safe to call from any thread.
    void KillTree() const;

    DWORD Pid() const;

    // Sends one command and returns the lines of its reply, or nullopt if the root is gone.
    std::optional<std::vector<std::string>> Send(std::string_view command);

private:
    std::optional<std::string> ReadLine();

    const UniqueHandle m_job;
    UniqueHandle m_process;
    UniqueHandle m_stdin;  // We write commands here
    UniqueHandle m_stdout; // We read replies here
    DWORD m_pid = 0;
    std::string m_pending; // Read from stdout but not yet split into lines
};
