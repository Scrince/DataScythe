                      


from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def find_cli() -> Path | None:
    candidates = [
        ROOT / "build" / "apps" / "cli" / "datascythe.exe",
        ROOT / "build" / "apps" / "cli" / "datascythe",
        ROOT / "releases" / "native" / "windows" / "datascythe.exe",
        ROOT / "releases" / "native" / "linux" / "datascythe",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def main() -> int:
    cli = find_cli()
    if cli is None:
        print("datascythe CLI not found. Build the project first:", file=sys.stderr)
        print("  cmake -B build && cmake --build build", file=sys.stderr)
        return 1
    return subprocess.call([str(cli), *sys.argv[1:]])


if __name__ == "__main__":
    raise SystemExit(main())
