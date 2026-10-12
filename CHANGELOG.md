# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.1] - 2026-10-11

Bug-fix release.

### Security

- Process names are printed with control characters replaced by `?`. Any local user
  can name a process, and escape sequences in a name could clear the screen or
  retitle the window of whoever ran `vly list` or `vly kill`, root included.

### Fixed

- `vly kill` options are recognized after the targets. In `vly kill steam -t 600`,
  `600` was taken as a pid to stop, which under `sudo` could hit an unrelated system
  process. Unknown options are now rejected, and `--` ends option parsing.
- `kill` and `kill-tree` no longer report processes as "already exited" when they
  could not be checked because of a system error such as too many open files; those
  processes were left running while vly exited with status 0. They are now reported,
  and the exit status is 2.
- vly raises its open-file limit at startup, so `kill-tree` handles trees larger than
  the common default limit of 1024 processes.
- The `USER` column shows the real owner of non-dumpable processes (such as
  `ssh-agent` or polkit agents) instead of `root`.
- `kill` and `kill-tree` cannot hang on a parent cycle caused by pid reuse while
  reading `/proc`.
- A memory leak when `kill` fails to read `/proc`.

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

[Unreleased]: https://github.com/zzVini-br/vly-manager/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/zzVini-br/vly-manager/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/zzVini-br/vly-manager/releases/tag/v0.1.0
