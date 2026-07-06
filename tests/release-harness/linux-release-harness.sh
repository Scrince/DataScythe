#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="0.1.0"

echo "[RUN] DataScythe Linux release preflight"

test -f "$ROOT/CMakeLists.txt"
test -f "$ROOT/src/core/version.h"

if [[ -x "$ROOT/build/apps/cli/datascythe" ]]; then
  "$ROOT/build/apps/cli/datascythe" --version
fi

if [[ "${1:-}" == "--validate-artifacts" ]]; then
  test -f "$ROOT/releases/native/linux/DataScythe-${VERSION}-linux-amd64.tar.gz" \
    || echo "[WARN] No staged Linux archive found"
fi

echo "[PASS] Linux release preflight complete"