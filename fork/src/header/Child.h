#pragma once

#include <windows.h>

// A non-root process: it only executes the commands the root sends through its input pipe.
int RunChild(DWORD rootPid, bool killTree);
