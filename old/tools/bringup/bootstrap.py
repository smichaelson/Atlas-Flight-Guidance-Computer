"""Create a clone-local Python environment using the bundled, hash-pinned wheel.

No downloads, hardware access, global package installs or fixed user paths.
The dashboard needs Python 3.10+; firmware building additionally needs ST tools.
"""
from __future__ import annotations
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import traceback
import venv

ROOT = Path(__file__).resolve().parents[2]
WHEEL_NAME = 'pyserial-3.5-py2.py3-none-any.whl'
WHEEL_SHA256 = 'c4451db6ba391ca6ca299fb3ec7bae67a5c55dde170964c7a14ceefec02f2cf0'

def probe(python: Path, dependencies=False) -> bool:
    code = 'import sys; assert sys.version_info >= (3,10)'
    if dependencies:
        code += '; import serial, serial.tools.list_ports; assert serial.VERSION == "3.5"'
    try:
        return subprocess.run([str(python), '-I', '-c', code], cwd=ROOT,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                              timeout=15).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False

def environment() -> Path:
    folder = ROOT / '.venv'
    if folder.resolve().parent != ROOT.resolve():
        raise RuntimeError('This clone\'s .venv resolves outside the clone. Restore a local environment before launching.')
    python = folder / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')
    if probe(python, dependencies=True):
        return python
    # An exclusive setup lock prevents two double-clicks racing venv/pip writes.
    lock = ROOT / '.atlas-setup.lock'
    try:
        handle = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        raise RuntimeError('Atlas setup is already running. If a previous setup was interrupted, '
                           'close its window and remove .atlas-setup.lock from this clone.')
    os.close(handle)
    try:
        if not probe(python):
            if folder.exists():
                preserved = ROOT / ('.venv.unusable-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
                folder.rename(preserved)
                print('Preserved the unusable environment as ' + preserved.name, flush=True)
            print('First launch: creating this clone\'s Python environment...', flush=True)
            venv.EnvBuilder(with_pip=True, symlinks=False).create(folder)
        wheel = Path(__file__).parent / 'vendor' / WHEEL_NAME
        if not wheel.is_file() or hashlib.sha256(wheel.read_bytes()).hexdigest() != WHEEL_SHA256:
            raise RuntimeError('Bundled pyserial wheel is missing or changed. Restore tools/bringup/vendor from the repository.')
        subprocess.run([str(python), '-I', '-m', 'pip', '--isolated', 'install', '--no-index',
                        '--no-deps', '--disable-pip-version-check', str(wheel)], cwd=ROOT, check=True)
        if not probe(python, dependencies=True):
            raise RuntimeError('The local environment did not pass its Python/pyserial check.')
        return python
    finally:
        lock.unlink()

def main():
    os.environ['PYTHONUTF8']='1'
    if sys.version_info < (3, 10):
        raise RuntimeError('Install Python 3.10 or newer from https://www.python.org/downloads/windows/ and run the launcher again.')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('dashboard', 'demo', 'build', 'servo-build', 'setup'))
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    python = environment()
    if args.mode == 'setup':
        print(json.dumps(dict(ready=True, root=str(ROOT), python=str(python), offline_setup=True), indent=2))
        return 0
    if args.mode in ('build', 'servo-build'):
        command = [str(python), '-B', str(Path(__file__).with_name('build_firmware.py'))]
        if args.mode == 'servo-build': command.append('--servo-bench')
    else:
        command = [str(python), '-B', str(Path(__file__).with_name('launch_ground_station.py'))]
        if args.mode == 'demo': command.append('--demo')
    return subprocess.call(command + args.arguments, cwd=ROOT)

if __name__ == '__main__':
    for stream in (sys.stdout,sys.stderr):
        if hasattr(stream,'reconfigure'): stream.reconfigure(encoding='utf-8',errors='backslashreplace')
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        log = ROOT / '.atlas-launch.log'
        try:
            log.write_text(traceback.format_exc(), encoding='utf-8')
        except OSError:
            pass
        print('ATLAS LAUNCH STOPPED: ' + str(exc), file=sys.stderr)
        print('Details: ' + str(log), file=sys.stderr)
        raise SystemExit(1)
