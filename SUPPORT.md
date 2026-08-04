# Support policy

## Current status

Nightlock is under active development and does not yet offer a long-term-support release line. Support is best effort unless a security advisory or release announcement states otherwise.

## Source and release support

| Line | Status | Security fixes | Compatibility fixes |
|---|---|---|---|
| `main` | Active development | Best effort | Best effort |
| Tagged historical versions | Historical | Not guaranteed | Not guaranteed |

The current source version is `1.2.0`, with [VERSION](VERSION) as the authoritative source. Release tags must match it exactly with a leading `v`; see [CHANGELOG.md](CHANGELOG.md).

## Platform baseline

The following configurations are continuously compiled and tested:

| CI target | Toolchain role | Guarantee |
|---|---|---|
| `macos-14` | Apple build and test baseline | Source build and tests only |
| `windows-2022` | Visual Studio 2022 build and test baseline | Source build and tests only |
| `ubuntu-22.04` | GCC/Linux build and test baseline | Source build and tests only |

CI success does not by itself guarantee that an installer works on every end-user OS version. Runtime support will become authoritative only after package smoke tests cover clean target systems.

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
