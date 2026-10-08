#pragma once

#include <windows.h>

#include <optional>
#include <string>

struct Options
{
    bool isChild = false;
    DWORD rootPid = 0; // Children only: the process they accept commands from
    bool killTree = false;
};

inline constexpr wchar_t USAGE[] =
    L"usage:\n"
    L"  fork.exe [--kill-tree]                  root, reads commands from stdin\n"
    L"  fork.exe --child <rootPid> [--kill-tree] child, reads commands from its input pipe (started by fork.exe itself)\n"
    L"\n"
    L"  --kill-tree  root: \"create\" without \"tree\" or \"single\" makes tree-killing processes\n"
    L"               child: when this process dies, Windows also kills its descendants\n"
    L"               (default: they are left orphaned)\n";

std::optional<Options> ParseCommandLine(int argc, wchar_t* argv[]);

// Full command line for a child, argv[0] included, as CreateProcess expects it.
std::wstring BuildChildCommandLine(const std::wstring& executable, DWORD rootPid, bool killTree);
