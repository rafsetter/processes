#pragma once

#include <windows.h>

#include <mutex>
#include <string>
#include <vector>

#include "Console.h"
#include "ForkRoot.h"
#include "Layout.h"
#include "ProcessModel.h"
#include "UniqueHandle.h"

// The TUI: draws the process tree and turns key presses into commands for the fork.exe root.
class App
{
public:
    explicit App(std::wstring forkExecutable);

    int Run();

private:
    bool StartRoot();
    void HandleInput();
    void HandleKey(const KEY_EVENT_RECORD& key);
    void CreateUnderSelected();
    void DeleteSelected();
    void ToggleKillTreeMode();
    void Move(Direction direction);
    void Draw();

    // Runs on the watcher thread.
    void CheckProcesses();

    // The functions below need m_mutex to be held.
    void ApplyCommonReplyLine(const std::string& line);
    void SelectInsteadOf(DWORD pid);
    void ScrollToSelection(COORD screenSize);

    const std::wstring m_forkExecutable;
    Console m_console;
    ForkRoot m_root; // Main thread only
    UniqueHandle m_redrawEvent;
    bool m_running = true;
    bool m_killTreeMode = false; // Main thread only: whether new processes kill their tree when they die

    std::mutex m_mutex; // Guards everything below, shared with the watcher thread
    ProcessModel m_model;
    Layout m_layout;
    DWORD m_selectedPid = 0;
    std::wstring m_status;
    bool m_rootDied = false;
    int m_scrollX = 0;
    int m_scrollY = 0;
};
