#pragma once

#include <windows.h>

#include <chrono>
#include <optional>
#include <unordered_map>
#include <vector>

#include "UniqueHandle.h"

using Clock = std::chrono::steady_clock;

struct ProcessNode
{
    DWORD parentPid = 0; // ProcessModel::NO_PARENT for the root and for orphans
    std::vector<DWORD> children;
    bool killTree = false; // Its descendants die with it, instead of being orphaned
    UniqueHandle process; // Lets us notice when it dies
    bool deleting = false; // We asked for it to die, so it wasn't killed outside of the program
    std::optional<Clock::time_point> diedAt; // Killed outside of the program; shown in red until removed
};

// The tree as manager shows it, mirroring what the fork.exe root knows: when a process
// dies, its children keep running as orphans, unless it was created to kill its tree.
// Not thread-safe.
class ProcessModel
{
public:
    static constexpr DWORD NO_PARENT = 0;

    // Starts over with a tree that only has the root.
    void Reset(DWORD rootPid);
    void Clear();

    DWORD RootPid() const;
    const std::unordered_map<DWORD, ProcessNode>& Nodes() const;
    const std::vector<DWORD>& Orphans() const;
    ProcessNode* Find(DWORD pid);
    const ProcessNode* Find(DWORD pid) const;
    bool IsDying(DWORD pid) const;

    // True if pid is ancestor or one of its descendants.
    bool IsInSubtree(DWORD pid, DWORD ancestor) const;

    // If the parent is gone, the new process is an orphan.
    void Add(DWORD pid, DWORD parentPid, bool killTree);

    // Forgets a process. Its children become orphans, or, if it killed its tree, are forgotten too.
    // Removing the root clears everything.
    void Remove(DWORD pid);

    // Marks a process we're about to delete (or not anymore), and the processes that will die with it.
    void SetDeleting(DWORD pid, bool deleting);

    // Marks a process as killed outside of the program, unless we already know it's dead or deleting it.
    bool MarkDied(DWORD pid, Clock::time_point when);

    // Checks every process and marks the ones that died. Returns the newly found ones.
    std::vector<DWORD> DetectDeaths(Clock::time_point now);

    // Dead processes that have been shown for long enough.
    std::vector<DWORD> Expired(Clock::time_point now, Clock::duration shownFor) const;

private:
    std::vector<DWORD>& SiblingsOf(const ProcessNode& node);
    void SetSubtreeDeleting(DWORD pid, bool deleting);
    void MarkSubtreeDied(DWORD pid, Clock::time_point when);
    void Forget(DWORD pid); // With all its descendants

    DWORD m_rootPid = 0;
    std::unordered_map<DWORD, ProcessNode> m_nodes;
    std::vector<DWORD> m_orphans; // Roots of the orphan trees
};
