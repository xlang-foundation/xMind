# xMind development constraints

Keep this file below 16 KiB: xMind reads it as repository guidance. Historical
checkpoint prose belongs in doc/evidence or the [verbatim archive](doc/development-checkpoints-20261018.md).
[Current validation status](doc/VALIDATION_STATUS.md) records accepted scope.

## Current work and preserved installation

- Only delivery goal: deliver a working v1 with solid UI and reliable browser/VS Code/CLI coding, prioritizing usability and release blockers over feature breadth. The user removed the old broader goal; do not resume or pursue that backlog. Advanced breadth is outside this goal unless it blocks v1. Acceptance: [working v1 goal](doc/WORKING_V1_GOAL.md).

- SDK6df6520f passes local184 (f86eda4b), hosted182/pip25, SDKpip25/26 and fixed perf11. Candidate77 VSIX ead875de passes fresh packaged CLI read/edit/Node4; NEVER replay sessione79c1b465d3a838371a5ca9ed24c3530/run41adee13318f1b47b08716af8eab53c4. Rendered/installed v1 acceptance remains open; installed76 unchanged. Local IPC only; cross-node workers belong to Cantor Cluster. See doc/VALIDATION_STATUS.md.

- Candidate77 hidden packaged browser passes read/edit/Node4, automatic preview and same-run reload; NEVER replay sessioncad770cfb1b44980bec72423e2d8c272/run13999d0493df4f06a6bdd2aa97ac13f1. Owners retired. Initial76 handoff profile-info exceeds30s before any task; retain .agentflow/packaged-v1-managed-handoff-sdk6df. Cause unproven; no replay/budget change. See status/evidence.

- Candidate77 packaged Code passes selected saved-editor context/read/edit/Node4, live buffer update and same-run reload after reauthentication; NEVER replay session4362efdf093a41249a5fe377dfbe1721/rune3bbf9fdeb1a498899fa280143c26a9e. Isolated backend/command and11 identity-verified Code processes retired. Authentication persistence/visible desktop are not proved; installed76 unchanged.

- Candidate77 runtime must have one link/file: redundant stage aliases were removed without byte/VSIX changes after native correctly refused hard links in .agentflow/m77. Fresh m77b managed startup/cancel/restart comparisons pass; final receipt-name collision is reconciled read-only. NEVER replay session8710aa29e1f918c42c050b77d21ee8b8/read5349848d37ee0fc81a28483ebd51c002/cancel72e374ce24659e89a22bcdd8ab289671. Both owners retired. Original76 startup failure remains unresolved; no upgrade acceptance.

- V1 blocker: packaged77 workspace create/switch/scoped files/history/reload pass, but full view/adapter restart resets B to launch A. Empty sessions1529aa4c008642b5abbf8f1444867766/A and a90fafab69754063a59404de0fb4ab6c/B remain; zero tasks/models. Repair preference restoration without creating more histories. Isolated .agentflow/u77 retains view10504, owners13476/31548 and adapter session49872; hidden CUA tab3 is handed off at http://127.0.0.1:64430/ui/. Exact identities in restart-live.json. Preserve collector failure; see workspace restart evidence. Installed76 unchanged.

- Installed76 Native d85a1697/SDK8c passes local179/hosted177, CLI/browser/editor/handoff, preservation and smoke. Exact75 backup remains. NEVER replay completed76 tasks. Hosted packaging and full goal remain open; see doc/VALIDATION_STATUS.md.

- Installed75 Nativec17c240b/SDK8c passes full179, clients500/178, fresh CLI/browser/editor coding and74-to75 handoff. Five roots/2442 files/2398 runtime/74 backup/smoke verify. See doc/evidence/installed-0.1.75-859c66da-20261024.json; goal active.

- Candidate74 editor updates clean buffer before reload; selected-context coding and history restore pass. NEVER replay session99b136d7bb544013a4d98bbc72705860/rootc3266a85e6704444b2c640e05babc442. See doc/evidence/candidate74-rendered-editor-coding-20261024.json.

- Installed75 smoke sessiondd7f9cdd53c3546a9fe1aaf50e9abd3c/rootcbc04d940ea8cc5278f403b9cc75aafe completes read/history with0models; owner exited. NEVER replay. Installed74 checkpoint archived verbatim.

- Candidate67 completed coding/handoff roots remain in doc/development-checkpoints-20261024.md verbatim; NEVER replay.

- Installed67 smoke55a89cad4783844c55b74fccd45ea580/root8866649c22b59f0793866858907107f4 completed read/history, zero models; NEVER replay. Owner exited. Installation66 guidance archived verbatim in doc/development-checkpoints-20261024.md.

- Browser first-send permission fix passes clients485/169 and actual UI without reload. Installed68 includes the fix. Both completed read/grant/revoke fixtures are in doc/evidence/browser-first-send-permission-refresh-20261024.json; NEVER replay.

