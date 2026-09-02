"""Offline bring-up artifact verification; NEVER connects to or programs an MCU.

Major functions: verify checks hashes, target/profile, vectors and HEX addresses;
hex_data validates Intel HEX checksums; main prints evidence for the operator.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct

FLASH_BASE = 0x08000000
FLASH_END = 0x08100000  # Diagnostic programming contract: bank 1 only.


def hex_data(text: str) -> dict[int, int]:
    """@brief Decode checked Intel HEX; reject overlaps/out-of-bank writes. @return Bytes."""
    output: dict[int, int] = {}
    upper = 0
    ended = False
    for line in text.splitlines():
        if not line or ended or not line.startswith(":"):
            raise ValueError("Malformed/trailing Intel HEX record")
        record = bytes.fromhex(line[1:])
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 255:
            raise ValueError("Invalid Intel HEX length/checksum")
        size, address, kind = record[0], int.from_bytes(record[1:3], "big"), record[3]
        data = record[4:-1]
        if kind == 0:
            start = upper + address
            if not size or start < FLASH_BASE or start + size > FLASH_END:
                raise ValueError("HEX writes outside diagnostic flash bank 1")
            for i, value in enumerate(data):
                if start + i in output:
                    raise ValueError("Overlapping HEX data records")
                output[start + i] = value
        elif kind == 4 and size == 2 and address == 0:
            upper = int.from_bytes(data, "big") << 16
        elif kind == 2 and size == 2 and address == 0:
            upper = int.from_bytes(data, "big") << 4
        elif kind == 5 and size == 4 and address == 0:
            entry = int.from_bytes(data, "big")
            if not FLASH_BASE <= entry < FLASH_END:
                raise ValueError("HEX entry point is outside bank 1")
        elif kind == 1 and size == 0 and address == 0:
            ended = True
        else:
            raise ValueError("Unsupported/malformed HEX record")
    if not ended or not output:
        raise ValueError("Missing HEX data/end record")
    return output


def verify(manifest_path: Path) -> dict:
    """@brief Verify local artifacts for the selected image. @return Evidence or raise.

    This is an accidental-wrong-file/corruption check, not a signed authenticity
    chain or a claim that the attached board matches the schematic.
    """
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (not isinstance(manifest, dict) or type(manifest.get("schema")) is not int or
            manifest.get("schema") != 1 or manifest.get("profile") != "bringup" or
            manifest.get("target") != "STM32H743ZIT6" or manifest.get("flash_base") != "0x08000000"):
        raise ValueError("Not the reviewed Atlas bring-up target/profile/address")
    files = {}
    for kind, expected in (("binary", "Atlas-Bringup.bin"), ("hex", "Atlas-Bringup.hex")):
        if manifest.get(kind) != expected:
            raise ValueError("Unexpected artifact name")
        files[kind] = (manifest_path.parent / expected).read_bytes()
        if hashlib.sha256(files[kind]).hexdigest() != manifest.get(kind + "_sha256"):
            raise ValueError(kind + " hash mismatch; rebuild before programming")
    elf = (manifest_path.parent / "Atlas-Bringup.elf").read_bytes()
    if not elf.startswith(b"\x7fELF") or hashlib.sha256(elf).hexdigest() != manifest.get("elf_sha256"):
        raise ValueError("ELF hash/format mismatch")
    binary = files["binary"]
    if len(binary) != manifest.get("binary_bytes") or not 8 <= len(binary) <= FLASH_END - FLASH_BASE:
        raise ValueError("Invalid binary size")
    stack, reset = struct.unpack_from("<II", binary)
    if stack != 0x20020000 or not reset & 1 or not FLASH_BASE <= reset - 1 < FLASH_BASE + len(binary):
        raise ValueError("Wrong STM32 initial stack/reset vectors")
    data = hex_data(files["hex"].decode("ascii"))
    if min(data) != FLASH_BASE or max(data) != FLASH_BASE + len(binary) - 1:
        raise ValueError("HEX/binary extents disagree")
    for address, value in data.items():
        if binary[address - FLASH_BASE] != value:
            raise ValueError("HEX and binary content disagree")
    if any(FLASH_BASE + i not in data for i in range(8)):
        raise ValueError("Incomplete HEX vector table")
    return dict(verified_offline=True, profile="bringup", target=manifest["target"],
                build_type=manifest["build_type"], program_file=str((manifest_path.parent / manifest["hex"]).resolve()),
                flash_base=manifest["flash_base"], binary_bytes=len(binary),
                hex_sha256=manifest["hex_sha256"], initial_sp=hex(stack), reset_vector=hex(reset),
                hardware_tested=False)


def main() -> None:
    """@brief Check an explicit manifest path without any programming side effects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.manifest), indent=2))
    except (OSError, ValueError, KeyError) as exc:
        parser.exit(1, "IMAGE CHECK FAILED: " + str(exc) + "\n")


if __name__ == "__main__":
    main()
