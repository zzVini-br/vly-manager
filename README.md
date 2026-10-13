<p align="center">
  <img src="assets/vly-manager.svg" alt="vly-manager logo: Tux in aviator goggles peeking over a monitor that shows a process tree" width="160">
</p>

<h1 align="center">vly-manager</h1>

<p align="center"><strong>"Fly" through your processes and system.</strong></p>

A fast, practical process and system manager for Linux: see what is running, find it,
and stop it, from a command line built for scripts or an interactive view.

Built to solve a real annoyance: games launched through Steam/Proton leaving Steam
convinced a game is still running, so it ignores `SIGTERM` and logout or shutdown hangs
until systemd's stop timeout expires.

> **Status:** early development. v0.1 shipped the command line; the interactive view
> is in progress. See the [changelog](CHANGELOG.md) for what changed in each release.

## Interactive view

<img align="right" src="assets/vly-manager-pointing.svg" alt="Tux in aviator goggles pointing at a monitor full of processes" width="120">

Run `vly` with no arguments in a terminal. The process tree refreshes every second, on
a dark background (`vly --no-theme` keeps your terminal's own).

| Key | Action |
|-----|--------|
| `↑` `↓` `PgUp` `PgDn` `Home` `End` | move |
| `←` `→` | collapse / expand, go to parent / child |
| type a name | jump to it; repeat a letter to cycle through matches |
| `/` | filter by name (`Esc` clears the filter) |
| `Tab` | switch between the tree and lists sorted by CPU or memory |
| `Enter` | show the threads of the selected process |
| `Del` | stop the process (`SIGTERM`, then `SIGKILL`) |
| `Shift+Del` | stop the process and all its descendants, after a confirmation |
| `?` `F1` | help |
| `Esc` `F10` | quit |

Stopping runs in the background, so the view stays live while processes get their
grace period; rows being stopped are marked, and the footer reports the outcome.

## Command line

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

## Roadmap

vly-manager is growing from a command-line tool into a full process and system monitor.

- [x] **Command line** (v0.1): list, filter and stop processes or whole trees, safely
- [ ] **Interactive view** (v0.2, in progress): a live process tree you can search,
      filter, sort and act on from the keyboard
- [ ] **System activity**: network and disk usage, per process and for the whole system
- [ ] **Apps and processes**: desktop applications told apart from background processes,
      as in a desktop task manager
- [ ] **Packaging**: a `.deb` package with a desktop entry
- [ ] **Graphical interface**: a Qt front end, with polkit for other users' processes

## Building

Requirements: a C11 compiler, CMake ≥ 3.25, ncurses (`libncurses-dev`), pkg-config and
Linux ≥ 5.3 (for pidfds).

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
app/     vly: command line
app/tui/ interactive view; tree_view, quickfind, threads_view and kill_jobs hold its
         logic without curses, so it is unit-tested
tests/   unit tests and end-to-end CLI tests (CTest)
```

## A note on threads

Linux has no way to terminate a single thread from outside its process: fatal signals
always take down the whole thread group. vly shows threads for inspection, and `Del` on
a thread asks before stopping its whole process.

## Contributing

Contributions are welcome: bug reports, ideas and pull requests. Open an issue before
starting on a larger change so we can agree on the approach. See
[CONTRIBUTING.md](CONTRIBUTING.md) for how to build, test and shape your commits.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
