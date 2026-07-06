# Security Policy

## Supported Versions

| Version | Support status |
| --- | --- |
| 0.1.x | Active |
| Older versions | Not supported |

Use the newest verified build from the same release set: native binaries,
documentation, and `docs/SHA256SUMS` must all come from one promoted version.

## Security Model

DataScythe is an offline local erasure tool. It does not require accounts,
servers, telemetry, or network access during normal operation.

The security promise is explicit local overwrite of OS-addressable bytes plus
optional hardware-assisted secure erase. That promise only holds when the
operator runs a verified copy with appropriate privileges on the intended target.

## Sensitive Material

Treat these as critical:

- paths and filenames selected for shredding
- erasure certificates and exported logs
- drive serial numbers and volume layout shown in the UI
- confirmation phrases typed by the operator

DataScythe does not upload this material, but exported files remain sensitive
after the operation completes.

## Known Limitations

- SSD wear-leveling and remapped blocks may retain old data.
- HPA/DCO and firmware-resident regions are outside user-space visibility.
- NTFS/ReFS metadata, snapshots, and backups may retain copies elsewhere.
- Verification samples only a small number of sectors.
- Hardware secure erase depends on drive firmware and privilege level.

## Reporting a Vulnerability

If you believe you found a security issue, do not open a public issue with
exploit details. Report privately with:

1. affected version and platform
2. reproduction steps
3. expected vs actual behavior
4. impact assessment

Include whether the issue could cause unintended erasure, privilege bypass, or
data disclosure beyond the selected target.