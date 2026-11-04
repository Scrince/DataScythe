# Security Policy

## Supported versions

| Version | Status |
| --- | --- |
| 0.1.x | Active release line |
| Earlier versions | Unsupported; upgrade to a current verified release |

Use a promoted native artifact and its matching documentation, checksum
manifest, and detached signatures as one release set. Do not mix an executable
from one version with metadata from another.

## Security properties and limits

DataScythe is a local storage utility that performs high-impact destructive
operations. Its C++ engines issue host-visible writes or supported device
commands. The application does not implement a cryptographic erasure proof or
claim certification against a media-sanitization standard.

The result “success” means the requested software operation completed through
the OS/device interface without a reported fatal error. It does not guarantee
that every requested readback ran: erase verification is skipped with a warning
when the final scheduled pass is random and no final zero pass is configured.
Check warnings and operation settings. Success does not mean every physical
storage cell, remapped block, firmware-reserved region, snapshot, backup, or
remote replica has been erased.
Read the [Threat Model](THREAT_MODEL.md) before using the tool for a security
disposition or regulated-media decision.

The source tree has no application-level account, telemetry, server, or upload
protocol. The program necessarily relies on the host OS, kernel, drivers, and
storage firmware; those components can have their own network or data-handling
behavior.

## Safe deployment

### Verify the release set

Obtain the release public key through a channel independent of the artifact
download, and compare the full fingerprint before trusting it:

```text
DataScythe Release Signing (2026) <release@datascythe.local>
Fingerprint: 72E7 D550 77BA 8D7D 425B DC76 6133 2D0E 78FE F1CF
```

From the repository root, verify the manifest signature and checksums:

```bash
gpg --show-keys docs/DataScythe_Release_Signing_2026_pubkey.asc
gpg --import docs/DataScythe_Release_Signing_2026_pubkey.asc
gpg --verify docs/SHA256SUMS.asc docs/SHA256SUMS
python scripts/generate_sha256sums.py --check
```

Verify the detached signature for the exact archive you intend to install. For
example:

```bash
gpg --verify releases/native/linux/DataScythe-0.1.0-linux-amd64.tar.gz.asc \
  releases/native/linux/DataScythe-0.1.0-linux-amd64.tar.gz
```

Use the matching macOS or Windows signature/archive pair for those platforms.
Successful checksum verification without a trusted manifest signature only
shows internal consistency; it does not establish who produced the files. PGP
release signatures do not substitute for Windows Authenticode publisher trust.

### Build from source

Use a reviewed source revision and a controlled toolchain. The normal build
requires CMake 3.16 or later and a C++17 compiler; the GUI additionally requires
Qt 5.15 or Qt 6 development packages. Disable unneeded components, protect the
build host, and retain the exact source revision and build configuration used
for any operational binary. A locally built binary does not automatically
inherit the release signer’s provenance.

### Privilege and target control

Raw-device operations require administrator/root privileges. Run only the
minimum required privileged process, on a trusted and fully controlled host.
An attacker with equivalent privilege can change device mappings, race file
paths, tamper with reports, or interfere with I/O; DataScythe is not a boundary
against the host administrator or kernel.

Before authorizing any device operation, independently confirm the exact
physical path, model, serial, capacity, and intended role. Do not treat a
`system`/`not system` label as authoritative: the current Linux and macOS drive
enumerators do not reliably identify the running system disk, and platform
inventory can be incomplete. Manually exclude the boot/system device.

The GUI uses typed confirmation phrases and a final confirmation dialog for
destructive drive, clone, file, and folder actions. The CLI prompts unless
`--yes` is supplied; for drive-level modes, `--yes` also requires an exact
`--confirm-target` path. These are operator-error controls, not cryptographic
authorization. Scripts that provide `--yes` must validate device identity
immediately before invoking DataScythe and must log the approved path and
identity.

The mounted-volume operation is distinct: its core path does not run the same
dismount gate used by full-device and quick-zero modes. It can overwrite a
volume while the OS still has the filesystem mounted. Do not run it against an
active volume unless filesystem corruption and concurrent writes are acceptable;
unmount first where the platform and workflow permit.

For clone operations, check both source and target. A live source is not frozen
or snapshotted. If consistency matters, make the source quiescent or use an
appropriate hardware write blocker. A target dismount failure aborts the clone;
a reported success from the OS dismount helper does not prove that all possible
mounts and writers are absent.

## Operational handling

