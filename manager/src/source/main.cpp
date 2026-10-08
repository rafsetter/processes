#include <windows.h>

#include <iostream>
#include <string>

#include "App.h"

namespace
{
std::wstring ExecutableDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = 0;
    while ((length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()))) == path.size())
    {
        path.resize(path.size() * 2);
    }
    path.resize(length);
    return path.substr(0, path.find_last_of(L'\\') + 1);
}
}

int wmain()
{
    // Both projects of the solution are built into the same folder.
    const std::wstring forkExecutable = ExecutableDirectory() + L"fork.exe";
    if (GetFileAttributesW(forkExecutable.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        std::wcerr << L"fork.exe not found at " << forkExecutable << L"\nBuild the whole solution (fork and manager).\n";
        return 1;
    }

    return App(forkExecutable).Run();
}
