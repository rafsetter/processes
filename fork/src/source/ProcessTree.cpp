#include "ProcessTree.h"

#include "Pipes.h"
#include "Processes.h"
#include "Protocol.h"

using namespace std;

namespace
{
constexpr DWORD EXIT_TIMEOUT_MS = 2000;

// Gives the process time to exit on its own, then terminates it.
void WaitForExit(HANDLE process)
{
    if (WaitForSingleObject(process, EXIT_TIMEOUT_MS) != WAIT_OBJECT_0)
    {
        TerminateProcess(process, 1);
        WaitForSingleObject(process, EXIT_TIMEOUT_MS);
    }
}
}

ProcessTree::ProcessTree(bool defaultKillTree)
    : m_rootPid(GetCurrentProcessId())
    , m_defaultKillTree(defaultKillTree)
    , m_job(CreateKillOnCloseJob())
{
    m_nodes[m_rootPid];
}

bool ProcessTree::HasJob() const
{
    return static_cast<bool>(m_job);
}

DWORD ProcessTree::RootPid() const
{
    return m_rootPid;
}

bool ProcessTree::DefaultKillTree() const
{
    return m_defaultKillTree;
}

string ProcessTree::Create(DWORD parentPid, bool killTree)
{
    const auto parent = m_nodes.find(parentPid);
    if (parent == m_nodes.end())
    {
        return Protocol::Failed("unknown pid " + to_string(parentPid));
    }

    DWORD childPid = 0;
    UniqueHandle childProcess;

    if (parentPid == m_rootPid)
    {
        SpawnResult child = SpawnChild(m_rootPid, killTree, m_job.Get());
        if (child.pid == 0)
        {
            return Protocol::Failed("CreateProcess error " + to_string(child.error));
        }
        childPid = child.pid;
        childProcess = move(child.process);
    }
    else
    {
        // The parent spawns it itself, so Windows records it as the parent.
        const string command = string(Protocol::COMMAND_CREATE) + " "
            + string(killTree ? Protocol::MODE_TREE : Protocol::MODE_SINGLE);
        const optional<string> reply = Request(parent->second, command);
        if (!reply)
        {
            return Protocol::Failed("pid " + to_string(parentPid) + " is not responding");
        }

        const optional<DWORD> pid = Protocol::ParseWithPid(*reply, Protocol::REPLY_CREATED);
        if (!pid)
        {
            return reply->starts_with(Protocol::REPLY_FAILED) ? *reply : Protocol::Failed("unexpected reply: " + *reply);
        }

        childPid = *pid;
        childProcess.Reset(OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, childPid));
        if (!childProcess)
        {
            return Protocol::Failed("OpenProcess error " + to_string(GetLastError()));
        }
    }

    if (!Attach(childPid, parentPid, killTree, move(childProcess)))
    {
        return Protocol::Failed("could not connect to pid " + to_string(childPid));
    }

    return Protocol::WithPid(Protocol::REPLY_CREATED, childPid);
}

vector<string> ProcessTree::Delete(DWORD pid)
{
    if (pid == m_rootPid)
    {
        return { Protocol::Failed("cannot delete the root, use kys") };
    }

    const auto node = m_nodes.find(pid);
    if (node == m_nodes.end())
    {
        return { Protocol::Failed("unknown pid " + to_string(pid)) };
    }

    Request(node->second, Protocol::COMMAND_KYS);
    WaitForExit(node->second.process.Get());

    vector<string> lines;
    for (const DWORD removed : Remove(pid))
    {
        lines.push_back(Protocol::WithPid(Protocol::REPLY_DELETED, removed));
    }
    return lines;
}

vector<string> ProcessTree::List() const
{
    vector<string> lines;
    ListSubtree(m_rootPid, 0, lines);
    for (const DWORD orphan : m_orphans)
    {
        ListSubtree(orphan, 0, lines);
    }
    return lines;
}

