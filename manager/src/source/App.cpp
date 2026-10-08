#include "App.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <iterator>
#include <optional>

#include "Canvas.h"
#include "Protocol.h"
#include "Renderer.h"
#include "Watcher.h"

using namespace std;

namespace
{
constexpr chrono::milliseconds WATCH_INTERVAL { 200 };
constexpr chrono::seconds DEATH_SHOWN_FOR { 1 };

// For the console signal handler, which runs on a thread of its own.
HANDLE g_quitEvent = nullptr;
const ForkRoot* g_forkRoot = nullptr;

BOOL WINAPI OnConsoleSignal(DWORD signal)
{
    if (signal == CTRL_CLOSE_EVENT || signal == CTRL_LOGOFF_EVENT || signal == CTRL_SHUTDOWN_EVENT)
    {
        // Windows terminates this process as soon as the handler returns, so the main thread never
        // gets to shut down: kill the tree here. (The job would kill it anyway once we're gone, but
        // only if we're really gone, and a debugger can keep us around.)
        g_forkRoot->KillTree();
        return TRUE;
    }

    // Ctrl+Break: quit normally. (Ctrl+C arrives as a key, since input isn't processed.)
    SetEvent(g_quitEvent);
    return TRUE;
}

wstring Widen(string_view text)
{
    return wstring(text.begin(), text.end());
}

wstring JoinPids(const vector<DWORD>& pids)
{
    wstring joined;
    for (const DWORD pid : pids)
    {
        joined += (joined.empty() ? L"" : L", ") + to_wstring(pid);
    }
    return joined;
}
}

App::App(wstring forkExecutable) : m_forkExecutable(move(forkExecutable))
{
}

int App::Run()
{
    m_redrawEvent.Reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    const UniqueHandle quitEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    g_quitEvent = quitEvent.Get();
    g_forkRoot = &m_root;

    if (!StartRoot())
    {
        wcerr << m_status << L"\n";
        return 1;
    }

    if (!m_console.Open())
    {
        wcerr << L"manager must run in a console window\n";
        return 1;
    }
    SetConsoleCtrlHandler(OnConsoleSignal, TRUE);

    {
        const Watcher watcher(WATCH_INTERVAL, [this] { CheckProcesses(); });
        const HANDLE events[] = { m_console.Input(), m_redrawEvent.Get(), quitEvent.Get() };

        while (m_running)
        {
            Draw();

            const DWORD signaled = WaitForMultipleObjects(static_cast<DWORD>(size(events)), events, FALSE, INFINITE);
            if (signaled == WAIT_OBJECT_0)
            {
                HandleInput();
            }
            else if (signaled != WAIT_OBJECT_0 + 1)
            {
                m_running = false;
            }

            bool rootDied = false;
            {
                lock_guard lock(m_mutex);
                rootDied = exchange(m_rootDied, false);
            }
            if (rootDied && m_running)
            {
                StartRoot();
            }
        }
    }

    SetConsoleCtrlHandler(OnConsoleSignal, FALSE);
    m_root.Stop();
    m_console.Close();
    return 0;
}

bool App::StartRoot()
{
    wstring error;
    const bool started = m_root.Start(m_forkExecutable, error);

    lock_guard lock(m_mutex);
    if (!started)
    {
        m_model.Clear();
        m_selectedPid = 0;
        m_status = L"could not start fork.exe: " + error;
        return false;
    }

    m_model.Reset(m_root.Pid());
    m_selectedPid = m_root.Pid();
    m_scrollX = 0;
    m_scrollY = 0;
    return true;
}

void App::HandleInput()
{
    INPUT_RECORD records[32];
    DWORD count = 0;
    if (!ReadConsoleInputW(m_console.Input(), records, static_cast<DWORD>(size(records)), &count))
    {
        m_running = false;
        return;
    }

    for (DWORD i = 0; i < count && m_running; ++i)
    {
        if (records[i].EventType == KEY_EVENT && records[i].Event.KeyEvent.bKeyDown)
        {
            HandleKey(records[i].Event.KeyEvent);
        }
        else if (records[i].EventType == WINDOW_BUFFER_SIZE_EVENT)
        {
            m_console.FitBufferToWindow();
        }
    }
}

