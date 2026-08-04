# Architecture Decision Records

Architecture Decision Records (ADRs) preserve durable technical decisions and their consequences. Follow the [ADR process](../governance/adr-process.md).

| ADR | Status | Decision |
|---|---|---|
| [0001](0001-vault-cryptography.md) | Accepted retrospectively | Argon2id and XChaCha20-Poly1305 vault cryptography |
| [0002](0002-tlv-forward-compatibility.md) | Accepted retrospectively | Versioned TLV payload and critical-tag compatibility |
| [0003](0003-atomic-save-protocol.md) | Accepted retrospectively | Temporary file, backup rotation, and durable replacement |
| [0004](0004-qt-desktop-architecture.md) | Accepted retrospectively | Qt front end over a Qt-free shared core |
| [0005](0005-cross-platform-package-layouts.md) | Accepted retrospectively | Platform-specific installed layouts and release packages |

Use [template.md](template.md) for new decisions. Never renumber an ADR after it is referenced.
