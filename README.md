# vly-manager

A fast, practical process manager for Linux, with a terminal UI first and a Qt GUI later.

Built to solve a real annoyance: games launched through Steam/Proton leaving Steam
convinced a game is still running, so it ignores `SIGTERM` and logout or shutdown hangs
until systemd's stop timeout expires.

> **Status:** early development (v0.1 in progress). The command line works; the
> interactive terminal UI is next.

## Usage

```sh
vly list                  # process tree with CPU and memory usage
vly list steam            # only processes whose name contains "steam", with their children
vly kill-tree steam       # stop every "steam" process and everything it started
vly kill 12345            # stop a single process by pid
vly kill-tree -t 2 steam  # wait 2 s (default 5) before escalating to SIGKILL
```

`kill` and `kill-tree` take pids or process names (exact match, case-insensitive) and
stop every matching process. They send `SIGTERM`, give processes a grace period to exit
cleanly, then send `SIGKILL` to whatever is left, all in parallel. Use `kill-tree`
when the process has children: `kill` alone leaves them running.

`vly list` hides kernel threads unless given `-k`, and highlights zombie (`Z`) and
uninterruptible (`D`) processes. Colors are used only on a terminal and honor
[`NO_COLOR`](https://no-color.org).

Exit status, for scripts:

| Status | Meaning |
|--------|---------|
| 0 | every target stopped |
| 1 | usage error, or no matching process |
| 2 | partial: some processes belong to another user (run with `sudo`) or did not exit |

### Safety

- Signals go through pidfds, after checking the process start time, so a pid reused
  by another process between listing and killing is never signalled.
- pid 1, kernel threads and vly itself are never targeted.
- Zombies are reported, not signalled: they are already dead, and only their parent can
  clear them.

## Roadmap (v0.1)

- [x] Process tree with pid, state, user, CPU and memory
- [x] Filter by name
- [x] Stop a process or a whole tree (`SIGTERM`, wait, then `SIGKILL`)
- [x] Highlight zombie (`Z`) and uninterruptible (`D`) processes
- [x] Scriptable CLI: `vly kill-tree steam`
- [ ] Interactive terminal UI with threads view, dark theme by default
- [ ] `.deb` package

## Building

Requirements: a C11 compiler, CMake ≥ 3.25 and Linux ≥ 5.3 (for pidfds).

```sh
cmake -B build
cmake --build build
ctest --test-dir build
./build/app/vly list
```

Useful options:

| Option            | Default | Description                           |
|-------------------|---------|---------------------------------------|
| `VLY_BUILD_TESTS` | `ON`    | Build unit tests                      |
| `VLY_WERROR`      | `OFF`   | Treat warnings as errors              |
| `VLY_SANITIZE`    | `OFF`   | Build with AddressSanitizer and UBSan |

## Project layout

```
core/    libvly: reads /proc, builds the process tree, sends signals (no UI code)
app/     vly: command line (and, soon, the terminal UI)
tests/   unit tests and end-to-end CLI tests (CTest)
```

## A note on threads

Linux has no way to terminate a single thread from outside its process: fatal signals
always take down the whole thread group. vly will show threads for inspection, and the
kill action on a thread will kill its owning process (with a warning).

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
