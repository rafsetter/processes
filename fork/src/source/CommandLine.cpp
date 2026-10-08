#include "CommandLine.h"

#include <string_view>

using namespace std;

namespace
{
constexpr wstring_view CHILD_ARGUMENT = L"--child";
constexpr wstring_view KILL_TREE_ARGUMENT = L"--kill-tree";

optional<DWORD> ParsePid(const wchar_t* text)
{
    wchar_t* end = nullptr;
    const unsigned long pid = wcstoul(text, &end, 10);
    if (end == text || *end != L'\0' || pid == 0)
    {
        return nullopt;
    }
    return pid;
}
}

optional<Options> ParseCommandLine(int argc, wchar_t* argv[])
{
    Options options;

    for (int i = 1; i < argc; ++i)
    {
        const wstring_view argument = argv[i];

        if (argument == KILL_TREE_ARGUMENT && !options.killTree)
        {
            options.killTree = true;
        }
        else if (argument == CHILD_ARGUMENT && !options.isChild && i + 1 < argc)
        {
            const optional<DWORD> rootPid = ParsePid(argv[++i]);
            if (!rootPid)
            {
                return nullopt;
            }
            options.isChild = true;
            options.rootPid = *rootPid;
        }
        else
        {
            return nullopt;
        }
    }

    return options;
}

wstring BuildChildCommandLine(const wstring& executable, DWORD rootPid, bool killTree)
{
    wstring commandLine = L"\"" + executable + L"\" " + wstring(CHILD_ARGUMENT) + L" " + to_wstring(rootPid);
    if (killTree)
    {
        commandLine += L" " + wstring(KILL_TREE_ARGUMENT);
    }
    return commandLine;
}
