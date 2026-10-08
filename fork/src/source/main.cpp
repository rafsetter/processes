#include <windows.h>

#include <iostream>
#include <optional>

#include "Child.h"
#include "CommandLine.h"
#include "Root.h"

int wmain(int argc, wchar_t* argv[])
{
    const std::optional<Options> options = ParseCommandLine(argc, argv);
    if (!options)
    {
        std::wcerr << USAGE;
        return 1;
    }

    return options->isChild ? RunChild(options->rootPid, options->killTree) : RunRoot(options->killTree);
}
