#!/usr/bin/env python3
"""Read-only nfsu2.xyz porting preflight. Does not connect, patch, or import saves.

Exit 0: requested reference inputs match (NOT multiplayer readiness).
Exit 1: a reference input differs or cannot be read. Exit 2: CLI usage error.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "config" / "nfsu2-xyz-compatibility.json"


def file_check(path, expected):
    """Hash in bounded chunks; never execute a supplied game binary."""
    try:
        with path.open("rb") as stream:
            digest = hashlib.sha256()
            size = 0
            while chunk := stream.read(1024 * 1024):
                size += len(chunk)
                digest.update(chunk)
        actual_hash = digest.hexdigest()
        ok = actual_hash == expected["sha256"]
        if "bytes" in expected:
            ok = ok and size == expected["bytes"]
        return {"ok": ok, "bytes": size, "sha256": actual_hash,
                "status": "matches" if ok else "different"}
    except OSError as error:
        return {"ok": False, "status": "unreadable", "error": str(error)}


def check_upstream(root, catalog):
    # Comparing the source content also detects dirty checkouts with the right
    # git HEAD. Normalize CRLF: a Windows checkout is the same source snapshot.
    checks = {}
    for name, expected in catalog["upstream"]["files"].items():
        try:
            data = (root / name).read_bytes().replace(b"\r\n", b"\n")
            ok = hashlib.sha256(data).hexdigest() == expected
            checks[name] = {"ok": ok, "status": "matches" if ok else "different"}
        except OSError as error:
            checks[name] = {"ok": False, "status": "unreadable", "error": str(error)}
    return {"ok": all(item["ok"] for item in checks.values()), "files": checks}


def report(catalog, pc_game=None, upstream=None):
    checks = {}
    if pc_game is not None:
        # Accept a folder or the EXE, without searching unrelated user folders.
        executable = pc_game / "SPEED2.EXE" if pc_game.is_dir() else pc_game
        checks["pc_executable"] = file_check(executable, catalog["pc_executable"])
    if upstream is not None:
        checks["upstream_source"] = check_upstream(upstream, catalog)
    return {
        "schema": 1,
        "reference_inputs_match": all(item["ok"] for item in checks.values()) if checks else None,
        "multiplayer_ready": False,
        "profile_import_ready": False,
        "preflight_writes_runtime": False,
        "targets": catalog["targets"],
        "upstream_commit": catalog["upstream"]["commit"],
        "checks": checks,
        "missing_implementation": catalog["missing_implementation"],
        "mods": catalog["mods"],
        "site_features": catalog["site_features"],
        "note": "Matching references do not establish compatibility. No server connection or save conversion is performed.",
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pc-game", type=Path, help="PC v1.2 NA game folder or SPEED2.EXE")
    parser.add_argument("--upstream", type=Path, help="Local th0mk/nfsu2-web source checkout")
    parser.add_argument("--json", action="store_true", help="Print complete machine-readable report")
    args = parser.parse_args(argv)
    try:
        catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
        result = report(catalog, args.pc_game, args.upstream)
    except (OSError, ValueError) as error:
        print(f"Cannot read compatibility catalogue: {error}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(result, ensure_ascii=True, indent=2))
    else:
        print("nfsu2.xyz reference preflight; this check does not establish multiplayer readiness.")
        print("Targets: PCVR and standalone Quest 3")
        print(f"Audited upstream: {result['upstream_commit']}")
        print(f"Catalogue: {len(result['mods'])} mods, {len(result['site_features'])} site features")
        implemented = [m["name"] for m in result["mods"] if m["implemented_by_this_change"]]
        if implemented:
            print("Native mod implementations in this change: " + ", ".join(implemented))
        if not result["checks"]:
            print("No reference inputs checked. Supply --pc-game and/or --upstream.")
        for name, check in result["checks"].items():
            print(f"{name}: {'MATCH' if check['ok'] else 'MISMATCH / UNAVAILABLE'}")
            if "error" in check:
                print(f"  {check['error']}")
            for file, item in check.get("files", {}).items():
                if not item["ok"]:
                    print(f"  {file}: {item['status']}")
        print("PC/Xbox saves cannot be imported interchangeably.")
        print("Still needed:")
        for item in result["missing_implementation"]:
            print(f"  - {item}")
    return 1 if result["reference_inputs_match"] is False else 0


if __name__ == "__main__":
    raise SystemExit(main())
