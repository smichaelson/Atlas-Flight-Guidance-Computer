"""Real offline setup and HTTP startup via the Finder launcher; no device opens.

Runs natively on macOS, or exercises Bash + Windows Python on Windows. The
Windows run cannot validate Finder, macOS libraries/drivers or browser dispatch.
Only processes started by this test are stopped. Temporary clones are retained
for diagnostics, just like test_portability.py.
"""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request
import venv

ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = 'Start Atlas Dashboard.command'
BASH = (Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Git/bin/bash.exe'
        if os.name == 'nt' else Path('/bin/bash'))
HTTP = urllib.request.build_opener(urllib.request.ProxyHandler({}))


@unittest.skipUnless(BASH.is_file(), 'Bash is required for the Finder launcher tests')
class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.workspace = Path(tempfile.mkdtemp(prefix='atlas-launcher-'))
        # MSYS's automatic Windows argv translation cannot translate POSIX
        # paths containing apostrophes. Native macOS exercises those too.
        name = 'Atlas (offline) & Ω' if os.name == 'nt' else "Atlas (offline) & Ω ' $literal"
        self.clone = self.workspace / name
        self.clone.mkdir()
        shutil.copytree(ROOT / 'tools/bringup', self.clone / 'tools/bringup',
                        ignore=shutil.ignore_patterns('__pycache__'))
        shutil.copy2(ROOT / LAUNCHER, self.clone / LAUNCHER)
        self.python = self.clone / ('.venv/Scripts/python.exe' if os.name == 'nt' else '.venv/bin/python')
        self.env = os.environ.copy()
        self.env.update(ATLAS_NONINTERACTIVE='1', ATLAS_PYTHON=Path(sys.executable).as_posix(),
                        PIP_NO_INDEX='1', PIP_DISABLE_PIP_VERSION_CHECK='1',
                        HTTP_PROXY='http://127.0.0.1:1', HTTPS_PROXY='http://127.0.0.1:1',
                        NO_PROXY='127.0.0.1,localhost')
        # No implicit use of another interpreter's packages or environment.
        self.env.pop('PYTHONHOME', None)
        self.env.pop('PYTHONPATH', None)
        print('Launcher test clone: ' + str(self.clone), flush=True)

    def command(self, *args):
        return [str(BASH), str(self.clone / LAUNCHER), *args]

    def launch(self, *args, env=None):
        return subprocess.run(self.command(*args), cwd=self.workspace, env=env or self.env,
                              capture_output=True, encoding='utf-8', errors='replace', timeout=90)

    def checked(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def check_environment(self):
        result = subprocess.run([str(self.python), '-I', '-c',
            'import sys,serial,json; from serial.tools import list_ports; '
            'print(json.dumps(dict(prefix=sys.prefix,serial=serial.VERSION)))'],
            cwd=self.workspace, env=self.env, capture_output=True, text=True, timeout=15)
        # Exercise actual port enumeration through the application's API below.
        self.checked(result)
        data = json.loads(result.stdout)
        self.assertEqual(Path(data['prefix']).resolve(), self.python.parent.parent.resolve())
        self.assertEqual(data['serial'], '3.5')

    @contextmanager
    def server(self, demo=False):
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1', 0))
            port = reserve.getsockname()[1]
        url = f'http://127.0.0.1:{port}'
        log = self.workspace / ('demo.log' if demo else 'dashboard.log')
        with log.open('wb') as output:
            proc = subprocess.Popen(self.command('--no-browser', '--port', str(port),
                                                 *(['--demo'] if demo else [])),
                                    cwd=self.workspace, env=self.env, stdout=output, stderr=output,
                                    start_new_session=os.name != 'nt')
            try:
                deadline = time.monotonic() + 30
                while True:
                    try:
                        with HTTP.open(url + '/', timeout=1) as response:
                            page = response.read().decode('utf-8')
                        break
                    except (OSError, urllib.error.URLError):
                        if proc.poll() is not None or time.monotonic() >= deadline:
                            self.fail('Dashboard failed to start: ' + log.read_text(encoding='utf-8', errors='replace'))
                        time.sleep(.1)
                yield url, page
            finally:
                if os.name == 'nt':
                    # A launcher owns several Python children. Stop only this
                    # known test process tree, never every Python process.
                    subprocess.run(['taskkill.exe', '/PID', str(proc.pid), '/T', '/F'],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
                else:
                    try:
                        os.killpg(proc.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                proc.wait(timeout=15)

    def test_fresh_setup_real_dashboard_demo_and_session_reuse(self):
        self.assertFalse(self.python.exists())
        result = self.launch('--check')
        self.checked(result)
        self.assertIn('--open --port 8765', result.stdout)
        self.assertIn('ground_station.py', result.stdout)
        self.check_environment()
        result = self.launch('--check', '--demo', '--port', '18765')
        self.checked(result)
        self.assertIn('--demo', result.stdout)
        self.assertNotIn('First launch:', result.stdout)
        for demo in (False, True):
            with self.subTest(demo=demo), self.server(demo) as (url, page):
                token = re.search(r'name="atlas-token" content="([^"]+)"', page).group(1)
                headers = {'X-Atlas-Token': token}
                for path in ('/app.js', '/style.css', '/remote.js', '/stabilization.js', '/servos.js'):
                    with HTTP.open(url + path, timeout=3) as response:
                        self.assertEqual(response.status, 200)
                        self.assertGreater(len(response.read()), 100)
                def state():
                    with HTTP.open(urllib.request.Request(url + '/api/state', headers=headers), timeout=3) as response:
                        return json.load(response)
                snapshot = state()
                self.assertEqual(snapshot['mode'], 'demo' if demo else 'disconnected')
                self.assertEqual(snapshot['port'], '')
                self.assertIsNone(snapshot['pending'])
                if demo:
                    initial = snapshot['status']['ms']
                    deadline = time.monotonic() + 3
                    while state()['status']['ms'] <= initial and time.monotonic() < deadline:
                        time.sleep(.1)
                    self.assertGreater(state()['status']['ms'], initial)
                with HTTP.open(urllib.request.Request(url + '/api/ports', headers=headers), timeout=3) as response:
                    self.assertIsInstance(json.load(response), list)
                result = self.launch('--no-browser', '--port', url.rsplit(':', 1)[1])
                self.checked(result)
                self.assertIn('already running', result.stdout)
                self.assertEqual(state()['mode'], snapshot['mode'])

    def test_repairs_environment_without_pip(self):
        venv.EnvBuilder(with_pip=False, symlinks=False).create(self.clone / '.venv')
        result = self.launch('--check')
        self.checked(result)
        self.assertFalse(list(self.clone.glob('.venv.unusable-*')))
        self.check_environment()
        self.assertFalse((self.clone / '.atlas-setup.lock').exists())

    @unittest.skipUnless(sys.platform == 'darwin', 'Native macOS Python discovery')
    def test_python_discovery_and_finder_path(self):
        # First use the runner's ordinary python3, with no custom override.
        env = self.env.copy()
        env.pop('ATLAS_PYTHON')
        self.checked(self.launch('--check', env=env))
        # Subsequent Finder launches work without Homebrew/frameworks on PATH.
        env['PATH'] = '/usr/bin:/bin:/usr/sbin:/sbin'
        self.checked(self.launch('--check', env=env))
        self.check_environment()

    def test_preserves_unusable_environment(self):
        folder = self.clone / '.venv'
        folder.mkdir()
        (folder / 'keep.txt').write_text('A copied environment must be preserved.', encoding='utf-8')
        self.checked(self.launch('--check'))
        preserved = list(self.clone.glob('.venv.unusable-*'))
        self.assertEqual(len(preserved), 1)
        self.assertEqual((preserved[0] / 'keep.txt').read_text(encoding='utf-8'),
                         'A copied environment must be preserved.')
        self.check_environment()

    def test_damaged_dependency_refused_before_installation(self):
        # Save time and also verify refusal precedes repairing/installing pip.
        venv.EnvBuilder(with_pip=False, symlinks=False).create(self.clone / '.venv')
        wheel = self.clone / 'tools/bringup/vendor/pyserial-3.5-py2.py3-none-any.whl'
        for damaged in ('changed', 'missing'):
            with self.subTest(damaged=damaged):
                if damaged == 'changed':
                    wheel.write_bytes(b'corrupt wheel')
                else:
                    wheel.unlink()
                result = self.launch('--check')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Bundled pyserial wheel is missing or changed', result.stderr)
                self.assertTrue((self.clone / '.atlas-launch.log').exists())
                self.assertFalse((self.clone / '.atlas-setup.lock').exists())

    def test_invalid_python_and_port_fail_without_starting_server(self):
        env = dict(self.env, ATLAS_PYTHON=str(self.workspace / 'missing-python'))
        result = self.launch('--check', env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ATLAS_PYTHON must name a working Python', result.stderr)
        self.assertFalse((self.clone / '.venv').exists())
        result = self.launch('--check', '--port', '0')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Choose a port from 1024 to 65535', result.stderr)


if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8', errors='backslashreplace')
    unittest.main(verbosity=2)
