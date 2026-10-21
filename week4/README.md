Interactive Deadlock Simulation System
A GUI desktop application written in C (C11) + GTK 3 that simulates how an Operating System
handles resource allocation and deadlocks: Banker's Algorithm (avoidance), deadlock detection,
Resource Allocation Graph, safe / unsafe / deadlocked state classification, and recovery.
> OS PBL – Week 4 project.
Features (12 menu options)
#	Screen	Owner
1-5	Create processes / resources, Allocation, Maximum, Need matrix	Team Lead
6	Resource Allocation Graph	Student 3
7-8	Request resources, Run Banker's Algorithm	Student 1
9-10	Detect deadlock, Recover from deadlock	Student 2
11	System status, utilization, SAFE / UNSAFE / DEADLOCKED	Student 3
12	Exit	Team Lead
Install the tools
OS	Command
Ubuntu / Debian	`sudo apt install build-essential libgtk-3-dev pkg-config`
Windows	Install MSYS2, open the MINGW64 terminal: `pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk3 mingw-w64-x86_64-pkg-config make`
macOS	`brew install gtk+3 pkg-config`
Check: `pkg-config --modversion gtk+-3.0` prints a version.
Build and run
```
make          # builds build/deadlock_sim
make run      # build + start the app
make test     # runs tests/test_*.c (core only, no GUI), with AddressSanitizer
make debug    # debug build with AddressSanitizer
make clean
```
Folder structure
```
src/main.c                entry point
src/core/                 algorithms + data model (NEVER include GTK here)
  state.h/.c              SystemState (Lead)         results.h   result structs (Lead)
  bankers.h/.c            Banker's algorithm (Student 1)
  detection.h/.c          deadlock detection (Student 2)
  recovery.h/.c           recovery strategies (Student 2)
  analysis.h/.c           classify + utilization (Student 3)
src/gui/                  GTK screens
  app.h/.c                main window, sidebar, status bar, shared CSS (Lead)
  setup_tabs.h/.c         menu 1-5 (Lead)
  banker_tab.h/.c         menu 7, 8 (Student 1)
  detection_tab.h/.c      menu 9, 10 (Student 2)
  rag_tab.h/.c            menu 6 (Student 3)
  status_tab.h/.c         menu 11 (Student 3)
tests/                    test_*.c, one per module owner
docs/                     scenarios.md (Student 3)
```
Files marked "STUB" are placeholders created by the Team Lead so the project compiles from day one.
Their owners replace them completely.
For teammates writing GUI tabs
A tab is `GtkWidget *xxx_tab_new(SystemState *state)`; call `state_subscribe(refresh_fn, data)` so it
updates automatically, and call `state_notify()` after you change the state.
Use `app_show_error(widget, "message")` for error dialogs and `app_set_status_message("text")` for the status bar.
Shared CSS classes (defined in `app.c`): `state-safe`, `state-unsafe`, `state-deadlock` (coloured banners),
`title`, `card`, `cell`, `cell-head`, `cell-bad`, `hint`.
Entering the Allocation matrix before Maximum is allowed: a process whose Maximum row is all zeros
is treated as "not entered yet" by `state_validate`.
Git workflow
Never commit to `main`. Use `feature/bankers`, `feature/detection-recovery`, `feature/rag-analysis`,
`feature/core-and-gui`; open a Pull Request; the Team Lead reviews and merges.
Team
Member	Role
Member 1	Team Lead – architecture, setup screens, integration
Student 1	Banker's Algorithm
Student 2	Detection & Recovery
Student 3	RAG, analysis, status dashboard

