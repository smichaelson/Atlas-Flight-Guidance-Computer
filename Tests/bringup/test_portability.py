"""Launch a fresh Windows clone from an unrelated CWD, with no copied venv.

All subprocesses run --check: no server, browser, COM or firmware actions.
Keeps the generated clone in the printed temp folder for troubleshooting.
"""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

@unittest.skipUnless(os.name=='nt','Windows batch-launcher integration')
class PortabilityTests(unittest.TestCase):
    def test_fresh_clone_and_non_ascii_metacharacter_paths(self):
        workspace=Path(tempfile.mkdtemp(prefix='atlas-portability-'))
        clone=workspace/'clone (offline) & Ω %ATLAS_MISSING%'
        clone.mkdir()
        shutil.copytree(ROOT/'tools/bringup',clone/'tools/bringup',ignore=shutil.ignore_patterns('__pycache__'))
        names=('Start Atlas Dashboard.cmd','Atlas Dashboard Demo.cmd','Build Atlas Firmware.cmd','Build Atlas ServoBench.cmd')
        for name in names:
            shutil.copyfile(ROOT/name,clone/name)
        self.assertFalse((clone/'.venv').exists())
        env=os.environ.copy();env['ATLAS_NONINTERACTIVE']='1'
        env.pop('PYTHONUTF8',None);env.pop('ATLAS_MISSING',None)
        for name in names:
            # Pass a raw cmd.exe command line: list2cmdline escapes quotes for
            # CRT argv, which is not cmd.exe's /c syntax. Expand the path once
            # from an environment variable so literal percent signs survive.
            # This shell only launches the batch file; it does no file operations.
            env['ATLAS_TEST_LAUNCH_FILE']=str(clone/name)
            argument='--help' if name.startswith('Build') else '--check'
            command='"'+os.environ.get('COMSPEC','cmd.exe')+'" /d /s /c ""%ATLAS_TEST_LAUNCH_FILE%" '+argument+'"'
            result=subprocess.run(command,
                                  cwd=workspace,env=env,capture_output=True,encoding='utf-8',errors='replace',timeout=90)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            if name.startswith('Build'):
                self.assertIn('build_firmware.py',result.stdout)
                self.assertIn('--servo-bench',result.stdout)
            else:
                self.assertIn(str(clone/'tools/bringup/ground_station.py'),result.stdout)
                self.assertIn('--open --port 8765',result.stdout)
                if name.startswith('Atlas Dashboard'): self.assertIn('--demo',result.stdout)
        python=clone/'.venv/Scripts/python.exe'
        result=subprocess.run([str(python),'-I','-c',
            'import sys,serial,json;print(json.dumps(dict(prefix=sys.prefix,serial=serial.VERSION)))'],
            capture_output=True,text=True,timeout=10,check=True)
        probe=json.loads(result.stdout)
        self.assertEqual(Path(probe['prefix']).resolve(),(clone/'.venv').resolve())
        self.assertEqual(probe['serial'],'3.5')
        print('PORTABILITY PASS: '+str(workspace))

if __name__=='__main__': unittest.main(verbosity=2)
