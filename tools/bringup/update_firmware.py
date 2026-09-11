"""Verified, explicit STM32H743 bring-up updates through factory USB DFU.

Major functions: prepare freezes/hash-checks an image; enter_dfu binds a command
to a fresh CDC UID; program verifies the same UID in ROM before writing bank 1.
No option-byte changes, mass erase, automatic retries, or normal-profile flash.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
from image_check import verify
from protocol import Decoder, Session

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = ROOT / "build/Bringup/Atlas-Bringup.manifest.json"

def find_manifest(profile='bringup') -> Path:
    """Resolve known build locations within THIS clone, never another user's path."""
    if profile not in ('bringup','servo_bench'):
        raise ValueError('Choose Bringup or ServoBench')
    basename='Atlas-ServoBench' if profile=='servo_bench' else 'Atlas-Bringup'
    folders=('ServoBenchMake','ServoBench') if profile=='servo_bench' else ('BenchMake','Bringup','BringupRelease')
    for folder in folders:
        candidate=ROOT/'build'/folder/(basename+'.manifest.json')
        if candidate.is_file(): return candidate
    raise ValueError('Build this profile first using '+('Build Atlas ServoBench.cmd' if profile=='servo_bench' else 'Build Atlas Firmware.cmd'))


def programmer_path() -> Path:
    """Find an installed ST tool without downloading or changing drivers."""
    candidates = [shutil.which("STM32_Programmer_CLI")]
    import os
    candidates += [Path(os.environ.get("ProgramFiles", "C:/Program Files")) /
                   "STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"]
    candidates += list((Path(os.environ.get("LOCALAPPDATA", ".")) /
                        "stm32cube/bundles/programmer").glob("*/bin/STM32_Programmer_CLI.exe"))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    raise ValueError("Install STM32CubeProgrammer, including its USB DFU driver.")


def run_cli(cli: Path, args: list[str], timeout: int = 30) -> str:
    """Execute bounded argv, never shell text; treat reported errors as failures."""
    completed = subprocess.run([str(cli), *args], capture_output=True, text=True,
                               errors="replace", timeout=timeout,
                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    output = completed.stdout + completed.stderr
    if completed.returncode or re.search(r"(?im)^\s*Error\s*:", output):
        raise RuntimeError(output[-5000:] or "STM32CubeProgrammer failed")
    return output


def parse_uid(output: str) -> list[int]:
    """Read exactly the 96-bit UID from CubeProgrammer's address-labelled dump."""
    match = re.search(r"(?im)^\s*(?:0x)?1FF1E800\s*:\s*(?:0x)?([\dA-F]{8})\s+"
                      r"(?:0x)?([\dA-F]{8})\s+(?:0x)?([\dA-F]{8})\b", output)
    if not match:
        raise ValueError("Cannot verify target UID from ROM; programming refused.")
    return [int(v, 16) for v in match.groups()]


def prepare(manifest: Path) -> tuple[dict, Path]:
    """Freeze the chosen artifacts into a unique staging directory and validate."""
    evidence = verify(manifest)
    staging_root = ROOT / "build/update-staging"
    staging_root.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix="image-", dir=staging_root))
    basename = "Atlas-ServoBench" if evidence["profile"] == "servo_bench" else "Atlas-Bringup"
    for suffix in (".manifest.json", ".elf", ".bin", ".hex"):
        name = basename + suffix
        shutil.copyfile(manifest if suffix == ".manifest.json" else manifest.parent / name, folder / name)
    frozen = folder / (basename + ".manifest.json")
    result = verify(frozen)
    if result["hex_sha256"] != evidence["hex_sha256"]:
        raise ValueError("Image changed during preparation; recheck before updating.")
    return result, frozen


def enter_dfu(port: str, uid: list[int], report=print) -> None:
    """Own a CDC session until the device resets itself; no close-on-ACK race."""
    import serial
    session, decoder = Session(), Decoder()
    device = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=1)
    device.dtr, device.rts = False, False
    device.port = port
    with device:
        # Ensure the console observes a connection boundary even when a GUI
        # surrendered this port only milliseconds ago. Never replay old bytes.
        time.sleep(0.1)
        device.reset_input_buffer()
        device.dtr = True
        deadline = time.monotonic() + 8
        while not (session.hello and session.fresh(time.monotonic())):
            if time.monotonic() > deadline:
                raise TimeoutError("No fresh Atlas bring-up handshake; initial installation needs factory DFU.")
            for frame in decoder.feed(device.read(min(8192, device.in_waiting or 1))):
                session.accept(frame, time.monotonic())
            if decoder.errors:
                raise ValueError("Malformed telemetry during update handshake; request refused.")
        if session.hello["uid"] != uid:
            raise ValueError("CDC device identity changed; update refused.")
        verb = "bootloader " + " ".join(str(v) for v in uid)
        packet = session.request(verb, time.monotonic(), True)
        if device.write(packet) != len(packet):
            raise IOError("Short DFU request write; outcome unknown. Do not retry blindly.")
        report("DFU request sent. Waiting for the board's acknowledgement and reset…")
        deadline = time.monotonic() + 6
        accepted = False
        while time.monotonic() < deadline:
            try:
                # A Windows overlapped read waiting for 8192 bytes can lose an
                # already queued short ACK when the MCU resets before timeout.
                # Drain available bytes immediately; wait for only one if empty.
                data = device.read(min(8192, device.in_waiting or 1))
            except serial.SerialException:
                if accepted:
                    return
                raise IOError("USB disappeared before a verified DFU acknowledgement.")
            for frame in decoder.feed(data):
                session.accept(frame, time.monotonic())
                if session.last_reply:
                    if session.last_reply["status"]:
                        raise ValueError(session.last_reply["detail"])
                    accepted = True
            if decoder.errors or session.blocked:
                raise ValueError('Invalid telemetry during DFU acknowledgement; no programming attempted.')
            # Remain open after ACK: DTR loss before reset cancels firmware entry.
        if not accepted:
            raise TimeoutError("DFU request was not acknowledged; no programming attempted.")
        # Some Windows drivers do not signal removal until a later read. The
        # independent ROM enumeration/UID check still gates every flash write.


