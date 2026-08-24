# Support policy

## Current status

Nightlock is under active development and does not yet offer a long-term-support release line. Support is best effort unless a security advisory or release announcement states otherwise.

## Source and release support

| Line | Status | Security fixes | Compatibility fixes |
|---|---|---|---|
| `main` | Active development | Best effort | Best effort |
| Tagged historical versions | Historical | Not guaranteed | Not guaranteed |

The current source version is `1.2.4`, with [VERSION](VERSION) as the authoritative source. Release tags must match it exactly with a leading `v`; see [CHANGELOG.md](CHANGELOG.md).

## Platform baseline

The release artifacts have these end-user baselines:

| Artifact | Supported baseline | Bundled runtime |
|---|---|---|
| Windows Setup | 64-bit-compatible Windows 10 1809 (build 17763) or later | Qt, plugins, static libsodium, and the signed Microsoft Visual C++ x64 Redistributable |
| macOS DMG | macOS 13 or later on Intel or Apple Silicon | Universal application, Qt frameworks/plugins, and static libsodium |
| Debian package | Ubuntu 22.04 `amd64` baseline | Private Qt/ICU runtime; normal OS libraries are resolved automatically by APT |

No separate Qt, libsodium, Visual Studio, or compiler-runtime installation is required for these artifacts. Debian system libraries remain package dependencies and are installed by the package manager, not by a manual prerequisite procedure.

The following configurations are continuously compiled, tested, packaged, and smoke-tested:

| CI target | Toolchain role | Guarantee |
|---|---|---|
| `macos-14` | Apple build and test baseline | DMG mount, dependency/signature checks, CLI launch, and deterministic GUI launch |
| `windows-2022` | Visual Studio 2022 build and test baseline | Silent Setup install, PE dependency checks, CLI launch, and deterministic GUI launch |
| `ubuntu-22.04` | GCC/Linux build and test baseline | Debian install in a clean Ubuntu 22.04 container, complete ELF closure, CLI launch, and Xvfb GUI launch |

CI cannot prove compatibility with every hardware, driver, policy, or future OS update. The baselines above define the supported release scope; older OS versions and architectures not named there are not implied to be supported.

## Build requirements

- CMake 3.21 or newer;
- a C++20-capable compiler;
- Qt 6.10.1 with Widgets and SVG for the desktop application;
- libsodium 1.0.18 or newer, or the pinned vendored source path;
- platform packaging tools only when producing release artifacts.

## Vault compatibility

- The current envelope format version is 1.
- The current payload version is 1.
- Unknown non-critical TLV fields are skipped.
- Unknown critical fields and newer payload versions are rejected.
- Compatibility changes require format documentation, regression tests, and an ADR or RFC.

See [docs/format.md](docs/format.md) for the normative byte-level behavior.

## Deprecation and end of life

Until the first stable release, compatibility-breaking changes may occur but must be documented in `CHANGELOG.md` and in release notes. After a stable-release policy is adopted:

1. deprecations must name a replacement;
2. removal must not occur in the same stable release that introduces the deprecation;
3. vault-format readers must preserve documented backward compatibility or provide a migration path;
4. end-of-life dates must be announced before support ends.

## Getting help

Use public GitHub issues for reproducible build failures, non-sensitive bugs, and feature discussions. Remove personal paths and use synthetic vaults.

Do not use public issues for vulnerabilities, master passwords, vault contents, recovery material, signing material, or private crash data. Follow [SECURITY.md](SECURITY.md).
