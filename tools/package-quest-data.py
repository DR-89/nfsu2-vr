#!/usr/bin/env python3
"""Package a user's extracted NTSC-U Xbox disc for the on-headset ZIP importer."""
import argparse
import hashlib
from pathlib import Path
import re
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="Extracted disc folder containing default.xbe and NFSUNDER")
    parser.add_argument("output", type=Path, help="New ZIP file; existing files are never overwritten")
    args = parser.parse_args()
    manifest = Path(__file__).resolve().parents[1] / "android/app/src/main/java/org/nfsu2/quest/GameManifest.java"
    files = re.findall(r'Expected\("([^"]+)", (\d+)L, "([0-9a-f]{64})"\)', manifest.read_text(encoding="utf-8"))
    if len(files) != 12:
        parser.error("The supported game manifest must contain exactly 12 files")
    if args.output.exists():
        parser.error("Output already exists; choose a new filename")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation ensures this tool never removes or replaces an existing user archive.
    with args.output.open("xb") as output:
        try:
            with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED, allowZip64=True) as archive:
                for name, size, expected in files:
                    source = args.game / name
                    if not source.is_file() or source.stat().st_size != int(size):
                        raise ValueError(f"Missing or incompatible Xbox-USA file: {name}")
                    digest = hashlib.sha256()
                    with source.open("rb") as incoming, archive.open(name, "w", force_zip64=True) as outgoing:
                        while chunk := incoming.read(1024 * 1024):
                            digest.update(chunk)
                            outgoing.write(chunk)
                    if digest.hexdigest() != expected:
                        raise ValueError(f"SHA-256 mismatch; modified, damaged or unsupported Xbox-USA file: {name}")
                    print(f"Verified and packed: {name}", flush=True)
        except BaseException:
            output.close()
            args.output.unlink()
            raise
    print(f"Ready: {args.output.resolve()} ({args.output.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()
