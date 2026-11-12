#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract the parts of the user-supplied APK that the build needs.

Usage: extract_apk.py <game.apk> <out_dir>

Produces:
  <out_dir>/lib/libAngryBirdsClassic.so   (armeabi-v7a build, input for the recompiler)
  <out_dir>/assets/...                    (game data, read at runtime)
"""
import hashlib
import sys
import zipfile
from pathlib import Path

LIB = "lib/armeabi-v7a/libAngryBirdsClassic.so"
# SHA-256 of the supported library build (Angry Birds Classic 8.0.3, build 8031).
KNOWN_LIB_SHA256 = "ce7a5179cd00fce1f6c3c1ccf56e98dd87735ea56d814d962f0dc733b22988f3"


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    apk, out = Path(sys.argv[1]), Path(sys.argv[2])
    with zipfile.ZipFile(apk) as z:
        names = z.namelist()
        if LIB not in names:
            print(f"error: {LIB} not found in APK; unsupported version?", file=sys.stderr)
            return 1
        lib_data = z.read(LIB)
        digest = hashlib.sha256(lib_data).hexdigest()
        if KNOWN_LIB_SHA256 and digest != KNOWN_LIB_SHA256:
            print(f"error: unsupported library build (sha256 {digest})", file=sys.stderr)
            return 1
        (out / "lib").mkdir(parents=True, exist_ok=True)
        (out / "lib" / "libAngryBirdsClassic.so").write_bytes(lib_data)

        for name in names:
            if not name.startswith("assets/") or name.endswith("/"):
                continue
            dest = (out / name).resolve()
            if not str(dest).startswith(str(out.resolve()) + "/"):
                continue  # reject path traversal
            dest.parent.mkdir(parents=True, exist_ok=True)
            with z.open(name) as src, open(dest, "wb") as dst:
                dst.write(src.read())
    print(f"extracted {LIB} (sha256 {digest}) and assets to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
