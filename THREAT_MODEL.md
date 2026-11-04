# DataScythe Threat Model

## Purpose and scope

This document describes the security properties and limits of the DataScythe
0.1.x source tree. It covers the C++ core, command-line interface, Qt desktop
application, platform storage backends, hardware erase paths, and local report
and certificate output. It is an implementation threat model, not a claim of
independent assessment, product certification, or conformance to a media
sanitization standard.

DataScythe performs destructive local storage operations. The security-critical
question is not whether a progress bar reaches 100 percent. It is whether the
intended object was selected, the intended addressable range received the
requested writes or device command, errors were surfaced, and residual copies
outside that operation were understood by the operator.

## Security objectives

1. Make destructive scope visible before execution and require deliberate
   operator action in the supplied front ends.
2. Refuse to treat failed I/O, failed verification, incomplete file collection,
   or failed target-volume dismount as successful completion.
3. Restrict low-level writes to the path or device opened by the platform
   backend; never interpolate target paths into a shell command.
4. Explain the limits of host-visible overwrites, hardware erase commands,
   verification, clone reports, and erasure certificates.
5. Avoid transmitting selected paths, drive identifiers, or operation results
   through application-defined network or telemetry channels.

These objectives are conditional on a trustworthy executable, a trustworthy
kernel and storage stack, valid device identity information, and an operator
who checks the target before authorizing the operation.

## System context

The application has four main layers:

| Layer | Security-relevant responsibility |
| --- | --- |
| CLI and Qt GUI | Parse operation choices, show target information, gather confirmation, display progress and errors. |
| Core engines | Schedule overwrite passes, enumerate files, copy bytes, verify reads, calculate results, and produce logs/reports. |
| Platform backends | Open raw devices, query size and geometry, dismount or unmount volumes, enumerate drives, and issue device-specific commands. |
| OS, storage stack, controller, and medium | Translate host reads/writes or sanitize commands into actual storage behavior. This layer is outside DataScythe's control. |

The source contains no application-level service, account, telemetry, or upload
protocol. Release acquisition and signature verification occur outside the
running application. Platform helpers are invoked with argument vectors (for
example `umount` or `diskutil`); DataScythe does not construct a shell command
from a target path.

## Assets and security properties

| Asset | Required property | Principal exposure or failure mode |
| --- | --- | --- |
| Source data during clone | Read without unintended modification | A live source can change during acquisition; the OS may write to it concurrently. |
| Target drive or volume | Only the selected target is overwritten | Wrong selection, stale inventory, path aliasing in non-GUI callers, or device identity confusion. |
| Files selected for shredding | All collected regular files receive the requested writes | Permission failures, path replacement races, filesystem semantics, snapshots, remapping, and uncollected object types. |
| Erasure status and verification result | Success reflects completed engine work and any requested checks | Read-back proves only the behavior visible through the current OS/device interface. |
| Device and volume identity | Model, serial, path, size, and system classification are accurate enough for operator review | Platform enumeration is incomplete and system-drive classification is not reliable on all supported platforms. |
| Logs, certificates, clone reports | Integrity and confidentiality are controlled by the operator | Reports contain paths and device identifiers; certificate digest is not a signature; exports may overwrite the chosen destination. |
| Release binaries and documents | Installed bytes match a trusted release | Compromised build/signing keys, unverified downloads, or mixed release sets. |

## Trust boundaries

| Boundary | Assumed trusted | Not trusted or not controlled |
| --- | --- | --- |
| Operator to UI/CLI | Operator understands selected source/target and confirms the requested action | Accidental clicks, ambiguous names, stale selection, automation that supplies `--yes`, and misleading device metadata. |
| Front end to core | Configuration values passed by the application | The confirmation interface is not an authorization boundary against another process or a privileged attacker. Core APIs can be called by other code without the GUI dialog. |
| Core to platform backend | Backend reports errors and byte counts according to its implementation | Kernel, drivers, device firmware, controller caches, RAID/USB bridges, and actual media behavior. |
| Process to host | OS access controls, file descriptors/handles, and privilege checks | A compromised kernel, administrator/root malware, hostile filesystem driver, or process with equivalent privileges. |
| File collection to file open | Collected paths identify the intended files during enumeration | Another process can potentially replace or alter a path between collection and open (TOCTOU); hard links and concurrent mutation are not excluded. |
| App to exported artifact | Operator-selected output destination | Existing files may be truncated; output directories, ACLs, backup policy, and later custody are external. |
| Release channel to executable | Published checksum and detached signature are verified against the separately trusted public key | A compromised endpoint, signing key, build host, or key-distribution channel. |

## Adversaries and capabilities

### In scope

- An operator who selects the wrong device, mistakes a volume for a whole
  device, or assumes a successful status proves physical sanitization.
- A concurrent unprivileged process that changes accessible files or source
  data while an operation is running, subject to OS permissions and sharing
  behavior.
- A distribution attacker who substitutes a binary, manifest, or document
  before installation.
- Storage hardware or filesystems whose behavior differs from simple linear,
  stable, host-visible byte storage.
