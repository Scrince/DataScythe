# Windows installer

Installs the Windows x64 GUI, CLI, and bundled Qt runtime dependencies. The application can erase data; verify target devices before use.

Installer: `release/windows/DataScythe-Setup-0.1.0.exe`

SHA-256: `829c770544432ea22317255b51a7aae45c5646f775b201b15d92ee6ba9f3ba24`

Detached OpenPGP signature: `release/windows/DataScythe-Setup-0.1.0.exe.asc`

This is a detached OpenPGP release signature, not a Windows Authenticode signature.

Public key: `docs/DataScythe_Release_Signing_2026_pubkey.asc`
Fingerprint: `72E7D55077BA8D7D425BDC7661332D0E78FEF1CF`

Compare the fingerprint above before importing the key. From the project root:

```powershell
gpg --show-keys docs/DataScythe_Release_Signing_2026_pubkey.asc
gpg --import docs/DataScythe_Release_Signing_2026_pubkey.asc
gpg --verify release/windows/DataScythe-Setup-0.1.0.exe.asc release/windows/DataScythe-Setup-0.1.0.exe
gpg --verify release/windows/SHA256SUMS.asc release/windows/SHA256SUMS
```
