#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract the parts of user-supplied APKs that the build needs.

Usage: extract_apk.py <games_dir> <out_root> <game.apk>...

Each APK is matched to a game by its library (games/<id>/game.json) and must
be the supported build. Produces per game:
  <out_root>/<id>/lib/lib<library>.so   (armeabi-v7a build, input for the recompiler)
  <out_root>/<id>/assets/...            (game data, read at runtime)
"""
import hashlib
import json
import sys
import zipfile
from pathlib import Path


def load_games(games_dir: Path) -> dict:
    games = {}
    for f in sorted(games_dir.glob("*/game.json")):
        games[f.parent.name] = json.loads(f.read_text())
    return games


def extract(apk: Path, games: dict, out_root: Path) -> bool:
    with zipfile.ZipFile(apk) as z:
        names = set(z.namelist())
        for gid, g in games.items():
            lib = f"lib/armeabi-v7a/lib{g['library']}.so"
            if lib in names:
                break
        else:
            print(f"error: {apk}: no supported game library", file=sys.stderr)
            return False
        lib_data = z.read(lib)
        digest = hashlib.sha256(lib_data).hexdigest()
        if digest != g["sha256"]:
            print(f"error: {apk}: unsupported build of {g['title']} (library sha256 {digest}), "
                  f"needs {g['version']}", file=sys.stderr)
            return False
        out = out_root / gid
        (out / "lib").mkdir(parents=True, exist_ok=True)
        for name in sorted(names):
            if not name.startswith("assets/") or name.endswith("/"):
                continue
            dest = (out / name).resolve()
            if not str(dest).startswith(str(out.resolve()) + "/"):
                continue  # reject path traversal
            dest.parent.mkdir(parents=True, exist_ok=True)
            with z.open(name) as src, open(dest, "wb") as dst:
                dst.write(src.read())
        # Written last: its presence marks a complete extraction.
        (out / "lib" / f"lib{g['library']}.so").write_bytes(lib_data)
    print(f"{apk.name}: {g['title']} {g['version']} -> {out}")
    return True


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    games = load_games(Path(sys.argv[1]))
    out_root = Path(sys.argv[2])
    ok = True
    for apk in sys.argv[3:]:
        ok &= extract(Path(apk), games, out_root)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
