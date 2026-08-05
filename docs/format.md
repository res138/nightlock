# Nightlock vault file format (`.nlck`), version 1

A vault is a single file: a fixed 76-byte plaintext header followed by
one AEAD-sealed payload. Everything the reader needs to derive the key
sits in the header; the header itself is authenticated as associated
data, so any tampering with it — including KDF-parameter downgrades —
fails decryption.

All integers are little-endian. Multi-byte fields are written with
explicit byte shuffles (never struct dumps), so the encoding is
identical across platforms.

## Header (offsets in bytes)

| Offset | Size | Field             | v1 value / meaning                          |
|-------:|-----:|-------------------|---------------------------------------------|
|      0 |    4 | magic             | `4E 4C 43 4B` — ASCII `NLCK`                 |
|      4 |    2 | format version    | 1 (envelope/crypto semantics)                |
|      6 |    2 | header length     | 76 — bytes before the ciphertext             |
|      8 |    4 | cipher id         | 1 = XChaCha20-Poly1305 (IETF)                |
|     12 |    4 | kdf id            | 1 = Argon2id v1.3                            |
|     16 |    4 | kdf memory        | KiB; default 65536 (64 MiB)                  |
|     20 |    4 | kdf iterations    | default 3                                    |
|     24 |    4 | kdf parallelism   | 1 (the sodium backend is single-lane)        |
|     28 |   16 | salt              | random per vault; regenerated on password change |
|     44 |   24 | nonce             | fresh random on **every** save               |
|     68 |    8 | ciphertext length | plaintext length + 16 (the Poly1305 tag)     |
|     76 |    … | ciphertext ‖ tag  |                                              |

**AAD = header bytes [0, 76).** The 192-bit XChaCha nonce makes a fresh
random nonce per save safe without bookkeeping.

Readers must reject:

- wrong magic (`NotAVault`),
- unknown format version / cipher id / kdf id (`UnsupportedVersion`),
- a header shorter than 76 bytes, a ciphertext length that does not
  match the remaining file size, or KDF parameters outside the sanity
  clamp (`InvalidHeader`).

The clamp — memory ≤ 4 GiB, iterations ≤ 64, parallelism = 1 — matters
because KDF parameters are attacker-readable *before* any
authentication: a crafted file must not be able to request a 128 GiB
Argon2 run as a denial of service.

A failed tag check is reported as `WrongPassword`; cryptographically,
a wrong password and a tampered file are indistinguishable, and UI
copy should say so.

## Payload

The decrypted payload is a TLV record sequence:

```
record   = tag:u16 | length:u32 | value
payload  = Meta record, then exactly one Group record (the root)
```

Container records nest further records inside their value. String
values are UTF-8, not NUL-terminated; timestamps are signed
milliseconds since the Unix epoch.

### Tag map

| Tag      | Container | Meaning                                    |
|----------|-----------|--------------------------------------------|
| `0x0001` | yes       | Meta                                       |
| `0x8101` |           | PayloadVersion, u32 = 1 — **critical**     |
| `0x0102` |           | VaultName (informational)                  |
| `0x0103` |           | SavedAt, i64 ms (informational)            |
| `0x0002` | yes       | Group                                      |
| `0x0201` |           | Group name                                 |
| `0x0202` |           | Group icon                                 |
| `0x0003` | yes       | Entry                                      |
| `0x0301` |           | Entry name                                 |
| `0x0302` |           | Login                                      |
| `0x0303` |           | Password (secret)                          |
| `0x0304` |           | Created, i64 ms                            |
| `0x0305` |           | Modified, i64 ms                           |
| `0x0306` |           | URL                                        |
| `0x0307` |           | Icon                                       |
| `0x0308` |           | Note                                       |
| `0x0309` |           | 2FA code (secret)                          |
| `0x030A` |           | Pattern, u32 enum                          |
| `0x030B` |           | Entry preset, u32 enum                     |
| `0x030C` | yes       | Additional entry field                     |
| `0x030D` |           | Entry list color, u32 enum                  |
| `0x0C01` |           | Field label                                |
| `0x0C02` |           | Field value (secure storage)               |
| `0x0C03` |           | Secret display flag, u32 boolean           |
| `0x0C04` |           | User-defined field flag, u32 boolean       |

Rules:

- Child Group/Entry records appear **in stored order** — that order is
  the user's custom sort and must round-trip exactly.
- Empty optional scalar fields are omitted on write. Additional-field
  containers may keep an empty value so a preset's editable schema
  survives a round trip.
- Unknown **Pattern** values decode as `None` (decorative data must not
  brick a vault written by a newer build).
- Unknown **Entry list color** values likewise decode as `None`.

### Versioning

Tag bit `0x8000` marks a record **critical**.

- Unknown non-critical tag → skip it. New optional fields ship without
  any version bump, and older builds keep opening newer vaults.
- Unknown critical tag, or PayloadVersion greater than the reader's →
  refuse to load (`UnsupportedVersion`). Used for changes an old
  reader must not silently drop.

The header's format version covers the envelope (magic through
ciphertext length); PayloadVersion covers TLV semantics. They move
independently.

## Atomic save protocol

1. Serialize into pinned, scrubbed memory; seal with a fresh nonce.
2. Write the complete image to `<vault>.tmp` (mode 0600).
3. Flush: `fcntl(F_FULLFSYNC)` on macOS (plain `fsync` does not flush
   the drive cache on APFS), `fsync` elsewhere.
4. If `<vault>` exists, rename it to `<vault>.bak`.
5. Rename `<vault>.tmp` → `<vault>`; fsync the directory where the
   platform requires it.

Crash before step 4 leaves the old vault untouched; a crash between 4
and 5 leaves `.bak` intact. Readers never look at `.tmp`.

Concurrent writers (CLI + desktop app) are last-writer-wins in v1,
with `.bak` as the safety net; advisory file locking is future work.
