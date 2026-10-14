#!/usr/bin/env python3
"""Windows/POSIX entry point for the existing XBE -> native C pipeline."""
import argparse
from pathlib import Path
import subprocess
import sys

from inspect_game import ROOT, InputError, inspect


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game_dir", type=Path)
    parser.add_argument("--gen-dir", type=Path, default=ROOT / "gen")
    args = parser.parse_args()
    try:
        validation = inspect(args.game_dir)
        if validation.get("kind") != "extracted_disc" or not validation["ok"]:
            raise InputError("Code generation requires a validated extracted Xbox disc directory.")
    except (OSError, InputError) as error:
        parser.error(str(error))
    game = args.game_dir.resolve()
    generated = args.gen_dir.resolve()
    if generated == game or generated.is_relative_to(game) or game.is_relative_to(generated):
        parser.error("Generated code must be separate from the input disc directory.")
    if generated.exists() and any(generated.iterdir()):
        parser.error("Generation directory is not empty; use a new --gen-dir to preserve the existing build.")
    xbe = next(p for p in game.iterdir() if p.name.lower() == "default.xbe")
    toolkit = ROOT / "xboxrecomp"
    commands = [
        ["tools.xbe_parser", str(xbe), "--json", str(xbe.with_name(xbe.stem + "_analysis.json"))],
        ["tools.disasm", str(xbe), "--seed-functions", str(ROOT / "config/seed_functions.json")],
        ["tools.func_id", str(xbe)],
        ["tools.abi_analysis", str(xbe)],
        ["tools.recomp", str(xbe), "--game-name", "NFS Underground 2", "--all", "--split", "250",
         "--gen-dir", str(generated), "--exclude-manual", str(ROOT / "src/recomp_manual.c"),
         "--mmio-sections", "DSOUND,XPP"],
    ]
    for command in commands:
        subprocess.run([sys.executable, "-m", *command], cwd=toolkit, check=True)
    if not (generated / "recomp_funcs.h").is_file():
        raise RuntimeError("Pipeline did not produce recomp_funcs.h")
    print(f"Generated C: {generated}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
