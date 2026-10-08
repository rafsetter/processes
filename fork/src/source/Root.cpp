#include "Root.h"

#include <windows.h>

#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "ProcessTree.h"
#include "Protocol.h"

using namespace std;

namespace
{
void Print(const string& line)
{
    cout << line << endl;
}

void Print(const vector<string>& lines)
{
    for (const string& line : lines)
    {
        Print(line);
    }
}

// True when a person is typing in a console, false when another program pipes commands in.
// Only people get the banner and the prompt; programs get nothing but reply lines.
bool IsInteractive()
{
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != FALSE;
}

// A reply can have several lines (e.g. "exited" notices before "created"), so for programs
// an empty line marks where each reply ends.
void EndReply(bool interactive)
{
    if (!interactive)
    {
        Print("");
    }
}

// create [pid] [tree|single]
void Create(ProcessTree& tree, const vector<string>& arguments)
{
    optional<DWORD> pid;
    optional<bool> killTree;

    for (const string& argument : arguments)
    {
        if (!killTree && argument == Protocol::MODE_TREE)
        {
            killTree = true;
        }
        else if (!killTree && argument == Protocol::MODE_SINGLE)
        {
            killTree = false;
        }
        else if (!pid && Protocol::ParsePid(argument))
        {
            pid = Protocol::ParsePid(argument);
        }
        else
        {
            Print(Protocol::Failed("invalid arguments"));
            return;
        }
    }

    Print(tree.Create(pid.value_or(tree.RootPid()), killTree.value_or(tree.DefaultKillTree())));
}
}

int RunRoot(bool killTree)
{
    using namespace Protocol;

    ProcessTree tree(killTree);
    if (!tree.HasJob())
    {
        Print(Failed("could not create the job object"));
        return 1;
    }

    const bool interactive = IsInteractive();
    if (interactive)
    {
        Print("root " + to_string(tree.RootPid()) + (killTree ? " (--kill-tree)" : "")
            + " - commands: create [pid] [tree|single], delete <pid>, list, kys");
    }

    string line;
    while (true)
    {
        if (interactive)
        {
            cout << "> " << flush;
        }

        if (!getline(cin, line))
        {
            break;
        }

        istringstream stream(line);
        vector<string> words;
        for (string word; stream >> word;)
        {
            words.push_back(word);
        }
        if (words.empty())
        {
            continue;
        }

        Print(tree.PruneExited());

        const string& command = words[0];
        const vector<string> arguments(words.begin() + 1, words.end());
        const optional<DWORD> pid = arguments.size() == 1 ? ParsePid(arguments[0]) : nullopt;

        if (command == COMMAND_CREATE)
        {
            Create(tree, arguments);
        }
        else if (command == COMMAND_DELETE && pid)
        {
            Print(tree.Delete(*pid));
        }
        else if (command == COMMAND_LIST && arguments.empty())
        {
            Print(tree.List());
        }
        else if (command == COMMAND_KYS && arguments.empty())
        {
            break;
        }
        else
        {
            Print(Failed("unknown command or invalid arguments"));
        }

        EndReply(interactive);
    }

    tree.KillAll();
    Print(string(REPLY_EXITED));
    EndReply(interactive);
    return 0;
}
