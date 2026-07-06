# Migration Guide

## From pre-restructure DataScythe layouts

The repository now follows the YellowSphere release layout.

| Old path | New path |
| --- | --- |
| `cli/` | `apps/cli/` |
| `gui/` | `apps/gui/` |
| `build/cli/DataScytheCLI.exe` | `build/apps/cli/datascythe.exe` |
| `build/gui/DataScythe.exe` | `build/apps/gui/DataScythe.exe` |
| `shred.c` | `docs/reference/shred.c` |

## Binary rename

The CLI executable is now `datascythe` instead of `DataScytheCLI`. Scripts and
documentation should call:

```powershell
.\build\apps\cli\datascythe.exe --help
```

The GUI executable name remains `DataScythe.exe`.

## Release artifact layout

Checked-in release binaries belong under:

```text
releases/native/windows/
releases/native/linux/
releases/native/macos/
```

Hashes for promoted artifacts are recorded in `docs/SHA256SUMS`.

## Version synchronization

Keep these files aligned when promoting a release:

- `CMakeLists.txt` `project(... VERSION ...)`
- `src/core/version.h` `kAppVersion`
- `package.json` `version`
- `CHANGELOG.md`
- `docs/Release_vX.Y.Z.txt`