void App::HandleKey(const KEY_EVENT_RECORD& key)
{
    const bool control = key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);

    switch (key.wVirtualKeyCode)
    {
    case VK_UP:
        Move(Direction::Up);
        break;
    case VK_DOWN:
        Move(Direction::Down);
        break;
    case VK_LEFT:
        Move(Direction::Left);
        break;
    case VK_RIGHT:
        Move(Direction::Right);
        break;
    case 'C':
        if (control)
        {
            m_running = false;
        }
        else
        {
            CreateUnderSelected();
        }
        break;
    case 'D':
    case VK_DELETE:
        DeleteSelected();
        break;
    case 'T':
        ToggleKillTreeMode();
        break;
    case 'Q':
    case VK_ESCAPE:
        m_running = false;
        break;
    }
}

void App::CreateUnderSelected()
{
    DWORD parentPid = 0;
    {
        lock_guard lock(m_mutex);
        const ProcessNode* parent = m_model.Find(m_selectedPid);
        if (!parent)
        {
            return;
        }
        if (parent->diedAt || parent->deleting)
        {
            m_status = L"process " + to_wstring(m_selectedPid) + L" is dead";
            return;
        }
        parentPid = m_selectedPid;
    }

    // Not holding the lock while waiting for the reply, so the watcher keeps running.
    const bool killTree = m_killTreeMode;
    const string command = Protocol::WithPid(Protocol::COMMAND_CREATE, parentPid) + " "
        + string(killTree ? Protocol::MODE_TREE : Protocol::MODE_SINGLE);
    const optional<vector<string>> reply = m_root.Send(command);

    lock_guard lock(m_mutex);
    if (!reply)
    {
        m_status = L"the root is not responding";
        return;
    }

    for (const string& line : *reply)
    {
        if (const optional<DWORD> pid = Protocol::ParseWithPid(line, Protocol::REPLY_CREATED))
        {
            // The selection stays on the parent, so pressing c again adds a sibling.
            m_model.Add(*pid, parentPid, killTree);
            m_status = L"created " + to_wstring(*pid) + L" under " + to_wstring(parentPid)
                + (killTree ? L", it kills its tree when it dies" : L"");
        }
        else
        {
            ApplyCommonReplyLine(line);
        }
    }
}

void App::ToggleKillTreeMode()
{
    m_killTreeMode = !m_killTreeMode;

    lock_guard lock(m_mutex);
    m_status = m_killTreeMode
        ? L"kill tree ON: new processes (orange) take their whole tree with them when they die"
        : L"kill tree OFF: new processes leave their children orphaned when they die";
}

void App::DeleteSelected()
{
    DWORD pid = 0;
    {
        lock_guard lock(m_mutex);
        ProcessNode* node = m_model.Find(m_selectedPid);
        if (!node)
        {
            return;
        }
        if (m_selectedPid == m_model.RootPid())
        {
            m_status = L"the root can't be deleted, press q to quit";
            return;
        }
        if (node->diedAt || node->deleting)
        {
            m_status = L"process " + to_wstring(m_selectedPid) + L" is dead";
            return;
        }

        // Its death (and its tree's, if it takes it along) is our doing, so the watcher must not
        // report it as killed outside.
        m_model.SetDeleting(m_selectedPid, true);
        pid = m_selectedPid;
    }

    const optional<vector<string>> reply = m_root.Send(Protocol::WithPid(Protocol::COMMAND_DELETE, pid));

    lock_guard lock(m_mutex);
    bool deleted = false;
    int deletedCount = 0;
    for (const string& line : reply.value_or(vector<string> {}))
    {
        if (const optional<DWORD> deletedPid = Protocol::ParseWithPid(line, Protocol::REPLY_DELETED))
        {
            deleted = deleted || *deletedPid == pid;
            ++deletedCount;
            SelectInsteadOf(*deletedPid);
            m_model.Remove(*deletedPid);
        }
        else
        {
            ApplyCommonReplyLine(line);
        }
    }

    if (deleted)
    {
        m_status = L"deleted " + to_wstring(pid)
            + (deletedCount > 1 ? L" and the " + to_wstring(deletedCount - 1) + L" processes of its tree" : L"");
    }
    if (!reply)
    {
        m_status = L"the root is not responding";
    }
    if (!deleted)
    {
        m_model.SetDeleting(pid, false);
    }
}

