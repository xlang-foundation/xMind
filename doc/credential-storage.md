# Credential storage

The native Windows protection component compiled in Release and passed `native_secret_protection_contract`. It uses user-scoped [Windows DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata), with explicit context binding and no interactive UI. `SecretBytes` is move-only and clears its owned bytes on release; input/request copies remain the caller's responsibility. Encryption/decryption errors do not include the secret value.

The test uses synthetic binary bytes only. It checks round trip, wrong-context rejection, a modified ciphertext, unknown protection versions, empty input/context and move/clear behavior. It does not test other Windows user identities, host migration, backups or the HTTP credential lifecycle. Other OS protection providers are pending; unsupported OSes fail explicitly rather than store plaintext.

SQLite storage must use the xlang3-backed repository. The credential table and repository/service credential API are not yet implemented. Planned records carry credential ID, provider/type, display label, protection version and encrypted bytes. Public APIs expose metadata and credential references only. The provider/connector resolves a secret inside the backend when needed. Model context, session events and shared configuration must not receive credential plaintext.

Windows DPAPI normally binds decryption to the protecting user's credentials and computer; team deployment uses the server identity. Define credential rotation and portable backup/restore before supporting a different server identity or host. DPAPI is a protection provider, not a reason to put a plaintext encryption key in SQLite.

To inspect the verified component:

```powershell
.\build\native\Release\agentflow_secret_contract.exe
```

This component test does not establish database credential storage or overall platform readiness. The two SQLite adapter/repository tests still fail on the missing `isolation_level` API; see the complete native test evidence at `build/native/evidence/contracts-20261006.log`.