- Qualification58 and completed browser58 guidance is preserved verbatim in doc/development-checkpoints-20261024.md; NEVER replay its tasks.

- Native55 CLI sessionf5eebd145890d132cfc59b0589491803/rootcc3d14ff5b4c570cebf41a92c85c6d52 completed2reads/1approvededit/1process, Node4/4,9messages. NEVER replay. Previous6sessions/150messages/11runs/configuration exact, owner56032 retained. See coding55-cli-live-coding evidence.

- Historical51/53/54 owner, history and NEVER-replay rules remain verbatim in doc/development-checkpoints-20261024.md. Preserve those completed tasks.

- Historical hosted view-count failures and corrected gates remain in doc/evidence/hosted-view-inventory-failure-20261019.json; those receipts are not new runtime acceptance.

- Historical e09259c4 qualification guidance is preserved verbatim in doc/development-checkpoints-20261024.md.

- Hosted39309088086/9c5bedbb is terminal failure154/155 in705.36s: graph PowerShell30051ms timeout, script_entered only, uncertain receipt/tree retired; SDK2/2. Official artifact12218800656/69559 bytes SHA1d68fe7a verifies. This is installed44 native source, not the new module policy; no hosted acceptance. See doc/evidence/hosted-native-9c5bedbb-process-timeout-20261019.json.

- Historical native and hosted timeout checkpoints (838e2eb, 9e4b118, 0c93640/a418 and f3e7718) remain in doc/development-checkpoints-20261018.md and doc/evidence. Failed cold commands are not qualified by later warm controls. Do not cancel/restart for observation delay, weaken deadlines or combine focused results into full acceptance.

- Keep VS Code closed and open no visible editor/browser windows unless asked. Earlier desktop control failed with black capture/GetCursorPos access denied; do not repeat unlock questions. The new Code pane observation is separate from still-unverified visible-browser acceptance. Headless sandboxed Edge development checks are separate evidence. Preserve all older user pages/owners, including ports 62084, 54662, 51118 and 52757. Exact older process identities, historical receipts and installation details remain in doc/development-checkpoints-20261018.md and doc/evidence. Verify PID, creation time and image before any intentional retirement; no mass cleanup of user owners, views or profiles.

- CLI59 owner68928/62378 session3b25f9b24e1fdc7e66aae877d42cf5f4/root0c35d911e7417e3c4982726fbb2fb201 completed wrapIndex/Node4/9msgs; NEVER replay. CLI60 owner48808/49919 (3123/5100, process-code) sessionfa20550e632f0522be7bfd4b0643c8ed/root41707328b40471d556818959016d73fb completed inclusiveRange via actual worker42868 retired: edit/Node4/13msgs/6charges/2 known argument denials. NEVER replay either task. Births/receipts: doc/VALIDATION_STATUS.md.

- Historical installed44/TestProj43/55921/u28 exact owner identities and NEVER-replay namespaces remain verbatim in [the owner archive](doc/development-checkpoints-20261025.md). Preserve all those owners, pages, profiles and completed tasks; global ownership/retirement rules below still apply.

## Execution and evidence rules

- User disk-retention priority (2026-10-25): keep test artifacts compact. Reuse the current incremental build; avoid duplicate runtimes, packages, dumps and successful fixture histories. Preserve real user data, the current qualified release inputs, required rollback/performance baselines, concise summaries and essential failure diagnostics. Regenerable obsolete intermediates may be removed only after checking live process references and resolved paths. Do not replace a policy-blocked cleanup with another deletion route.

- Before heavy native builds/tests, inspect live process command lines for pyperformance, run_benchmark.py, run_pyperformance_xlang3_shimmed.py, benchmarks/ or benchmarks\, and scratch/performance/run- variants. Defer while such work is live. Do not overlap native builds/tests. Follow one specific live session/PID plus birth/image to terminal; an observation timeout is not process completion and never authorizes restarting or killing it. Never weaken process budgets, retry effects or conceal a failed fixture to get a pass.

- Freeze the actual tracked source/input closure, allowed pure libraries, SDK/runtime, dependencies and native binaries around validation, checking file sets as well as hashes. Preserve original failed logs and compare equivalent final code. A full-suite claim requires its complete actual inventory and terminal result; focused results cannot be combined into an invented full pass. Synthetic model/peer tests, real provider execution, HTTP/controller restore and actual rendered browser/editor acceptance are separate scopes. Preserve supplied usage and raw receipts; never estimate or invent evidence.

- Keep heavy builds, packages and diagnostics under repository .agentflow and build/skill-state-native. The user cleanup can delete C:/xMindNativeValidation; do not create arbitrary top-level folders. Preserve unrelated untracked endpoint.obj and probe.obj. Do not mass-retire existing user backends, browser hosts or profiles. Verify ownership with exact PID, creation time and image before any retirement, and keep private tokens/configuration out of logs, command arguments and Git. Existing provider/process YAML remains under D:/CantorAI2026/xMind/.config; use configured credentials without asking for the same key again.

