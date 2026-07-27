# Nightlock security notes

What the vault protects, what it deliberately does not, and the knobs
that control the balance. The companion file format spec lives in
[format.md](format.md).

## At rest (the `.nlck` file)

- **KDF:** Argon2id v1.3 (libsodium), 32-byte key. Defaults: 64 MiB,
  3 iterations, single lane — roughly 200–400 ms on an Apple-Silicon
  desktop. Parameters are stored per file and can be raised for more
  paranoid vaults (`VaultFile::create`/`changePassword`); readers clamp
  them (≤ 4 GiB, ≤ 64 iterations) so a crafted file cannot stage an
  Argon2 memory bomb.
- **Cipher:** XChaCha20-Poly1305 with a fresh random 192-bit nonce on
  every save. The whole 76-byte header is authenticated as associated
  data — flipping any header field (version, KDF cost, salt…) is the
  same as flipping ciphertext.
- **Salt:** 16 random bytes per vault, regenerated on every master
  password change.
- **Writes:** atomic (`tmp` + `F_FULLFSYNC` + `rename`), previous image
  kept as `.bak`, file mode 0600. A crash at any point leaves either
  the old vault or the new one, never a half-written file.
- A wrong password and a tampered file are cryptographically
  indistinguishable — both surface as "wrong password — or the file is
  damaged".

## In memory

Secrets in the core live in `secure::String`/`secure::Bytes`
(`core/include/nightlock/secure.hpp`):

- backing pages are `sodium_mlock`ed (best-effort) so they stay out of
  swap;
- every buffer that goes through the allocator is zeroized on free
  (`sodium_munlock` scrubs even when the earlier mlock failed);
- the derived key is wiped and the whole decrypted tree is
  field-by-field wiped on `VaultFile::lock()` — in the desktop app,
  locking (⌘L) really destroys the plaintext tree, it does not just
  cover the window;
- the serialized plaintext payload is built in `secure::Bytes` and
  scrubbed right after sealing.

### Known, accepted limitations

- **SSO:** short strings live inline in the string object and bypass
  the allocator; `secure::wipe()` covers live objects (including the
  capacity tail), and the lock-time tree walk covers the final free,
  but transient inline copies from moves/copies can linger briefly.
- **Qt widgets copy secrets.** The moment a password is shown,
  copied or edited, it exists in `QString` buffers, the clipboard, and
  widget internals that Qt never zeroizes. This is the same trade-off
  KeePassXC documents; fixing it would mean abandoning the toolkit's
  text machinery.
- **No swap/hibernation guarantee.** `mlock` is best-effort and the
  window contents can end up in compressed memory or screenshots like
  any GUI app.
- **No file locking between writers** (CLI + app run concurrently):
  last writer wins, the loser's image survives as `.bak`.

## Passwords

- The master password is never stored anywhere, in any form; there is
  no recovery. Losing it means losing the vault.
- The CLI reads passphrases with terminal echo off and restores the
  terminal on every exit path (including Ctrl-C); `--password-stdin`
  exists for scripts and tests and is as safe as the pipe feeding it.
- The generator (`nightlock gen`, `nightlock::generatePassword`) draws
  through `randombytes_uniform` — unbiased, CSPRNG-backed.
