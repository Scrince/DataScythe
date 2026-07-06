# DataScythe Threat Model

## Assets

- data on selected drives, volumes, files, and alternate data streams
- operator trust in erasure completeness
- exported logs and erasure certificates

## Trust Boundaries

| Boundary | Trusted side | Untrusted side |
| --- | --- | --- |
| Application process | erase engine and operator intent | other processes on the host |
| OS storage stack | best-effort overwrite via exposed handles | hidden firmware / remapped media |
| Operator UI | typed confirmations and pre-flight results | accidental target selection |
| Release artifacts | files matching `docs/SHA256SUMS` | unsigned or mismatched binaries |

## Adversaries Considered

- operator mistake selecting the wrong drive or folder
- malware on the host observing or interfering with erase operations
- incomplete erasure due to hardware behavior outside OS visibility
- distribution of tampered release binaries

## Adversaries Not Fully Addressed

- compromised firmware or kernel-level malware
- physical recovery from specialized lab equipment
- remote copies in backups, sync services, or cloud storage
- attacks requiring network exfiltration, since the app is offline by design

## Controls

- system-drive blocking for destructive whole-device operations
- pre-flight privilege and mount checks
- typed confirmation phrases and final confirmation dialogs in the GUI
- interactive `YES` confirmation in the CLI unless `--yes` is passed
- optional post-erase verification sampling
- release hash manifest in `docs/SHA256SUMS`

## Residual Risk

Successful use of DataScythe reduces casual recovery risk for
OS-addressable data. It does not guarantee forensic impossibility or protection
against a compromised host environment.