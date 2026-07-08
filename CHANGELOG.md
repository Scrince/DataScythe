# Changelog - DataScythe

All notable user-facing changes are documented here. The source of truth for
v0.1.0 is the native binaries under `releases/native/` plus the technical
references in `docs/`.

## [Unreleased]

### Added

- Qt GUI drive cloning tab for byte-for-byte physical drive copies with optional
  full byte comparison verification.
- Core `DriveCloneEngine` using platform raw-device I/O, target dismount, size
  checks, cancellation, progress reporting, and in-memory unit coverage.
- Qt GUI Analytics tab for selected-drive identity, SSD/HDD classification,
  geometry, cache, TRIM/discard, alignment, volume, and platform health-related
  properties.
- SHA-256 hashing during drive clone operations, plus GUI export for clone
  evidence reports and analytics reports.

## [0.1.0] - 2026-07-05

### Added

- Qt GUI and `datascythe` CLI for secure drive, volume, and file erasure.
- GNU shred-inspired pass scheduling with random passes and final zero pass.
- SSD hardware secure erase via NVMe Sanitize and ATA SECURITY ERASE.
- Pre-flight checks, typed confirmations, and system-drive blocking.
- Post-erase verification, NTFS ADS shredding, partition metadata wipe.
- Erasure certificates with GUI auto-save and CLI `--certificate`.
- Cross-platform platform backends for Windows, Linux, and macOS.
- Repository layout aligned with YellowSphere release discipline:
  `releases/native/`, `docs/SHA256SUMS`, release harnesses, and policy docs.

### Changed

- Moved application entry points to `apps/cli/` and `apps/gui/`.
- Renamed CLI binary from `DataScytheCLI` to `datascythe`.
- Moved reference `shred.c` to `docs/reference/shred.c`.
- Added PGP release signing with `.gnupg-release/` key home and detached `*.asc` signatures.
- Published `docs/DataScythe_Release_Signing_2026_pubkey.asc` for release verification.
- Added local Authenticode certificate `docs/DataScythe_Local_Code_Signing_2026.cer`
  with private exports under `.gnupg-release/private-keys-v1.d/`.
- Regenerated PGP release signing as a single primary key (YellowSphere-style);
  re-signed all release artifacts with fingerprint
  `72E7 D550 77BA 8D7D 425B DC76 6133 2D0E 78FE F1CF`.