- Operational failure: power loss, process termination, cancellation, media
  errors, dismount failure, or insufficient privilege.

### Out of scope for prevention by this application

- Kernel, boot chain, administrator/root, or firmware compromise.
- A hostile process with equivalent privilege modifying device state or
  intercepting I/O.
- Physical laboratory recovery, NAND chip-off, controller attacks, remapped
  sectors, hidden capacity, or undocumented firmware behavior.
- Copies in snapshots, journals, backups, replication, sync clients, caches,
  crash dumps, or remote systems.
- Attacks requiring an application network service: no such service is
  implemented, but the OS or another installed component may communicate.

## Operation-level analysis

### Whole-device overwrite and quick zero-fill

The core opens a target through the platform raw-device implementation, queries
its size, and writes in bounded buffers. Full-device mode follows the configured
pass schedule (default configuration: three scheduled passes plus a final zero
pass); quick mode writes one zero pass. For block devices, the default
partition-metadata option first zeros up to 1 MiB at the beginning and, for
devices larger than 2 MiB, up to 1 MiB at the end. It does not parse and
selectively remove every filesystem structure.

On full/quick erase, the engine refuses to proceed if the backend reports a
volume-dismount failure. A successful dismount call is only as complete as the
platform backend's discovery logic. Linux matches `/proc/mounts` device names
using string-prefix checks; Windows scans drive letters and examines the first
reported extent for each volume; macOS invokes `diskutil`. The Linux prefix
check can overmatch similarly prefixed device names, while the Windows scan can
miss mount points or later extents. These approaches do not establish that every
possible mount, namespace, multi-extent volume, or external holder has been
quiesced.

Cancellation, write/flush errors, loss of power, or process termination can
leave a partially overwritten target. Treat every interrupted or failed run as
unsanitized until independently assessed. A cancellation request is cooperative
and is observed between I/O chunks, not as an atomic rollback.

### Mounted-volume operation

The volume mode targets a mounted volume, not all partitions or all bytes of a
physical drive. Where a physical drive path is supplied, the front ends resolve
the first enumerated mounted volume. The selection and platform mapping must be
checked; a volume wipe is not equivalent to a whole-device wipe. The core's
pre-write dismount gate applies to full-device and quick-zero modes, not this
mounted-volume mode. The OS and applications may therefore continue to access
or modify the filesystem while its exposed volume bytes are being overwritten.
Expect filesystem corruption and inconsistent writes; do not use this operation
as a safe substitute for unmounting or as a physical-drive sanitization claim.

### File and directory shredding

The collector accepts regular files and, when requested, regular files under a
directory. It refuses a top-level symbolic link and does not recurse through
directory symlinks. On Windows, alternate data streams are enumerated when the
feature is enabled. Other non-regular objects are not treated as file payloads.
Traversal and per-entry inspection errors abort collection before the erase
engine begins writing. Files that cannot be opened or overwritten are reported
as failed/skipped; the aggregate operation is not reported successful.

Path enumeration and later opening are separate operations. A concurrent actor
with directory write access may replace a path in that interval. Hard links are
not a separate storage unit: overwriting a file's data changes the same inode or
file record as seen through other links. Deleting a directory entry removes a
name, not all replicas or filesystem history.

The collector builds the full path list before writing begins. Very large trees
therefore consume memory proportional to the number and length of collected
paths; resource exhaustion can stop the operation before or during processing.

File overwrite is a best-effort logical write through the filesystem. Copy-on-
write, journaling, snapshots, SSD flash translation, compression, deduplication,
remote filesystems, and backup agents can preserve prior versions. The current
implementation may round a regular-file overwrite length to a reported block
size; when deletion is disabled this can extend the logical file. Neither
overwrite nor unlink is a guarantee of physical erasure.

### Drive cloning

Clone reads from source offset zero through the reported source size and writes
those bytes to target offset zero. It refuses a target smaller than the source.
If the target is larger, the tail remains unchanged unless the explicit tail
zero-fill option is enabled. Optional verification compares the addressable
source and target bytes and calculates SHA-256 digests. A changed source between
copy and verification is warned about, not frozen or snapshotted. A clone is
therefore not necessarily a point-in-time consistent image.

Before copying, the core asks the target backend to dismount volumes. A reported
failure now aborts before writes. A reported success still inherits the
backend's mount-discovery limits described above. The source is not dismounted
or write-blocked by the clone engine. Source hashes and a successful comparison
do not establish provenance, forensic completeness, or correctness of
firmware-hidden sectors.

### Hardware secure erase

The secure-erase path issues supported ATA or NVMe commands through
platform-specific pass-through interfaces. NVMe Sanitize completion is polled
where the backend implements it; ATA paths report that a command was issued.
Availability, semantics, completion, and sanitization of remapped/reserved media
depend on the exact device firmware, bridge, driver, and vendor implementation.
Some backends retry NVMe with a different sanitize action after failure. An
accepted command or a sampled zero readback is not independent proof that all
physical cells or controller-reserved regions were sanitized.

