# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-10-11

First release: a command-line process manager.

### Added

- `vly list`: process tree with pid, state, user, CPU and memory usage.
  - Optional name filter that shows matching processes with their children.
  - Kernel threads hidden by default (`-k` shows them).
  - Zombie (`Z`) and uninterruptible (`D`) processes highlighted in color.
- `vly kill` and `vly kill-tree`: stop processes by pid or name, sending `SIGTERM`,
  waiting a grace period (`-t`, default 5 s) and then `SIGKILL` for anything left.
  - Every process with a matching name is stopped, all in parallel.
  - `kill-tree` also stops all descendants, parents first.
  - Reports what exited on `SIGTERM`, what needed `SIGKILL`, and what could not be
    stopped (other users' processes, processes stuck in `D` state, zombies).
  - Exit status 0 / 1 / 2 for success / not found / partial, for use in scripts.
- Safe signalling through pidfds: a pid reused by another process is never signalled,
  and pid 1, kernel threads and vly itself are never targeted.

[Unreleased]: https://github.com/zzVini-br/vly-manager/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/zzVini-br/vly-manager/releases/tag/v0.1.0
