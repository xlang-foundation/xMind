# Credential storage

The native Windows protection component compiled in Release and passed `native_secret_protection_contract`. It uses user-scoped [Windows DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata), with explicit context binding and no interactive UI. `SecretBytes` is move-only and clears its owned bytes on release; input/request copies remain the caller's responsibility. Encryption/decryption errors do not include the secret value.

The test uses synthetic binary bytes only. It checks round trip, wrong-context rejection, a modified ciphertext, unknown protection versions, empty input/context and move/clear behavior. It does not test other Windows user identities, host migration, backups or the HTTP credential lifecycle. Other OS protection providers are pending; unsupported OSes fail explicitly rather than store plaintext.

The C++ repository now persists encrypted credentials through xlang3 in a separate SQLite table. Schema version 2 migrates version 1 atomically and retains sessions/messages. Records carry scope, ID, purpose, label, revision, protection version and ciphertext. Metadata listing excludes ciphertext and plaintext. Backend-internal resolution checks purpose and decrypts with context binding to scope, ID, purpose and revision. Rotation and deletion require the current revision; deleted identities are retired so stale references cannot target a replacement credential. Use a new ID when replacing a deleted credential.

`encrypted_credential_repository_contract` verifies binary round trip across connections and reopen, scope/purpose checks, stale updates/deletes, retired identities, failed rotation rollback, ciphertext substitution rejection and migration failure/retry with preserved messages. Local-owner service authentication and provider discovery/enrollment were added in later checkpoints; see [provider setup](provider-setup.md). Repository scope filtering is not user authorization. Callers must authorize scope before invoking these backend-internal methods. Model context, session events and shared configuration must not receive credential plaintext. Team authorization belongs to private Nexus.

Windows DPAPI normally binds decryption to the protecting user's credentials and computer; team deployment uses the server identity. Define credential rotation and portable backup/restore before supporting a different server identity or host. DPAPI is a protection provider, not a reason to put a plaintext encryption key in SQLite.

To inspect the verified component:

```powershell
.\build\native\Release\agentflow_secret_contract.exe
```

The historical four-contract Release checkpoint passed. This establishes Windows credential persistence through embedded xlang3, not team authorization, portable secrets or overall platform readiness. Initial SQLite failures were resolved by the M1 runtime prerequisite patch; their historical log is retained separately. Later verification is recorded in [validation status](VALIDATION_STATUS.md).