- Candidate SDK499415d0 at .agentflow/xlang3-host-guard-sdk passes SDK5/default perf11 and complete Native172 at17be6368. Source: .agentflow/xlang3-ctypes-runtime-fix; build: .agentflow/sdk-host-guard-candidate/build/Release. Prior .agentflow/xlang3-sdk f809 and baseline .agentflow/xlang3-ctypes-baseline retained. Installed61 unchanged; fresh delivery pending. Original sibling SDK untouched. See doc/VALIDATION_STATUS.md.

- Commit and push reviewable checkpoints. Preserve installed/runtime backups, user settings, workspace selection, history and existing successful pages. No legacy profile migration is required; do not rewrite user data to make a new package appear compatible.

## Product and implementation rules

- Scope revision (2026-10-07): xMind OSS includes the local C++/xlang3 agent and coding backend, SQLite persistence, single/graph agents, broad native provider support, MCP/A2A, CLI, browser UI and the VS Code extension. Team-server features, PostgreSQL, WebRTC/signaling and the standalone Electron IDE are excluded from OSS delivery and reserved for CantorAI's closed-source Nexus project in the sibling Nexus folder. Earlier proposals are not remaining OSS requirements. Do not implement, package or count private features toward OSS completion.
- xMind OSS uses SQLite behind C++ repository contracts, with database I/O through embedded xlang3. Keep schema creation, transactions and run ownership rules in the backend. No legacy-profile migration is required. Follow doc/architecture.md for current OSS scope.
- Scope correction, 2026-10-25: xMind workers are local processes using standard xlang3 IPC. Cross-node worker enrollment, assignment, leases and fencing belong to later Cantor Cluster work and are not xMind delivery requirements. Generic authenticated connection clients remain separate from network worker execution.

- Isolate the agent/coding runtime from UI views. OSS clients use the local backend's command/event contracts; UI adapters do not own execution, permissions or persistence. This separation remains required without WebRTC or Electron implementation.
- Coding must support the native console CLI, browser UI and VS Code extension. Preserve existing CantorAI/WorkSense installations; Electron reuse belongs to the separate closed-source product.
- xMind Server remains the local backend/API name in OSS. SQLite is owned by that backend and accessed through xlang3, never mounted by clients. Local-owner authentication and tool-effect authorization remain required. Optional Nexus binding is an OSS connection/agent capability; the team-server implementation remains private.
- SQLite operations must go through embedded xlang3, including secret and general information storage. C++ owns repository contracts and encryption but calls xlang3 for database I/O. Do not link the direct-SQLite reference as production persistence. Store encrypted credential blobs separately from public metadata; do not expose plaintext secrets to clients or model context.
- The user's latest architecture requirement is native C++ implementation of the platform. OpenCode and LiteLLM are behavior/capability references only; do not execute, embed, copy their implementations or add their SDKs as the core platform. C++ owns backend services, sessions, agent/model/tool orchestration, graphs, permissions, protocols, persistence and provider adapters. The earlier Python/FastAPI agent prototype was removed. Do not reintroduce it as the platform or as evidence of native completion.
- The user confirmed C++ owns the core, with embedded xlang3 for scripts, skills and pure-Python libraries, then authorized continued work after the architecture was drawn. Follow doc/architecture.svg and doc/architecture.md. Use xlang3's supported embedding/native SDK; never retain the old xlang package ABI merely by changing include paths. VS Code's required JavaScript/TypeScript host adapter should remain thin; all platform logic belongs in C++.
- Execute Python application code, tests, probes and Python tooling with the sibling xlang3 runtime. The user explicitly prohibited CPython use. Do not fall back to CPython when xlang3 compatibility fails.
- Use Node.js for the VS Code extension and independent TypeScript MCP peers.
- Record and fix xlang3 compatibility gaps or continue independent work. Do not conceal a gap by changing the Python interpreter or monkey-patching platform identity.
- CPython-compatible pure-Python standard-library and third-party source packages are explicitly allowed under xlang3. Native dependencies must use xlang3-compatible bindings; do not rely on CPython native extension binaries. Library reuse is not authorization to execute the CPython interpreter.
- Install pure-Python packages from PyPI using pip executed by xlang3. The user has authorized fixes needed by xMind in an isolated xlang3 branch without further questions. Keep the original xlang3 checkout/main and unrelated work unchanged; limit this task to xMind compatibility, not broad runtime optimization. Preserve exact gap/reproduction evidence and never switch to CPython.
- AgentFlow's repository-local commit email is shawn@cantorai.com. Preserve global Git settings and sibling repository identity.
- Preserve upstream history and applicable license notices. OpenCode and LiteLLM remain references only; xMind's own MCP/A2A, graph, coding and editor capabilities are the product targets.
