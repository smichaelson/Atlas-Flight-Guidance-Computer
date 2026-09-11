"""Open one local Ground Station using the existing Python environment."""
import argparse
import hashlib
import re
from pathlib import Path
import subprocess
import sys
import urllib.error
import urllib.request
import webbrowser

ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo",action="store_true")
    parser.add_argument("--port",type=int,default=8765)
    parser.add_argument("--check",action="store_true",help="Print the launch command without opening a browser or server")
    args=parser.parse_args()
    if not 1024<=args.port<=65535: parser.error("Choose a port from 1024 to 65535")
    manifest=ROOT/"build/BenchMake/Atlas-Bringup.manifest.json"
    if not manifest.exists(): manifest=ROOT/"build/Bringup/Atlas-Bringup.manifest.json"
    command=[sys.executable,"-B",str(ROOT/"tools/bringup/ground_station.py"),"--open",
             "--port",str(args.port),"--manifest",str(manifest)]
    if args.demo: command.append("--demo")
    if args.check:
        print(subprocess.list2cmdline(command))
        return
    url=f"http://127.0.0.1:{args.port}/"
    try:
        with urllib.request.urlopen(url,timeout=2) as response:
            page=response.read(200000)
            existing=b'<meta name="atlas-token"' in page
    except (OSError,urllib.error.URLError):
        existing=False
    if existing:
        root_id=hashlib.sha256(str(ROOT).casefold().encode('utf-8')).hexdigest()[:24]
        match=re.search(rb'name="atlas-root-id" content="([a-f0-9]+)"',page)
        if not match or match.group(1).decode()!=root_id:
            raise SystemExit('A different or older Atlas server is using this port. Close its launcher, '
                             'or run Start Atlas Dashboard.cmd --port 8766 from this clone.')
        print("Atlas is already running. Opening its current session; use Explore demo if needed.")
        webbrowser.open(url)
        return
    raise SystemExit(subprocess.call(command,cwd=ROOT))

if __name__=="__main__": main()
