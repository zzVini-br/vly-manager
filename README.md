# vly-manager

A fast, practical process manager for Linux, with a terminal UI first and a Qt GUI later.

Built to solve a real annoyance: games launched through Steam/Proton leaving behind
processes that ignore `SIGTERM`, which makes logout and shutdown hang until systemd's
stop timeout expires.

> **Status:** early development (v0.1 in progress).

## Planned features (v0.1)

- Process tree (parents, children and threads) with PID, state, user, CPU and memory
- Filter by name
- Kill a single process, or a whole tree (`SIGTERM`, wait, then `SIGKILL`)
- Highlight zombie (`Z`) and uninterruptible (`D`) processes
- Scriptable CLI: `vly kill-tree steam`
- Dark theme by default
- `.deb` package

## Building

Requirements: a C11 compiler, CMake ≥ 3.25, ncurses (`libncurses-dev`), pkg-config.

```sh
cmake -B build
cmake --build build
ctest --test-dir build
./build/app/vly --version
```

Useful options:

| Option          | Default | Description                           |
|-----------------|---------|---------------------------------------|
| `VLY_BUILD_TESTS` | `ON`  | Build unit tests                      |
| `VLY_WERROR`      | `OFF` | Treat warnings as errors              |
| `VLY_SANITIZE`    | `OFF` | Build with AddressSanitizer and UBSan |

## Project layout

```
core/    libvly: reads /proc, builds the process tree, sends signals (no UI code)
app/     vly: CLI and terminal UI
tests/   unit tests (CTest)
```

## A note on threads

Linux has no way to terminate a single thread from outside its process: fatal signals
always take down the whole thread group. vly shows threads for inspection, and the kill
action on a thread kills its owning process (with a warning).

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
