# processes

## What it is

Two small Windows programs for playing with process trees:

- **`fork.exe`** creates real parent/child process trees on command: every process it creates is another `fork.exe`, and any of them can be told to create children of its own.
- **`manager.exe`** is a terminal UI that drives `fork.exe`: it draws the tree, lets you create and delete processes with the keyboard, and shows processes that were killed by someone else.

```
                       ┌────────┐
                       │  ROOT  │
                       │ 30840  │
                       └────────┘
                      ___/  │  \___
                  ___/      │      \___
                 /          │          \
           ┌────────┐  ┌────────┐  ┌────────┐
           │ 26968  │  │  1028  │  │ 25464  │
           └────────┘  └────────┘  └────────┘
─────────────────────── orphans ────────────────────────
                ┌────────┐    ┌────────┐
                │ 22040  │    │ 33008  │
                └────────┘    └────────┘
 c  Create    d  Delete    t  [ ] Kill tree    ←↑↓→  Navigate    q  Quit
```

## Why it exists

To see, by experiment, how process trees really behave on Windows. In particular:
**Windows does not kill a process's children when the process dies.** They keep running as
orphans. Killing a whole tree only happens when something does it on purpose, and here
that something is a *job object*. The tree you build in `manager.exe` is the same tree
Process Explorer or Task Manager shows, so you can compare the two side by side.

## How to build

Requirements: Windows 10 or later, and Visual Studio 2026 with the **Desktop development with C++** workload.

```bat
build.bat            :: Release
build.bat debug      :: Debug
```

Run `build.bat` (double-click it, or run it from a terminal), or open `processes.slnx` in Visual Studio.
The script prints where the executables were created and waits for a key:

```
bin\release\manager.exe
bin\release\fork.exe
```

Run `manager.exe`. It starts `fork.exe` itself, so the two must stay in the same folder.

| Key | Action |
|---|---|
| `c` | Create a child under the selected process (the selection stays, so `c` again adds a sibling) |
| `d` / `Delete` | Delete the selected process |
| `t` | Kill tree switch: processes created while it's on take their whole tree with them when they die |
| `←↑↓→` | Move between processes |
| `q` / `Esc` | Quit (kills every process) |

## How it works

```
manager.exe ──stdin/stdout──▶ fork.exe            root
                                ├─ fork.exe --child <rootPid>
                                │    └─ fork.exe --child <rootPid>
                                └─ fork.exe --child <rootPid> --kill-tree
                                     └─ ...
```

### fork.exe

**Two roles, one executable.**
- Started with no arguments, it is the **root**: it reads commands from stdin and answers on stdout.
- Started as `fork.exe --child <rootPid> [--kill-tree]`, it is a **child**: it only takes commands from the root, through its pipes.

| Command (to the root) | Reply |
|---|---|
| `create [pid] [tree\|single]` | `created <newPid>`: under `pid`, or under the root if no `pid` is given |
| `delete <pid>` | `deleted <pid>`, one line per process that died |
| `list` | the tree, indented, with `(orphan)` and `[kill-tree]` marks |
| `kys` | kills every process, replies `exited` and exits |
| | `exited <pid>` before a reply: that process died since the last command |
| | `failed <reason>` when something goes wrong |

When stdin is not a console (another program is driving the root), every reply ends with an empty line, so the program knows where it ends.

**Ownership: who creates whom.** The root never creates a process on behalf of another one.
For `create 1234`, the root asks process 1234 to create the child, and 1234 calls
`CreateProcess` itself. That's how Windows records 1234 as the real parent, and it's the
parent Process Explorer shows. The root's job is to know where every process is and to
route commands to it:

1. The root receives `create 1234`.
2. The root sends `create single` to 1234, through 1234's input pipe.
3. 1234 calls `CreateProcess("fork.exe --child <rootPid>")` and gets back the new PID, 5678.
4. 1234 replies `created 5678` through its output pipe.
5. The root connects to 5678's pipes and records it as a child of 1234.
6. The root replies `created 5678`.

**Pipe communication.** The pipes follow a star, not the tree: every child is directly
connected to the root, however deep it is. Each child owns (is the server of) two
message-mode named pipes, named after its own PID:

- `\\.\pipe\fork_input_pipe_<pid>`: root → child, commands
- `\\.\pipe\fork_output_pipe_<pid>`: child → root, replies

The root is their only client. The pipes allow a single connection, reject remote clients and
fail if their name is already taken. Both ends check who is on the other side
(`GetNamedPipeClientProcessId` / `GetNamedPipeServerProcessId`): a child accepts only the
root's PID, which it gets in its command line, and the root accepts only the expected child.
A child exits when its pipe breaks, that is, when the root is gone.

**Lifetime and job objects.** Windows doesn't kill children with their parent, so `fork.exe` uses job objects:

- **The root's job.** The root puts every child in a kill-on-close job. Processes inherit
  their parent's job, so every descendant at any depth is in it. When the root exits, or is
  killed, the job closes and Windows kills them all.
- **`single` (the default).** When the process dies, its children keep running as
  **orphans**. They're still connected to the root and can still be controlled; `list` and
  `manager.exe` show them as separate trees.
- **`tree`.** The process is started with `--kill-tree` and puts its own children in a
  kill-on-close job that only it has a handle to. When it dies (deleted, killed from Task
  Manager, crashed), the handle is closed and Windows kills everything under it. Jobs
  nest, so this works at any depth.

Whether a process kills its tree is decided when it's created and never changes. Processes created before the switch was turned on keep leaving orphans.

### manager.exe

- **Starting the root.** It starts the `fork.exe` root with its stdin and stdout connected to
  pipes and with no console, and sends it the same commands a person would type.
- **Its own job.** The root runs in manager's own kill-on-close job, so no `fork.exe` can
  outlive `manager.exe`, however it ends: `q`, closing the window, Task Manager, a crash.
  Closing the window kills the tree right away, since Windows ends the process as soon as
  the close handler returns.
- **Watcher thread.** Every 200 ms it checks every process. One that died without being
  deleted (killed from Task Manager, for example) is drawn in red, with its lines, for one
  second, then removed. Its children become orphans unless it was a tree-killing process.
  If the root dies, everything dies with it, and a new root is started.
- **Drawing.** The tree is drawn top to bottom, with the root centered at the top. One child
  gets a straight line and more children get diagonals. Normal processes are white,
  tree-killing processes orange, the selection green, dying processes red. Orphan trees are
  drawn below the main tree. It uses its own console screen buffer, so the console is
  restored on exit, and the view scrolls to keep the selection visible.

### Layout

```
processes.slnx, build.bat
bin\debug|release\           executables of both projects
fork\src\source, src\header
fork\bin\obj\debug|release\  intermediate files
manager\src\source, src\header  (also uses fork's Protocol and UniqueHandle)
manager\bin\obj\debug|release\
```
