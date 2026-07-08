# DataScythe

DataScythe is a native secure data erasure utility for physical drives, mounted
volumes, directories, and individual files. Current source builds also include
a Qt GUI drive cloning workflow for byte-for-byte physical drive copies and a
drive analytics view for health-related and device identity properties. The
project is built around C++17 core engines with command-line and Qt desktop
front ends.

DataScythe performs destructive operations. Use it only on targets that are
authorized for permanent erasure, and verify every device path before
confirming an operation.

## Release Status

Current version: `0.1.0`

Promoted native release artifacts are staged under `releases/native/` and are
covered by `docs/SHA256SUMS` plus detached PGP signatures.

| Platform | Artifact | Contents |
| --- | --- | --- |
| Windows x64 | `releases/native/windows/DataScythe-0.1.0-win64.zip` | Qt GUI, CLI, and Windows runtime dependencies |
| Linux amd64 | `releases/native/linux/DataScythe-0.1.0-linux-amd64.tar.gz` | CLI-only package |
| macOS arm64 | `releases/native/macos/DataScythe-0.1.0-macos.tar.gz` | CLI-only package for Apple Silicon |
| macOS universal | `releases/native/macos/DataScythe-0.1.0-macos-universal.tar.gz` | CLI-only package for Apple Silicon and Intel |

The Linux and macOS releases are CLI-only for v0.1.0 because Qt development
packages were not installed on the release build hosts. The Windows release
includes the GUI package.

## Verification

Before running a release artifact, verify the public key fingerprint, the
checksum manifest, and the artifact signature.

```bash
gpg --show-keys docs/DataScythe_Release_Signing_2026_pubkey.asc
gpg --import docs/DataScythe_Release_Signing_2026_pubkey.asc
gpg --verify docs/SHA256SUMS.asc docs/SHA256SUMS
python scripts/generate_sha256sums.py --check
```

Release signing key:

```text
DataScythe Release Signing (2026) <release@datascythe.local>
Fingerprint: 72E7 D550 77BA 8D7D 425B DC76 6133 2D0E 78FE F1CF
```

Example artifact verification:

```bash
gpg --verify releases/native/linux/DataScythe-0.1.0-linux-amd64.tar.gz.asc releases/native/linux/DataScythe-0.1.0-linux-amd64.tar.gz
gpg --verify releases/native/macos/DataScythe-0.1.0-macos-universal.tar.gz.asc releases/native/macos/DataScythe-0.1.0-macos-universal.tar.gz
gpg --verify releases/native/windows/DataScythe-0.1.0-win64.zip.asc releases/native/windows/DataScythe-0.1.0-win64.zip
```

PGP signatures verify release integrity. They do not replace Windows
Authenticode signing or operating-system publisher trust.

## Safety Requirements

- Confirm the physical device path, model, serial number, and size before every
  drive-level operation.
- Do not erase the system drive.
- Unmount volumes before whole-device erasure when the platform does not
  dismount them automatically.
- Keep erasure certificates and logs on controlled storage, not on the target
  being erased.
- Treat binaries, documentation, signatures, and `docs/SHA256SUMS` as one
  release set.

No software erasure tool can guarantee removal of data from SSD remapped blocks,
firmware-reserved areas, host backups, remote replicas, or a compromised
operating system.

## Capabilities

DataScythe supports:

- Physical drive enumeration
- GUI drive analytics for identity, SSD/HDD classification, geometry, TRIM,
  cache, alignment, volume, and platform health-related properties
- GUI byte-for-byte physical drive cloning with optional full verification
- SHA-256 clone evidence hashes and exportable clone reports
- Exportable GUI analytics reports
- Full-device overwrite
- Quick zero-fill
- Mounted volume shredding
- File and directory shredding
- Optional recursive directory processing
- NTFS alternate data stream shredding on Windows
- GNU shred-inspired overwrite pass scheduling
- MBR/GPT metadata wipe on block devices
- Optional sparse post-erase verification
- Erasure certificate export
- Hardware SSD secure erase paths where supported by the platform and device

## Command-Line Usage

List detected drives:

```bash
datascythe --list-drives
```

Quick zero-fill a device after explicit confirmation:

```bash
sudo datascythe --mode quick /dev/sdX
```

Run without the interactive prompt only when the target has already been
validated:

```bash
sudo datascythe --mode quick --yes /dev/sdX
```

Shred files, verify samples, and export a certificate:

```bash
datascythe --mode files --remove --verify --certificate cert.txt --yes file1.txt file2.txt
```

Shred a directory recursively:

```bash
datascythe --mode folder --recursive --yes /path/to/directory
```

Windows examples use physical drive paths such as:

```powershell
datascythe-cli.exe --list-drives
datascythe-cli.exe --mode quick --yes \\.\PhysicalDrive2
```

## Building From Source

Requirements:

- CMake 3.16 or newer
- C++17 compiler
- Qt 5.15 or Qt 6 development packages when `DATASCYTHE_BUILD_GUI=ON`
- Administrator/root privileges for raw drive operations

CLI-only build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DDATASCYTHE_BUILD_GUI=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

GUI-enabled build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

macOS universal CLI build:

```bash
cmake -B build-macos-universal \
  -DCMAKE_BUILD_TYPE=Release \
  -DDATASCYTHE_BUILD_GUI=OFF \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-macos-universal
ctest --test-dir build-macos-universal --output-on-failure
```

## Packaging

Create a CPack archive from an existing build tree:

```bash
cpack --config build/CPackConfig.cmake -B build/packages
```

Windows release staging:

```powershell
cmake --build build
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare_release_assets.ps1
python scripts/generate_sha256sums.py
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/sign_release_assets.ps1
```

Linux and macOS release archives are produced with CPack and then staged under
`releases/native/linux/` or `releases/native/macos/`.

## Project Layout

```text
apps/cli/          Command-line entry point
apps/gui/          Qt desktop application
src/core/          Erase/clone engines, certificates, scheduling, verification
src/platform/      Platform interfaces
platform/          Windows, Linux, and macOS implementations
docs/              Operator, release, and security documentation
scripts/           Release staging, hashing, and signing helpers
tests/             Unit tests and release harnesses
cmake/             Packaging and deployment helpers
releases/native/   Promoted native release artifacts
```

## Documentation

| File | Purpose |
| --- | --- |
| `docs/UserGuide.txt` | Operator workflow and UI guide |
| `docs/FileFormat.txt` | Certificate, log, and CLI output formats |
| `docs/EraseEngine.txt` | Erase engine behavior |
| `docs/PassScheduling.txt` | Overwrite pass scheduling |
| `docs/Confirmation.txt` | Confirmation requirements |
| `docs/Limitations.txt` | Technical limitations and residual risk |
| `docs/OpSec.txt` | Operational security requirements |
| `docs/TestVectors.txt` | Core logic conformance vectors |
| `docs/LinuxRelease.txt` | Linux-specific release notes |
| `docs/BuildReproducability.txt` | Reference build and verification notes |
| `docs/CodeSigning.txt` | Maintainer code-signing procedure |
| `docs/ThirdPartyNotices.txt` | Third-party notices |
| `docs/Release_v0.1.0.txt` | v0.1.0 promotion record |
| `docs/SHA256SUMS` | Release artifact checksum manifest |

## Licensing And Provenance

DataScythe is distributed under the license in `LICENSE.md`. Pass scheduling is
derived from GNU coreutils `shred.c`; see the project documentation for details
and third-party notices.
