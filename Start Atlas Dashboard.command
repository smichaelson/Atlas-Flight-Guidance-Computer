#!/bin/bash
# Finder/Terminal launcher for macOS (Apple silicon and Intel); Bash 3.2 compatible.
# Keep this file beside tools/bringup. Python packages are installed offline.
set -u

atlas_finish() {
    local atlas_status="$?"
    if [ "$atlas_status" -ne 0 ] && [ -t 0 ] && [ -z "${ATLAS_NONINTERACTIVE:-}" ]; then
        printf '\nAtlas could not start. Read the message above. Press Return to close. '
        read -r _atlas_reply
    fi
    exit "$atlas_status"
}
trap atlas_finish EXIT

atlas_root="$(CDPATH= cd -- "$(dirname "$0")" && pwd -P)" || exit 1
if [ ! -f "$atlas_root/tools/bringup/bootstrap.py" ]; then
    printf '%s\n' 'Keep this launcher inside the complete Atlas repository folder.' >&2
    exit 1
fi
cd -- "$atlas_root" || exit 1
export PYTHONUTF8=1
unset PYTHONHOME PYTHONPATH

atlas_usable_python() {
    [ -n "$1" ] && [ -x "$1" ] || return 1
    # Do not invoke Apple's development-tools stub or trigger an Xcode install.
    case "$1" in /usr/bin/python|/usr/bin/python3) return 1 ;; esac
    "$1" -I -c 'import sys, venv, ensurepip; sys.exit(sys.version_info < (3, 10))' >/dev/null 2>&1
}

atlas_python=''
if [ -n "${ATLAS_PYTHON:-}" ]; then
    if atlas_usable_python "$ATLAS_PYTHON"; then
        atlas_python="$ATLAS_PYTHON"
    else
        printf '%s\n' 'ATLAS_PYTHON must name a working Python 3.10+ executable with venv and ensurepip.' >&2
        exit 1
    fi
else
    # Finder may have a shorter PATH than an interactive Terminal session.
    for atlas_candidate in \
        "$atlas_root/.venv/bin/python" \
        "$(command -v python3 || true)" \
        /opt/homebrew/bin/python3 \
        /usr/local/bin/python3 \
        /Library/Frameworks/Python.framework/Versions/Current/bin/python3
    do
        if atlas_usable_python "$atlas_candidate"; then
            atlas_python="$atlas_candidate"
            break
        fi
    done
fi

if [ -z "$atlas_python" ]; then
    printf '%s\n' \
        'Atlas needs Python 3.10 or newer on this Mac.' \
        'Install the macOS Python package from https://www.python.org/downloads/macos/' \
        'Then double-click Start Atlas Dashboard.command again.' \
        'Atlas will install its bundled dashboard dependency automatically, without downloads.' >&2
    exit 1
fi

printf '%s\n' 'Starting Atlas. Keep this Terminal window open; press Control-C to stop.'
"$atlas_python" -B "$atlas_root/tools/bringup/bootstrap.py" dashboard "$@"
exit "$?"
