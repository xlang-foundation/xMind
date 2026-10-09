# Native private-state workspace boundary

Native workspace tools now reserve both `.config` and `.agentflow` directory
components. The previous `.agentflow` exclusion covered ordinary search traversal
but left direct workspace reads and other access paths available. The shared
component guard now applies to caller spellings, resolved file/directory handles
and workspace-root ancestors, including Windows case and trailing-dot/space
aliases. Similarly named public paths remain available.

The expanded workspace contract verifies denial of private-state reads,
snapshots, fingerprints, replacement/creation proposals, directory listing,
directory identity, repository guidance and private workspace roots. Nested
components and a junction alias are covered. Test files contain explicitly
synthetic state values; no user token or provider credential is used.

Exact source `3dc25b8c09ea792bd08bb20a10ab4b1d09f7e603` passed its complete
**89/89 native gate in 268.34 seconds** and **35/35 browser suite**, with no
browser failures, skips or cancellations. The hosted extension test and native
browser integration steps also passed. The complete workflow then failed VSIX
packaging because that revision did not yet stage its built runtime. It produced
no accepted runtime artifact and was not installed in the live preview.

The downloaded diagnostic archive was checked against GitHub's advertised
SHA-256 and byte count. Its pinned native contract manifest exactly matches every
passed CTest identity and the committed CI declaration. The original passing
[CTest log](evidence/native-private-state-ctest.log) and
[scoped receipt](evidence/native-private-state-hosted.json) preserve that evidence.
The [hosted job](https://github.com/xlang-foundation/xMind/actions/runs/37875761154)
remains a failed packaging workflow; these results establish the boundary's
test scope, not a successful release, source-byte freeze or live deployment.

The subsequent staging fix is undergoing hosted validation. The later native
skill source has its separate pending 90-contract gate. Neither changes the
installed preview until its package and installation checks have passed.
