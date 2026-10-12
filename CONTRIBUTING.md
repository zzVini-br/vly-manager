# Contributing to vly-manager

Thanks for your interest! Bug reports, ideas and pull requests are all welcome.

## Before you start

- **Bugs:** open an issue with your distribution, kernel version (`uname -r`), what you
  ran, what happened and what you expected. Output of `vly list` around the problem
  helps a lot.
- **Features or larger changes:** open an issue first to discuss the idea, so your
  time goes into something that fits where the project is heading (see the roadmap in
  the [README](README.md#roadmap)).
- **Small fixes** (typos, obvious bugs): a pull request on its own is fine.

## Building and testing

On Debian or Ubuntu:

```sh
sudo apt install build-essential cmake pkg-config libncurses-dev
cmake -B build
cmake --build build
ctest --test-dir build
```

Before sending a pull request, also build with warnings as errors and the sanitizers,
and make sure every test still passes:

```sh
cmake -B build-check -DCMAKE_BUILD_TYPE=Debug -DVLY_WERROR=ON -DVLY_SANITIZE=ON
cmake --build build-check
ctest --test-dir build-check
```

Some tests start and stop real child processes; they never touch anything else on your
system.

## Code

- C11, formatted with the repository's `.clang-format`; match the style of the
  surrounding code.
- `core/` (libvly) reads `/proc` and sends signals; it never contains UI code.
- In `app/tui/`, logic lives in curses-free modules (such as `tree_view` and
  `quickfind`) that are unit-tested; the curses code only draws.
- Safety rules that every change keeps:
  - signals go through the `vly_kill*` / `vly_signal_process` functions, which verify a
    process's identity with a pidfd before signalling it; never call `kill()` on a pid
    directly;
  - process names are untrusted: print them through `fmt_safe_name()`;
  - walks over the process tree are bounded, since pid reuse can create cycles in a
    snapshot.

## Tests

- New behavior comes with tests; a bug fix comes with a test that fails without it.
- Use the fake `/proc` in `tests/fixtures/proc/` for deterministic cases, and real
  child processes when the kernel's behavior is what matters.

## Commits and pull requests

- Work on a branch (`feat/...`, `fix/...`) and open the pull request against `main`.
- Keep commits atomic: one logical change each, and every commit builds and passes
  the tests on its own.
- Commit messages are in English: a short imperative subject ("Add ...", "Fix ..."),
  then a body explaining why the change is needed.
- Don't edit `CHANGELOG.md` in your pull request; describe user-visible changes in the
  pull request description and they are added to the changelog at release time.

## License

By contributing, you agree that your contributions are licensed under the
[GPL-3.0-or-later](LICENSE), like the rest of the project.
