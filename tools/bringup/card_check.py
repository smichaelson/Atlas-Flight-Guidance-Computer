"""Explicit laptop-side SD fixtures; no format, overwrite, delete or MCU access.

Major functions: prepare exclusively creates ATLAS.TXT; verify reads the MCU's
ATLASCHK.TST after unmount/power-off and compares every byte independently.
"""
import argparse
from pathlib import Path

READ_TEXT = b"ATLAS SD READ TEST v1\r\n"
TEST_NAME = "ATLASCHK.TST"


def prepare(directory: Path) -> Path:
    """@brief Create one exact ASCII/CRLF fixture. @return New path; existing is refused."""
    if not directory.is_dir():
        raise ValueError("Select an existing mounted test-card directory")
    target = directory / "ATLAS.TXT"
    with target.open("xb") as file:
        file.write(READ_TEXT)
    return target


def verify(directory: Path) -> int:
    """@brief Independently check the MCU's 1024-byte file. @return Verified byte count."""
    with (directory / TEST_NAME).open("rb") as file:
        data = file.read(1025)  # Bounded even if an unrelated/huge file was selected.
    expected = bytes((i * 37 + (i >> 8) * 13 + 0xA5) & 255 for i in range(1024))
    if data != expected:
        raise ValueError("Test file is missing, truncated, longer than 1024 bytes, or corrupted")
    return len(data)


def main() -> None:
    """@brief Operate on one explicit laptop-mounted card; no device discovery."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("prepare", "verify"))
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    try:
        if args.operation == "prepare":
            print("Created (no overwrite):", prepare(args.directory))
        else:
            print("Independent laptop comparison:", verify(args.directory), "bytes matched")
    except (OSError, ValueError) as exc:
        parser.exit(1, str(exc) + "\n")


if __name__ == "__main__":
    main()