def program(manifest: Path, uid: list[int], dfu_port: str = "USB1", report=print,
            start_application: bool = True) -> dict:
    """Program a single enumerated, UID-matched H743; verify before start."""
    if not re.fullmatch(r"USB[1-9][0-9]*", dfu_port):
        raise ValueError("Select an explicit USB DFU port such as USB1.")
    cli = programmer_path()
    deadline = time.monotonic() + 25
    while True:
        output = run_cli(cli, ["-l", "usb"])
        ports = re.findall(r"(?im)^\s*(?:USB Port|Device Index)\s*:\s*(USB\d+)\s*$", output)
        if ports:
            if ports != [dfu_port]:
                raise ValueError("DFU selection is ambiguous or changed; disconnect other STM32 DFU devices.")
            break
        if time.monotonic() >= deadline:
            raise TimeoutError("Factory DFU did not enumerate. Check USB/driver; no flash write occurred.")
        time.sleep(0.5)
    report("Factory DFU detected. Verifying the physical MCU identity…")
    serials = re.findall(r"(?im)^\s*Serial number\s*:\s*([A-Za-z0-9]{6,64})\s*$", output)
    if len(serials) != 1:
        raise ValueError("Cannot bind the DFU serial number; programming refused.")
    connection = ["-c", f"port={dfu_port}", f"sn={serials[0]}"]
    # Read sizes are BYTES, including -r32. Pin every later CLI connection to
    # the same serial number so USB1 reassignment cannot select another board.
    identity = run_cli(cli, [*connection, "-r32", "0x1FF1E800", "12", "-r8", "0x1FF1E7FE", "1"])
    if not re.search(r"(?i)Device ID\s*:\s*0x0?450\b", identity) or parse_uid(identity) != uid:
        raise ValueError("DFU MCU/UID does not match Atlas. No flash write occurred.")
    evidence = verify(manifest)  # Recheck the frozen bytes immediately before write.
    report("Identity matched. Writing the selected image and verifying flash…")
    written = run_cli(cli, [*connection, "-d", evidence["program_file"], "-v"], timeout=180)
    if not re.search(r"(?i)(verification.*(success|OK)|verified successfully)", written):
        raise RuntimeError("Programmer did not confirm verification. Board stays in DFU.\n" + written[-4000:])
    revision = re.search(r"(?im)^\s*(?:0x)?1FF1E7FE\s*:\s*(?:0x)?([\dA-F]{2})\b", identity)
    rom_version = int(revision[1],16) if revision else None
    warm_start = start_application and rom_version in (0x91,0x92)
    started = ""
    if warm_start:
        report("Flash verified. Starting the application…")
        started = run_cli(cli, [*connection, "-s", "0x08000000"])
    else:
        report("Flash verified. Complete this installation with a battery power cycle and BOOT0 low.")
    return dict(evidence, uid=uid, dfu_serial=serials[0], rom_version=rom_version,
                flash_verified=True, application_start_requested=warm_start,
                power_cycle_required=not warm_start,
                application_reconnected=False, programmer_log=identity + written + started)


def main() -> None:
    """Default to offline verification; live writes require explicit bench arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--port", help="Atlas CDC port; omitted for an initial factory-DFU install")
    parser.add_argument("--dfu", default="USB1")
    parser.add_argument("--uid", help="Three comma-separated UID words (decimal or 0x-prefixed)")
    parser.add_argument("--confirm-bench", action="store_true")
    args = parser.parse_args()
    try:
        if not args.confirm_bench:
            print(json.dumps(verify(args.manifest), indent=2))
            return
        if not args.uid:
            raise ValueError("An explicitly selected MCU UID is required before programming.")
        uid = [int(word, 0) for word in args.uid.split(",")]
        if len(uid) != 3 or any(not 0 <= word <= 0xFFFFFFFF for word in uid):
            raise ValueError("UID must contain three unsigned 32-bit words.")
        _, frozen = prepare(args.manifest)
        if args.port:
            enter_dfu(args.port, uid)
        # An initial BOOT0 installation finishes with the documented cold boot.
        result = program(frozen, uid, args.dfu, start_application=bool(args.port))
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        parser.exit(1, f"UPDATE STOPPED: {exc}\n")


if __name__ == "__main__":
    main()
