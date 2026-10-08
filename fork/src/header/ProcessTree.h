#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "UniqueHandle.h"

// Lives in the root: knows every process and talks to each one directly through its pipes,
// however deep it is. Methods return the reply lines for the user.
class ProcessTree
{
public:
    // defaultKillTree: whether "create" makes tree-killing processes when it doesn't say.
    explicit ProcessTree(bool defaultKillTree);

    // The root's job guarantees no descendant outlives the root, so the tree is unusable without it.
    bool HasJob() const;
    DWORD RootPid() const;
    bool DefaultKillTree() const;

    // killTree: when the new process dies, its descendants die too (otherwise they're orphaned).
    // Fixed for the life of the process.
    std::string Create(DWORD parentPid, bool killTree);
    std::vector<std::string> Delete(DWORD pid);
    std::vector<std::string> List() const;

    // Forgets processes that died without being deleted (e.g. killed from Task Manager).
    std::vector<std::string> PruneExited();

    // Kills every descendant, including orphans, at once.
    void KillAll();

private:
    static constexpr DWORD NO_PARENT = 0;

    struct Node
    {
        DWORD parentPid = NO_PARENT;
        std::vector<DWORD> children;
        bool killTree = false;
        UniqueHandle process;
        UniqueHandle input;  // Root writes commands here
        UniqueHandle output; // Root reads replies here
    };

    static std::optional<std::string> Request(const Node& node, std::string_view command);

    bool Attach(DWORD pid, DWORD parentPid, bool killTree, UniqueHandle process);
    std::vector<DWORD> Remove(DWORD pid);
    std::vector<DWORD>& SiblingsOf(DWORD pid);
    void CollectDescendants(DWORD pid, std::vector<DWORD>& descendants) const;
    void ListSubtree(DWORD pid, size_t depth, std::vector<std::string>& lines) const;

    const DWORD m_rootPid;
    const bool m_defaultKillTree;
    UniqueHandle m_job;
    std::unordered_map<DWORD, Node> m_nodes;

    // Processes whose parent exited. Like Process Explorer, they are shown as separate trees.
    std::vector<DWORD> m_orphans;
};
