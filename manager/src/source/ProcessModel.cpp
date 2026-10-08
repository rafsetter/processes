#include "ProcessModel.h"

#include <algorithm>

using namespace std;

void ProcessModel::Reset(DWORD rootPid)
{
    Clear();
    m_rootPid = rootPid;
    m_nodes[rootPid].process.Reset(OpenProcess(SYNCHRONIZE, FALSE, rootPid));
}

void ProcessModel::Clear()
{
    m_nodes.clear();
    m_orphans.clear();
    m_rootPid = 0;
}

DWORD ProcessModel::RootPid() const
{
    return m_rootPid;
}

const unordered_map<DWORD, ProcessNode>& ProcessModel::Nodes() const
{
    return m_nodes;
}

const vector<DWORD>& ProcessModel::Orphans() const
{
    return m_orphans;
}

ProcessNode* ProcessModel::Find(DWORD pid)
{
    const auto node = m_nodes.find(pid);
    return node == m_nodes.end() ? nullptr : &node->second;
}

const ProcessNode* ProcessModel::Find(DWORD pid) const
{
    const auto node = m_nodes.find(pid);
    return node == m_nodes.end() ? nullptr : &node->second;
}

bool ProcessModel::IsDying(DWORD pid) const
{
    const ProcessNode* node = Find(pid);
    return node && node->diedAt;
}

bool ProcessModel::IsInSubtree(DWORD pid, DWORD ancestor) const
{
    while (pid != NO_PARENT)
    {
        if (pid == ancestor)
        {
            return true;
        }

        const ProcessNode* node = Find(pid);
        if (!node)
        {
            return false;
        }
        pid = node->parentPid;
    }
    return false;
}

void ProcessModel::Add(DWORD pid, DWORD parentPid, bool killTree)
{
    ProcessNode node;
    node.parentPid = m_nodes.contains(parentPid) ? parentPid : NO_PARENT;
    node.killTree = killTree;
    node.process.Reset(OpenProcess(SYNCHRONIZE, FALSE, pid));

    SiblingsOf(node).push_back(pid);
    m_nodes.emplace(pid, move(node));
}

void ProcessModel::Remove(DWORD pid)
{
    if (pid == m_rootPid)
    {
        Clear();
        return;
    }

    const auto node = m_nodes.find(pid);
    if (node == m_nodes.end())
    {
        return;
    }

    erase(SiblingsOf(node->second), pid);
    if (node->second.killTree)
    {
        Forget(pid);
        return;
    }

    for (const DWORD child : node->second.children)
    {
        m_nodes.at(child).parentPid = NO_PARENT;
        m_orphans.push_back(child);
    }
    m_nodes.erase(node);
}

void ProcessModel::SetDeleting(DWORD pid, bool deleting)
{
    ProcessNode* node = Find(pid);
    if (!node)
    {
        return;
    }

    if (!node->killTree)
    {
        node->deleting = deleting;
        return;
    }

    // Everything below it is in its job, whatever their own setting.
    SetSubtreeDeleting(pid, deleting);
}

void ProcessModel::SetSubtreeDeleting(DWORD pid, bool deleting)
{
    ProcessNode& node = m_nodes.at(pid);
    node.deleting = deleting;
    for (const DWORD child : node.children)
    {
        SetSubtreeDeleting(child, deleting);
    }
}

bool ProcessModel::MarkDied(DWORD pid, Clock::time_point when)
{
    ProcessNode* node = Find(pid);
    if (!node || node->deleting || node->diedAt)
    {
        return false;
    }
    node->diedAt = when;
    return true;
}

vector<DWORD> ProcessModel::DetectDeaths(Clock::time_point now)
{
    vector<DWORD> died;
    for (const auto& [pid, node] : m_nodes)
    {
        // A process we couldn't open had already exited.
        if (!node.process || WaitForSingleObject(node.process.Get(), 0) == WAIT_OBJECT_0)
        {
            if (MarkDied(pid, now))
            {
                died.push_back(pid);
            }
        }
    }

    // Every process is in the root's job, so they all die with it.
    if (IsDying(m_rootPid))
    {
        for (const auto& [pid, node] : m_nodes)
        {
            if (MarkDied(pid, now))
            {
                died.push_back(pid);
            }
        }
        return died;
    }

    // Windows is killing their descendants (through their job), so they turn red together.
    for (const DWORD pid : vector<DWORD>(died))
    {
        if (m_nodes.at(pid).killTree)
        {
            for (const DWORD child : m_nodes.at(pid).children)
            {
                MarkSubtreeDied(child, now);
            }
        }
    }

    return died;
}

vector<DWORD> ProcessModel::Expired(Clock::time_point now, Clock::duration shownFor) const
{
    vector<DWORD> expired;
    for (const auto& [pid, node] : m_nodes)
    {
        if (node.diedAt && now - *node.diedAt >= shownFor)
        {
            expired.push_back(pid);
        }
    }
    return expired;
}

vector<DWORD>& ProcessModel::SiblingsOf(const ProcessNode& node)
{
    return node.parentPid == NO_PARENT ? m_orphans : m_nodes.at(node.parentPid).children;
}

void ProcessModel::MarkSubtreeDied(DWORD pid, Clock::time_point when)
{
    ProcessNode& node = m_nodes.at(pid);
    if (!node.diedAt)
    {
        node.diedAt = when;
    }
    for (const DWORD child : node.children)
    {
        MarkSubtreeDied(child, when);
    }
}

void ProcessModel::Forget(DWORD pid)
{
    for (const DWORD child : m_nodes.at(pid).children)
    {
        Forget(child);
    }
    m_nodes.erase(pid);
}