- Prefer a disposable test image or simulated device when validating a new
  build, script, or workflow. Never test destructive code against a production
  or system disk.
- Keep the target disconnected from other hosts and services during the run
  where practical. Ensure the target is not concurrently mounted or modified.
- Save logs, certificates, and clone reports to a separate controlled device.
  They may disclose filenames, paths, drive model/serial identifiers, hashes,
  sizes, and operation status. The default export code does not encrypt them.
- Protect export destinations with filesystem ACLs and encrypted storage as
  appropriate. Certificate/report output may truncate an existing file at the
  selected path. Check the destination before export.
- Treat cancellation, power loss, process termination, I/O error, failed
  verification, or dismount error as an incomplete operation. Do not release or
  repurpose the medium based solely on elapsed time or a partial progress bar.
- Choose verification coverage deliberately. Sparse verification samples only
  a few locations; percentage mode samples evenly spaced 512-byte regions; full
  mode reads back the OS-addressable range. Even full readback does not inspect
  physical regions hidden by the device or filesystem. Hardware-erase
  verification is sparse zero sampling. Full/high-percentage verification
  currently allocates an offset entry for every sample before reading; large
  devices can require substantial RAM and may fail under memory pressure.
- If a random pass is the final scheduled pass and final-zero is disabled,
  erase verification is skipped with a warning. Read the warning stream; do not
  assume a success status or default verification flag proves that readback ran.
- Directory shredding collects the path set before writing; very large trees
  can consume substantial memory. Plan capacity and avoid concurrent directory
  changes during collection and processing.
- Choose the operation matching the sanitization scope. File overwrite and
  unlink do not sanitize filesystem history; a mounted-volume wipe is not a
  physical-drive wipe; clone tail zero-fill is only a target-copy behavior.
- Do not infer that random overwrite passes are cryptographic sanitization. If
  OS random-byte generation fails, the current implementation uses a
  deterministic fallback sequence rather than aborting the operation.

## Data handling

DataScythe keeps operation log entries in process memory and writes them only
when the operator exports a log or report. Certificates and clone reports are
plain text. An erasure certificate carries a SHA-256 digest of its unsigned
body; that digest is tamper evidence only when the expected digest is protected
independently. It does not authenticate the author, device, time, or result.
Clone reports may include source and target SHA-256 values, which attest only to
the byte streams observed through the host I/O path. Neither artifact is
digitally signed by the application.

The application does not erase its own log/export files after use. It does not
clear OS caches, journals, snapshots, backups, temporary files, swap, crash
dumps, or remote copies. Apply the host organization’s retention and media
handling policy to those artifacts.

## Vulnerability reporting

Do not publish an exploitable vulnerability, a weaponized reproducer, or
sensitive target data in a public issue. Use GitHub’s private vulnerability
reporting flow for this repository when it is enabled. If private reporting is
unavailable, contact the maintainers through a verified private channel before
publishing technical details.

Include, where safe:

1. Affected version or source revision and operating system/platform.
2. Exact build options and relevant privileges.
3. Minimal reproduction steps using a disposable image or mock device.
4. Expected versus observed behavior and whether unintended bytes were written.
5. Logs with serial numbers, usernames, and unrelated paths redacted.
6. A concise impact statement covering target confusion, privilege boundary,
   incomplete erasure, data disclosure, or evidence integrity.

Do not send live credentials, private keys, production drive images, or personal
data in a report. The project does not publish a response-time SLA or guarantee
a coordinated-disclosure schedule; maintainers should acknowledge receipt,
reproduce the report safely, assess affected releases, and coordinate a fix and
advisory before public technical disclosure when feasible.

## Maintainer security requirements

Changes to drive enumeration, system-disk classification, target resolution,
raw I/O, mount handling, file traversal, overwrite scheduling, device command
pass-through, cancellation, verification, or evidence export are security
sensitive. At minimum:

- Review destructive scope and error paths on every supported platform.
- Ensure precondition failure returns before the first write or device command.
- Keep file and device operations fail-closed on enumeration or I/O errors.
- Test with fake `IRawDevice` implementations or disposable images; tests must
  not discover and write host disks.
- Distinguish a successful API call from a verified physical sanitization
  outcome in code, UI, CLI output, and evidence.
- Preserve release artifact, manifest, detached signature, and documentation
  consistency when publishing a release.
- Never include signing private keys, production storage images, or user data
  in the repository or build artifacts.