Bus detection is not uniformly based on hardware inquiry. The current macOS
secure-erase backend infers ATA versus NVMe partly from the device-path string,
while macOS drive enumeration exposes `/dev/diskN` names. Such a name can be
classified as ATA even when the underlying device is NVMe; the advertised
hardware path should not be assumed to select the correct protocol on that
platform without confirming the implementation and device support.

### Verification and evidence

Erase verification reads the expected final pattern back through the same
platform I/O path. Sparse mode checks three 512-byte locations (or fewer for a
small target); percent mode chooses evenly spaced 512-byte samples; full mode
checks the addressable range in 512-byte steps. The current implementation
materializes all selected offsets in memory before checking them. Full
verification, or a high percentage on a large device, can require substantial
RAM and may fail from memory pressure; the samples are not streamed. These
checks test observable readback, not data remanence beneath the translation
layer. Hardware secure erase verification uses the zero-sample helper and is
sparse; selecting a full verification mode does not turn that helper into a
full-media examination.

If the final scheduled overwrite pass is random and no final zero pass is
configured, the erase engine skips pattern verification and records a warning.
The operation may still return success, and the result's verification flag is
not a reliable indication that readback ran in this case. Inspect warnings and
the pass configuration; do not interpret “passed” as evidence of a check that
was skipped.

Clone verification compares source and target at every byte in the reported
source range and hashes the observed streams. SHA-256 is used for evidence and
comparison, not confidentiality or authenticity. An erasure certificate's
content digest detects later changes only if the expected digest is trusted; it
is not digitally signed. Clone reports and logs are likewise unsigned unless
the operator protects or signs them separately.

## High-impact controls and residual risk

| Threat | Current control | Residual risk |
| --- | --- | --- |
| Wrong destructive target | GUI shows device path/model/serial and requires typed phrase plus final confirmation; CLI prompts unless `--yes`; drive-level `--yes` requires exact `--confirm-target`. | Confirmation proves intent over a string, not physical identity. Always compare path, model, serial, and capacity immediately before execution. |
| System-disk destruction | CLI and GUI check enumerated `is_system_drive` flags; some Windows backends populate them. | **Do not rely on this control on Linux or macOS:** their current drive enumerators do not reliably mark the running system device/volume. The CLI exact-path mapping is also not a general alias resolver. Manually identify and exclude the boot/system storage. |
| Mounted target corruption | Whole-device erase and clone stop if dismount reports failure. | Backend coverage is imperfect; success does not prove every mount or process has released the target. |
| Partial overwrite presented as success | I/O/flush errors fail the result; directory collection errors fail before writing. | Verification can be skipped when the last pass is random and final-zero is disabled. Power loss, forced termination, and cancellation leave partial writes. Inspect warnings and preserve the failure record. |
| Weak random pass | Scheduler uses a C++ random engine seeded from `std::random_device`; byte generation uses OS randomness. | If OS byte generation fails, current code falls back to a deterministic byte sequence without surfacing a hard error. Do not treat random-pass success as a cryptographic sanitization guarantee. |
| False physical-erasure assurance | UI/docs and certificates describe OS-addressable scope and storage limits. | Host-level readback cannot observe remapped or reserved flash, controller behavior, snapshots, or firmware. |
| Report disclosure or tampering | Exports are explicit and contain local result metadata; certificate includes a content digest. | Paths, serials, hashes, operation state, and excerpts may be sensitive. Files can be overwritten at the selected destination; digest is not a signature. |
| Supply-chain substitution | Release set includes checksum manifest and detached PGP signatures. | Verification is only as trustworthy as the acquired public key, fingerprint, and signing/build infrastructure. |

## Operator security invariants

Before a destructive run:

1. Establish the exact physical device and its role independently of the
   application's system-drive flag. Record path, model, serial, and capacity.
2. Disconnect or unmount unrelated storage where practical; close applications
   that may write to the target; use hardware write protection for a clone
   source when evidentiary consistency matters.
3. Use the narrowest operation that satisfies the objective. A file overwrite,
   volume wipe, whole-device overwrite, hardware sanitize, and clone tail wipe
   have different scopes.
4. Export evidence to storage that is not the target. Restrict access to that
   storage and preserve logs, certificates, hashes, and release identity
   together.
5. Treat any failed, cancelled, interrupted, or unexpectedly short operation
   as incomplete. Do not infer sanitization from UI progress alone.

## Maintainer review checklist

Changes touching target selection, drive enumeration, raw-device open/close,
volume dismount, file traversal, overwrite scheduling, hardware commands,
verification, cancellation, or report generation require security review.
Reviewers should verify at minimum:

- All target sizes, offsets, and byte-count arithmetic are bounded and checked.
- A failed precondition cannot fall through to the first destructive write.
- Every opened handle is closed on all error paths; partial work is explicit.
- Device identity and system-volume detection fail safely on each platform.
- File collection cannot silently omit an object while reporting complete
  success, and symlink/alias behavior is deliberate.
- Reports distinguish attempted operations, observed verification, and
  physical-media claims.
- Tests use fake devices or disposable images and cannot address host disks.