void App::Move(Direction direction)
{
    lock_guard lock(m_mutex);
    if (const optional<DWORD> next = FindNeighbor(m_layout, m_model, m_selectedPid, direction))
    {
        m_selectedPid = *next;
    }
}

void App::Draw()
{
    const COORD size = m_console.Size();
    Canvas canvas(size.X, size.Y);
    {
        lock_guard lock(m_mutex);
        if (!m_model.Find(m_selectedPid))
        {
            m_selectedPid = m_model.RootPid();
        }

        m_layout = ComputeLayout(m_model, size.X);
        ScrollToSelection(size);
        DrawScreen(canvas, m_model, m_layout, m_selectedPid, m_scrollX, m_scrollY, m_status, m_killTreeMode);
    }
    m_console.Present(canvas);
}

void App::CheckProcesses()
{
    bool changed = false;
    {
        lock_guard lock(m_mutex);
        const Clock::time_point now = Clock::now();

        const vector<DWORD> died = m_model.DetectDeaths(now);
        if (!died.empty())
        {
            changed = true;
            const DWORD rootPid = m_model.RootPid();
            m_status = ranges::find(died, rootPid) != died.end()
                ? L"root " + to_wstring(rootPid) + L" was killed outside of the program, and every process died with it"
                : L"killed outside of the program: " + JoinPids(died);
        }

        for (const DWORD pid : m_model.Expired(now, DEATH_SHOWN_FOR))
        {
            changed = true;
            if (pid == m_model.RootPid())
            {
                // The main thread starts a new root.
                m_model.Clear();
                m_selectedPid = 0;
                m_rootDied = true;
                break;
            }
            SelectInsteadOf(pid);
            m_model.Remove(pid);
        }
    }

    if (changed)
    {
        SetEvent(m_redrawEvent.Get());
    }
}

void App::ApplyCommonReplyLine(const string& line)
{
    // The root tells about processes that died before handling the command; the watcher
    // may not have noticed them yet.
    if (const optional<DWORD> pid = Protocol::ParseWithPid(line, Protocol::REPLY_EXITED))
    {
        m_model.MarkDied(*pid, Clock::now());
    }
    else if (line.starts_with(Protocol::REPLY_FAILED))
    {
        m_status = Widen(line);
    }
}

void App::SelectInsteadOf(DWORD pid)
{
    const ProcessNode* node = m_model.Find(pid);
    if (!node)
    {
        return;
    }

    // A process that kills its tree takes the selection with it if it's anywhere in that tree.
    const bool selectionGoesAway = m_selectedPid == pid || (node->killTree && m_model.IsInSubtree(m_selectedPid, pid));
    if (!selectionGoesAway)
    {
        return;
    }

    if (node->parentPid != ProcessModel::NO_PARENT)
    {
        m_selectedPid = node->parentPid;
        return;
    }

    // The root of an orphan tree: move to the next orphan tree, or to the previous one if it was the last.
    const vector<DWORD>& orphans = m_model.Orphans();
    if (const auto position = ranges::find(orphans, pid); position != orphans.end())
    {
        if (next(position) != orphans.end())
        {
            m_selectedPid = *next(position);
            return;
        }
        if (position != orphans.begin())
        {
            m_selectedPid = *prev(position);
            return;
        }
    }

    if (!node->killTree && !node->children.empty())
    {
        m_selectedPid = node->children.front(); // Becomes an orphan
        return;
    }

    m_selectedPid = m_model.RootPid();
}

void App::ScrollToSelection(COORD screenSize)
{
    const int viewWidth = screenSize.X;
    const int viewHeight = max(1, screenSize.Y - FOOTER_ROWS);

    if (const auto selected = m_layout.boxes.find(m_selectedPid); selected != m_layout.boxes.end())
    {
        // Keep the selected box, with a margin for the lines around it, in view.
        const NodeBox& box = selected->second;
        m_scrollX = min(m_scrollX, box.x - 1);
        m_scrollX = max(m_scrollX, box.x + box.width + 1 - viewWidth);
        m_scrollY = min(m_scrollY, box.y - 2);
        m_scrollY = max(m_scrollY, box.y + box.height + 2 - viewHeight);
    }

    m_scrollX = clamp(m_scrollX, 0, max(0, m_layout.width - viewWidth));
    m_scrollY = clamp(m_scrollY, 0, max(0, m_layout.height - viewHeight));
}
