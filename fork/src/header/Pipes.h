#pragma once

#include <windows.h>

#include <optional>
#include <string>

#include "UniqueHandle.h"

// Every non-root process owns (is the server of) two pipes named after its own PID:
//   input:  root -> process (commands)
//   output: process -> root (replies)
std::wstring InputPipeName(DWORD pid);
std::wstring OutputPipeName(DWORD pid);

// Server side, used by the process that owns the pipe. direction is PIPE_ACCESS_INBOUND or PIPE_ACCESS_OUTBOUND.
UniqueHandle CreatePipeServer(const std::wstring& name, DWORD direction);

// Blocks until a client connects, and only accepts it if it is the expected process.
bool AcceptPipeClient(HANDLE pipe, DWORD expectedClientPid);

// Client side, used by the root. Retries while the server process is still starting up and creating its pipes.
UniqueHandle ConnectToPipe(const std::wstring& name, DWORD access, DWORD expectedServerPid, HANDLE serverProcess);

bool WriteMessage(HANDLE pipe, const std::string& message);

// Returns nullopt when the other end closed the pipe or an error happened.
std::optional<std::string> ReadMessage(HANDLE pipe);
