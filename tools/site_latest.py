#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""A stable name for the firmware package on the site.
   tools/site_latest.py site
The site keeps one package: firmware/anatomix-<version>.fwsc, the one of the build that made it. A
catalogue that names that file by version goes stale at the next release - the older URL is gone
(404), so the entry silently breaks. This copies the package under a name that does not carry the
version, so a catalogue can point at it and keep working:
    firmware/anatomix-latest.fwsc
The installer's own page keeps naming the versioned file, so the site's download link still shows
which version it is."""
import shutil
import sys
from pathlib import Path


def main(argv):
    if len(argv) != 2:
        print("usage: site_latest.py <site-dir>", file=sys.stderr)
        return 2
    fw = Path(argv[1]) / "firmware"
    if not fw.is_dir():
        print(f"{fw}: no firmware directory", file=sys.stderr)
        return 1
    pkgs = sorted(fw.glob("anatomix-*.fwsc"))
    if not pkgs:
        print(f"{fw}: no anatomix-*.fwsc to alias", file=sys.stderr)
        return 1
    if len(pkgs) > 1:
        print(f"{fw}: more than one package ({', '.join(p.name for p in pkgs)}); not aliasing",
              file=sys.stderr)
        return 1
    src = pkgs[0]
    dst = fw / "anatomix-latest.fwsc"
    shutil.copyfile(src, dst)
    print(f"site: {dst.name} ({src.name}, {dst.stat().st_size} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