vector<string> ProcessTree::PruneExited()
{
    vector<DWORD> exited;
    for (const auto& [pid, node] : m_nodes)
    {
        if (node.process && WaitForSingleObject(node.process.Get(), 0) == WAIT_OBJECT_0)
        {
            exited.push_back(pid);
        }
    }

    vector<string> lines;
    for (const DWORD pid : exited)
    {
        // May already be gone, removed along with an exited ancestor.
        if (!m_nodes.contains(pid))
        {
            continue;
        }

        for (const DWORD removed : Remove(pid))
        {
            lines.push_back(Protocol::WithPid(Protocol::REPLY_EXITED, removed));
        }
    }
    return lines;
}

void ProcessTree::KillAll()
{
    // The root's job holds every descendant, whatever its parent and including nested jobs.
    // The root itself is not in it.
    TerminateJobObject(m_job.Get(), 0);

    for (const auto& [pid, node] : m_nodes)
    {
        if (node.process)
        {
            WaitForSingleObject(node.process.Get(), EXIT_TIMEOUT_MS);
        }
    }

    erase_if(m_nodes, [this](const auto& entry) { return entry.first != m_rootPid; });
    m_nodes.at(m_rootPid).children.clear();
    m_orphans.clear();
}

optional<string> ProcessTree::Request(const Node& node, string_view command)
{
    if (!WriteMessage(node.input.Get(), string(command)))
    {
        return nullopt;
    }
    return ReadMessage(node.output.Get());
}

bool ProcessTree::Attach(DWORD pid, DWORD parentPid, bool killTree, UniqueHandle process)
{
    Node node;
    node.parentPid = parentPid;
    node.killTree = killTree;
    node.input = ConnectToPipe(InputPipeName(pid), GENERIC_WRITE, pid, process.Get());
    if (node.input)
    {
        node.output = ConnectToPipe(OutputPipeName(pid), GENERIC_READ | FILE_WRITE_ATTRIBUTES, pid, process.Get());
    }

    if (!node.output)
    {
        TerminateProcess(process.Get(), 1);
        return false;
    }

    node.process = move(process);
    m_nodes.at(parentPid).children.push_back(pid);
    m_nodes.emplace(pid, move(node));
    return true;
}

// Forgets a process that has exited and returns every PID that was forgotten.
vector<DWORD> ProcessTree::Remove(DWORD pid)
{
    erase(SiblingsOf(pid), pid);
    vector<DWORD> removed { pid };

    if (m_nodes.at(pid).killTree)
    {
        // Its job died with it, and Windows is killing its descendants.
        CollectDescendants(pid, removed);
        for (size_t i = 1; i < removed.size(); ++i)
        {
            WaitForExit(m_nodes.at(removed[i]).process.Get());
        }
        for (const DWORD dead : removed)
        {
            m_nodes.erase(dead);
        }
    }
    else
    {
        // Windows doesn't kill children with their parent: they keep running, parentless.
        for (const DWORD child : m_nodes.at(pid).children)
        {
            m_nodes.at(child).parentPid = NO_PARENT;
            m_orphans.push_back(child);
        }
        m_nodes.erase(pid);
    }

    return removed;
}

vector<DWORD>& ProcessTree::SiblingsOf(DWORD pid)
{
    const DWORD parentPid = m_nodes.at(pid).parentPid;
    return parentPid == NO_PARENT ? m_orphans : m_nodes.at(parentPid).children;
}

void ProcessTree::CollectDescendants(DWORD pid, vector<DWORD>& descendants) const
{
    for (const DWORD child : m_nodes.at(pid).children)
    {
        descendants.push_back(child);
        CollectDescendants(child, descendants);
    }
}

void ProcessTree::ListSubtree(DWORD pid, size_t depth, vector<string>& lines) const
{
    const Node& node = m_nodes.at(pid);
    string line = string(depth * 2, ' ') + to_string(pid);
    if (pid == m_rootPid)
    {
        line += " (root)";
    }
    else if (node.parentPid == NO_PARENT)
    {
        line += " (orphan)";
    }
    if (node.killTree)
    {
        line += " [kill-tree]";
    }

    lines.push_back(line);
    for (const DWORD child : node.children)
    {
        ListSubtree(child, depth + 1, lines);
    }
}
