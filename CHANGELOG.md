# Changelog

All notable changes to Nightlock will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project intends to follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html) after the current historical tag/version mismatch is resolved.

GitHub-generated release notes are a publication aid, not a second authoritative history. This file is the curated source for user-visible, compatibility, security, and operational changes.

## [Unreleased]

### Added

- Initial developer documentation program, governance model, onboarding guides, ADR/RFC framework, support policy, and third-party inventory.

### Changed

- Reconciled project versioning at `1.2.0` and made `VERSION` authoritative for CMake, application metadata, and release-tag validation.

### Deprecated

- Nothing yet.

### Removed

- Nothing yet.

### Fixed

- Windows release staging now uses an absolute install prefix, as required by Qt 6.10 deployment.
- Windows packaging now resolves Inno Setup through the platform API and is exercised by pull-request CI before release tags are created.

### Security

- Documented the current unsigned-release limitation and the required private vulnerability-reporting path.

## Historical versions

Tags `alpha-0.1`, `alpha-0.2`, and the release now named `v1.1.0` predate this changelog policy. The `v1.1.0` release used an internal CMake version of `0.1.0`; this historical mismatch is preserved rather than rewriting published artifacts. Starting with `v1.2.0`, the release tag, package metadata, and application version must match [VERSION](VERSION).

[Unreleased]: https://github.com/res138/nightlock/compare/main...HEAD
