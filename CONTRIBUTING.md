# Contributing to DataScythe

DataScythe performs destructive data erasure. Contributions must preserve
safety checks, explicit confirmations, offline execution, and platform parity
where practical.

## Project Boundaries

Accepted work should stay within:

- secure overwrite of OS-addressable bytes on drives, volumes, and files
- hardware-assisted SSD secure erase where the platform exposes it
- pre-flight validation and operator confirmations
- logging, certificates, and release verification tooling
- documentation, test vectors, and cross-platform backends

Do not add telemetry, cloud sync, automatic updates, remote wipe triggers, or
network-required runtime behavior.

## Source of Truth

The canonical v0.1.0 implementation is the native C++ core in `src/` plus the
application surfaces in `apps/`. Technical behavior is documented in `docs/`.
Release artifacts checked in under `releases/native/` must match `docs/SHA256SUMS`.

## Development Rules

- Treat erase-engine, raw-device, and secure-erase changes as safety-sensitive.
- Keep destructive operations behind pre-flight checks and confirmations.
- Update `docs/` and `CHANGELOG.md` when operator-visible behavior changes.
- Avoid unrelated refactors in erase and platform code.
- Do not log file contents, sector samples, or certificate secrets beyond
  operator-selected export paths.

## Local Setup

```powershell
cd T:\DataScythe
$env:PATH = "C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\mingw1310_64\bin;" + $env:PATH
cmake -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:\Qt\6.9.0\mingw_64" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Required Tests by Surface

| Surface touched | Required command |
| --- | --- |
| `src/core/*` | `ctest --test-dir build --output-on-failure` |
| `apps/cli/*` | unit tests + `datascythe --version` + `datascythe --help` |
| `apps/gui/*` | unit tests + manual smoke on elevated Windows host |
| `platform/*` | unit tests + platform release harness when preparing a release |
| `docs/*`, `scripts/*`, release layout | `python scripts/generate_sha256sums.py --check` when artifacts exist |

## Release Promotion Checklist

1. Bump version in `CMakeLists.txt`, `src/core/version.h`, `package.json`, and `CHANGELOG.md`.
2. Build release artifacts for each target platform.
3. Copy binaries into `releases/native/{windows,linux,macos}/`.
4. Regenerate `docs/SHA256SUMS`.
5. Sign artifacts and manifest: `npm run release:sign` (uses `.gnupg-release/`).
6. Optional Authenticode signing: `docs/CodeSigning.txt` and `scripts/sign_code.ps1`.
6. Update `docs/Release_vX.Y.Z.txt` and `README.md`.
7. Archive superseded artifacts under `lts/` when promoting a new default build.