# macOS dashboard launcher review

Date: 2026-10-04. Scope: the root [Mac launcher](../Start%20Atlas%20Dashboard.command),
shared dependency bootstrap, launch options and operator instructions. Firmware,
dashboard instruments and hardware command policy are unchanged. No Atlas board
was opened or programmed during these checks.

These are three focused self-review passes, not independent reviews or evidence
of native macOS hardware testing.

## Review 1: platform and packaging

The launcher uses Bash 3.2-compatible syntax and quoted arguments, resolves its
own repository folder instead of relying on the working directory, and searches
the local environment, PATH, Apple silicon/Intel Homebrew locations and the
python.org framework. Apple's development-tools Python is skipped. A custom
`ATLAS_PYTHON` must pass a Python 3.10+/venv/ensurepip check. Failure messages stay
visible in an interactive Terminal. The file is recorded with Git executable
mode `100755`; `.gitattributes` forces LF line endings in Windows clones too.

The Python prerequisite and supported architectures were checked against
[Python's macOS documentation](https://docs.python.org/3/using/mac.html).
The launcher targets macOS, not iOS/iPadOS. Python installation is explicit;
Atlas does not silently install Homebrew, Xcode, Python or system-wide packages.

## Review 2: dependencies and failure recovery

The shared bootstrap retains the pinned pyserial 3.5 wheel and SHA-256 check,
clone-local environment, exclusive setup lock and offline pip installation.
It now repairs missing pip from Python's bundled ensurepip. Missing or modified
pyserial wheels are rejected before installation, with a log and released lock.
An unusable environment is renamed and preserved, rather than deleted.

The integration tests passed fresh setup, incomplete-environment repair,
preservation of an unusable environment, both damaged/missing wheel cases and
invalid Python/port diagnostics. They use disposable clones, blocked external
HTTP proxies and pip's no-index setting. The existing Windows portability test
also passed all four dashboard/demo/build shortcuts from a fresh clone in an
unrelated working directory with spaces, non-ASCII characters and shell
metacharacters.

## Review 3: application startup and regression

The new [launcher integration suite](../Tests/bringup/test_launcher.py) passed
five executable tests on Windows with Python 3.11.9 and Git Bash 5.2.37.
It actually starts the local server through the new shell launcher, loads the
HTML, JavaScript and styles, reads protected state, enumerates USB port names
without opening them, verifies advancing demo data and reuses the same running
session on a second launch. Both ordinary disconnected and demo startup pass.
`--check` retains browser opening in the normal launch command; `--no-browser`
allows the real HTTP checks without taking over the user's browser.

The existing Ground Station suite passed all 48 API, session and mocked updater
tests; the radio suite passed 9 tests and remote telemetry passed 10. Repository
checks passed 429 local links and 91 heading anchors, and both staged and
unstaged diffs passed whitespace checks. The new launcher tests do not connect a board, send commands, build
firmware or flash anything. Temporary test clones/logs are retained under the
system temporary directory for diagnosis; no captures or firmware are added.

## Native Mac verification still required

This workstation has no macOS runtime or Apple hardware. Git Bash plus Windows
Python validates shell orchestration and shared application startup, not
Finder/Gatekeeper, Safari/default-browser handoff, native Python framework
loading or a physical Mac USB/DFU connection. Native tests also include
apostrophes/literal dollar signs in paths and a restricted Finder-style PATH;
the Windows/MSYS path bridge cannot cover those native behaviors.

The [Dashboard launchers workflow](../.github/workflows/dashboard-launchers.yml)
is prepared for Apple silicon (`macos-15`, Python 3.14), Intel
(`macos-15-intel`, Python 3.12) and Windows (Python 3.10). It exercises fresh
offline dependency setup and real HTTP startup; Mac jobs additionally verify
executable permissions and the native `/bin/bash` launcher. These runner labels
are documented by [GitHub](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).
The workflow has not been run in this session. It can run after these changes
are pushed to GitHub; its presence is not a passing Mac test result. Even a
passing CI run will not certify physical Atlas USB operation or Finder/browser
UI behavior.
