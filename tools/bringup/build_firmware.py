"""Build and verify inhibited Atlas firmware; never opens a device or programs it."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from image_check import verify

ROOT=Path(__file__).resolve().parents[2]

def main():
    env=os.environ.copy()
    compiler=shutil.which("arm-none-eabi-gcc")
    if not compiler:
        bundles=Path(env.get("LOCALAPPDATA",""))/"stm32cube/bundles/gnu-tools-for-stm32"
        candidates=sorted(bundles.glob("*/bin/arm-none-eabi-gcc.exe"),reverse=True)
        if candidates: compiler=str(candidates[0])
    if not compiler: raise RuntimeError("Install the STM32 Arm GNU compiler or add it to PATH.")
    env["PATH"]=str(Path(compiler).parent)+os.pathsep+env.get("PATH","")
    cmake,make=shutil.which("cmake"),shutil.which("gmake")
    if not cmake or not make: raise RuntimeError("This Windows build needs CMake and GNU Make (gmake).")
    subprocess.run([cmake,"-S",".","-B","build/BenchMake","-G","MinGW Makefiles",
        "-DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake","-DCMAKE_BUILD_TYPE=Debug",
        "-DATLAS_BRINGUP=ON","-DCMAKE_MAKE_PROGRAM="+make.replace("\\","/")],cwd=ROOT,env=env,check=True)
    subprocess.run([cmake,"--build","build/BenchMake","--parallel","4"],cwd=ROOT,env=env,check=True)
    print(json.dumps(verify(ROOT/"build/BenchMake/Atlas-Bringup.manifest.json"),indent=2))

if __name__=="__main__":
    try: main()
    except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as exc:
        raise SystemExit("BUILD STOPPED: "+str(exc))
