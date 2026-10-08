#include "Child.h"

#include <optional>
#include <string>
#include <string_view>

#include "Pipes.h"
#include "Processes.h"
#include "Protocol.h"

using namespace std;

namespace
{
// "create tree" or "create single"; nullopt if the command isn't a create.
optional<bool> ParseCreate(string_view command)
{
    const string tree = string(Protocol::COMMAND_CREATE) + " " + string(Protocol::MODE_TREE);
    const string single = string(Protocol::COMMAND_CREATE) + " " + string(Protocol::MODE_SINGLE);
    if (command == tree)
    {
        return true;
    }
    if (command == single || command == Protocol::COMMAND_CREATE)
    {
        return false;
    }
    return nullopt;
}

// job: where the new child goes; nullptr when this process leaves its children orphaned.
string CreateChild(DWORD rootPid, bool childKillsTree, HANDLE job)
{
    // Spawned by this process, so Windows records this process as its parent.
    const SpawnResult child = SpawnChild(rootPid, childKillsTree, job);
    return child.pid != 0
        ? Protocol::WithPid(Protocol::REPLY_CREATED, child.pid)
        : Protocol::Failed("CreateProcess error " + to_string(child.error));
}
}

int RunChild(DWORD rootPid, bool killTree)
{
    // With --kill-tree, this process's children go in a job that only this process holds a
    // handle to. However this process dies, the handle gets closed and Windows kills everything
    // in the job: its children and, since they inherit the job, all of their descendants.
    const UniqueHandle job = killTree ? CreateKillOnCloseJob() : UniqueHandle {};

    const DWORD pid = GetCurrentProcessId();
    const UniqueHandle input = CreatePipeServer(InputPipeName(pid), PIPE_ACCESS_INBOUND);
    const UniqueHandle output = CreatePipeServer(OutputPipeName(pid), PIPE_ACCESS_OUTBOUND);

    if (!input || !output || !AcceptPipeClient(input.Get(), rootPid) || !AcceptPipeClient(output.Get(), rootPid))
    {
        return 1;
    }

    // Ends when the root closes the pipe (e.g. it exited).
    while (optional<string> command = ReadMessage(input.Get()))
    {
        if (*command == Protocol::COMMAND_KYS)
        {
            WriteMessage(output.Get(), string(Protocol::REPLY_EXITED));
            return 0;
        }

        string reply;
        if (const optional<bool> childKillsTree = ParseCreate(*command))
        {
            reply = killTree && !job
                ? Protocol::Failed("could not create the job object")
                : CreateChild(rootPid, *childKillsTree, job.Get());
        }
        else
        {
            reply = Protocol::Failed("unknown command " + *command);
        }

        if (!WriteMessage(output.Get(), reply))
        {
            return 1;
        }
    }

    return 0;
}
