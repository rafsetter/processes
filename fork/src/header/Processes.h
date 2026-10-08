#pragma once

#include <windows.h>

#include <string>

#include "UniqueHandle.h"

struct SpawnResult
{
    DWORD pid = 0;
    DWORD error = ERROR_SUCCESS;
    UniqueHandle process;
};

std::wstring GetExecutablePath();

// A job that Windows terminates, with every process in it, once the last handle to it is closed,
// which also happens when the process holding the handle dies. Processes started by a process
// in the job are in the job too, so it covers all descendants at any depth.
UniqueHandle CreateKillOnCloseJob();

// Starts "fork.exe --child <rootPid> [--kill-tree]" as a child of the calling process.
// killTree is for the new process: whether its own descendants die with it.
// If job is given, the child is put in it before it runs any code.
SpawnResult SpawnChild(DWORD rootPid, bool killTree, HANDLE job);
