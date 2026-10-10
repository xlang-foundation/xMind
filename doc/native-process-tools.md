# Native coding process tools — implementation contract

The `37e153b` hosted gate built and passed 51 native contracts but its approved
process-executor fixture exceeded the unchanged 45-second outer timeout.
[Exact gate provenance](evidence/native-cli-recovery-corrected-gate-failure-provenance.json)
records that failure. It supplied no phase output locating the stall, so its cause
remains unproven.

The fixture now flushes static phase names with measured elapsed milliseconds
around approval, dispatch, binary/stream/flood output, executable binding,
result/output journal faults, reopen/quarantine, timeout/cancellation and store
closure. No product engine behavior, assertion or timeout was changed. A new
local build passed all 52 native contracts; this fixture completed in 10.32
seconds with every phase reaching completion. [Local phase trace](evidence/native-process-timeout-diagnostic-local.log)
and [provenance](evidence/native-process-timeout-diagnostic-local-provenance.json)
preserve that scope. Diagnostics have not yet reproduced or explained the hosted
stall; the next exact hosted gate remains required before release validation.

Three additional sequential runs of only this traced contract passed in 9.67,
10.17 and 10.19 seconds, each reaching the final phase under the same 45-second
outer timeout. [Unaltered repeated CTest output](evidence/native-process-timeout-diagnostic-repeat.log)
and [source/binary provenance](evidence/native-process-timeout-diagnostic-repeat-provenance.json)
record the actual temporary-workspace effects. No engine change or timeout
increase was made, and the hosted stall has not reproduced or been diagnosed.

The installed verified runtime `4fc1148` has now completed one live
`gpt-5.6-sol` Responses command cycle through the native CLI and shared browser.
A trusted `git-status` profile binds the installed Git executable, read-only
status arguments and a three-second budget. The authorized validation controller
checked the exact executable/profile revision, native binding, arguments,
directory and expiry before sending `/allow`. Native execution recorded exit
zero, retired its process tree and persisted stdout/stderr plus an output event.
Both streams matched an independent Git invocation. The browser displayed the
actual command, retained output, outcome and both responses' supplied metrics.
[Live evidence](evidence/live-responses-git-status.json) records sanitized
results. This verifies that one registered foreground command, not shell,
background/PTY, build/test automation or complete coding parity. Older milestone
statements below retain their original scope; the primary VS Code preview was
not replaced or populated for this check.

The foreground process milestone passed at source `3297a4bce2d591c0f1c1dd37ccc18a15297369e7`: **35 native and 40 extension contracts passed** in [run 37635775031](https://github.com/xlang-foundation/xMind/actions/runs/37635775031). [Original complete CTest output](evidence/native-process-passing-ci-ctest.log), [original job log including extension TAP](evidence/native-process-passing-ci-job.log), [provenance](evidence/native-process-passing-ci-provenance.json). Actual Windows child/file effects verify tree cleanup, exact approvals/executable bindings, xlang3-backed profile/output persistence, storage failures/quarantine, model/admin/server/CLI continuation and watcher reconnect. Inference is synthetic; renderer/host fixtures are labelled. The [actual unseeded right-sidebar preview](evidence/vscode-native-process-bundle-sidebar.png) now uses this tested bundle with its existing database/profile preserved. No model or process profile is seeded; live coding and populated editor process execution remain unverified. Shell parsing/discovery, background jobs, PTY, full-output artifacts and broader parity remain required. Earlier failures/pending statements below retain their historical source scope and are superseded only by this exact passing result.

Adapter source `6e348ad1b142f44a05b9c03f333c82eb79b474ff` built in Release, but [isolated run 37624585693](https://github.com/xlang-foundation/xMind/actions/runs/37624585693) failed one of 32 native contracts: `native_process_adapter_contract` reported `Owned process exit is not established`. The other 31 native contracts passed. [Original complete CTest log](evidence/native-process-adapter-ci-failure.log), [original provenance](evidence/native-process-adapter-ci-failure-provenance.json). Extension tests were skipped and no runtime bundle was published. This is not a process milestone pass. The actual editor preview remains on the earlier verified creation bundle.

Executor/conditional-agent source `027f5387cfc4c6c18273ffd45fd20ba622d1dbae` is pushed. Its [isolated run 37625495428](https://github.com/xlang-foundation/xMind/actions/runs/37625495428) reached terminal cancellation without a job result when the newer sidebar checkpoint entered the branch's concurrency queue. Source `add194ece77f9c1a96f87a7a78c790c3621c3c62` built in Release, but [run 37626160239](https://github.com/xlang-foundation/xMind/actions/runs/37626160239) failed both process contracts: the adapter reported the same unestablished process-exit observation; the executor's interrupted-lifecycle assertion failed. The other 31 native contracts passed; extension tests were skipped and no bundle was published. [Original complete output](evidence/native-process-executor-ci-failure.log), [provenance](evidence/native-process-executor-ci-failure-provenance.json). Correction source `765b9037ce7dcb8d383aaa6919884aafadb7e5fb` reached the limited terminal result documented below in [run 37626465263](https://github.com/xlang-foundation/xMind/actions/runs/37626465263).

The original adapter failure was at the zero-time root-process handle probe after job accounting reached zero. That probe cannot wait for asynchronous termination to finish; [Microsoft's process termination documentation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess) requires waiting on the actual process handle, and [job termination](https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-terminatejobobject) applies process termination to the tree. Correction `765b9037ce7dcb8d383aaa6919884aafadb7e5fb` waits for the root handle within the remaining five-second cleanup budget. [Run 37626465263](https://github.com/xlang-foundation/xMind/actions/runs/37626465263) built Release and passed **32 of 33 native contracts**, including the complete direct process executor contract (actual approvals, file effects, storage fault/recovery and interrupted quarantine). The adapter still failed with an opaque post-dispatch observation error. [Original complete output](evidence/native-process-exit-wait-ci-result.log), [provenance](evidence/native-process-exit-wait-ci-provenance.json). Extension tests were skipped; there is no passing process bundle or full process milestone. This narrows the observed failure but does not prove the remaining adapter behavior.

The diagnostic correction distinguishes pipe closure (`ERROR_BROKEN_PIPE`/`ERROR_PIPE_NOT_CONNECTED`) from other native observation errors and retains numeric stage-specific error codes for pipe/job failures. Output-observer exceptions remain opaque in the product; only the isolated test fixture prints its own readiness diagnostics. No arbitrary exception/output text is added to public outcomes. Diagnostic source `9a6b4422a65e58c59eaa696b79feea3b46445a94` built Release and passed **34 of 35 native contracts** in [run 37630878223](https://github.com/xlang-foundation/xMind/actions/runs/37630878223). The adapter reports `Fixture readiness parent-exit: Actual descendant handle unavailable: Windows error 87` before `stdout_observer_failed`. [Original complete output](evidence/native-process-parent-exit-ci-diagnostic.log), [provenance](evidence/native-process-parent-exit-ci-provenance.json). Extension tests were skipped and no runtime bundle was published.

The remaining failure is consistent with the fixture's Node parent killing its child on exit. [Node's pinned libuv implementation](https://github.com/nodejs/node/blob/v22.23.3/deps/uv/src/win/process.c#L1016-L1035) assigns non-detached children to its own kill-on-parent-exit job. The parent-exit fixture now uses `detached:true` to omit that inner job. [The same implementation](https://github.com/nodejs/node/blob/v22.23.3/deps/uv/src/win/process.c#L988-L1001) explicitly omits `CREATE_BREAKAWAY_FROM_JOB`, so it does not request escape from xMind's enclosing native job. The test additionally requires the actual descendant handle to be alive at readiness; it still requires native timeout, a signaled descendant handle before return, the root's actual zero exit, and absence of the delayed child marker. This is a test-fixture correction, pending exact-source CI verification, with no weakened process cleanup assertion or new product capability claim.

Source `e0046d865ebe4450220edf6421f14e9718594382` built Release and passed **34 of 35 native contracts** in [run 37628786269](https://github.com/xlang-foundation/xMind/actions/runs/37628786269). The configuration, direct approval executor and model/admin/server/CLI process contracts all passed: actual executable binding/change rejection, embedded-xlang3 storage/reopen, rejected admin lease, exact approvals, real child/file effects, denied/cancelled approval, result continuation and model-free restart. Inference is synthetic. The lower-level adapter still reported the opaque post-dispatch observation failure. [Original complete output](evidence/native-process-profiles-ci-result.log), [provenance](evidence/native-process-profiles-ci-provenance.json). Extension tests were skipped and no bundle was published; there is no complete process milestone or full coding/provider/editor acceptance claim.

The thin sidebar renderer now reviews the exact executable/argument vector, profile revision, relative directory and budgets before sending only an operation decision ID. Known process results render separate stdout/stderr, explicit hex bytes, visible control escapes, observed exit/PID/timing and retained/drained counts. Raw JSON remains available. Uncertain commands expose no approval/retry action; malformed proposals cannot be allowed and malformed results retain raw evidence. All **34 extension contracts passed locally** with labeled DOM/host fixtures: [complete output](evidence/vscode-process-review.log). This does not establish populated editor/native process execution. No synthetic command/history was inserted into the interactive preview.

The host revalidation contract now explicitly covers a changed process executable binding after review, alongside changed file proposal bytes. The real extension host handler refuses to call the decision API when the re-fetched exact proposal differs. All **34 extension contracts pass locally** with this additional scenario: [complete output](evidence/vscode-process-binding-review.log). These remain labeled isolated host/DOM fixtures; they do not establish actual populated editor execution.

The pinned OpenCode reference is `v2.0.16`, commit `3a103fe0aff726a4edc7492f03f7b88195d9e4c9`. Source inspection of `packages/core/src/tool/plugin/shell.ts`, `packages/core/src/shell.ts` and `packages/core/src/session/shell.ts` shows more than a subprocess call: foreground/background commands, working-directory checks after approval, timeouts, retained output, lifecycle observation and completion notifications. The pinned API inventory includes `session.shell` and shell discovery. Implementing a foreground tool alone will not establish parity with these capabilities. No reference implementation is copied or executed.

## First native component

The first component runs a foreground command through a trusted, explicitly configured Windows executable. It must support build/test tools with literal argument vectors; a separately configured shell profile can interpret a reviewed command string. Executable/profile lookup belongs to the backend, never the model or editor. Models cannot select an arbitrary executable path, supply environment values, disable ownership, or declare a command safe. All commands require an exact recorded approval, including commands described as read-only.

Use a dedicated C++ process adapter and executor. The existing MCP stdio adapter is a protocol transport: it discards stderr content and requires newline-framed writes. Passing build output through it would lose evidence and mix protocol ownership with coding processes. Common Windows handle/quoting/launch helpers can be factored only with regression coverage for existing MCP and schema workers.

The prepared adapter supports trusted absolute local-drive directories with retained, non-reparse ancestor handles and captured file identities. UNC/reparse directories remain unsupported. It has one exclusive reader for each anonymous pipe, alternates bounded reads of bytes already reported available, and keeps draining after its combined retention budget is exhausted. A bounded observer receives raw retained bytes; result counts report all drained bytes. EOF input, explicit environment/handle inheritance, suspended job assignment, tree accounting, timeout/cancellation and output-sink uncertainty are implemented in source. The adapter does not authorize requests or store outcomes.

The executor uses the embedded-xlang3 operation journal. Backend-created immutable profiles choose an executable and fixed prefix; model arguments choose a registered profile, literal argument vector, relative directory and bounded timeout. A proposal retains the complete launched argument vector, profile revision, directory identity and budgets. It claims the workspace and stable `process-profile:<id>` resource before dispatch. The adapter retains the enclosing workspace identity as well as the selected nested directory. Completed zero/nonzero exits are acknowledged process results with `independently_verified:false`; invalid text bytes are encoded as hex. Timeout/cancellation after dispatch and failed output observation preserve quarantine. Failed outcome storage leaves the executing claim for restart and causes the conditional agent loop to fail-stop through the existing service health path. The direct executor contract passed actual SQLite outcome faults, recovery, another-workspace quarantine, real timeout and cancellation after an actual file effect at the exact partial CI source scopes above. This does not establish the still-failing adapter contract or a complete process gate.

`AgentSettings.process_profiles` conditionally advertises/routes `run_process` through this executor. Source now connects the persisted registry through server startup and offline administration; no profile is seeded by default. Independent model/server/admin/CLI continuation and restart tests are prepared with synthetic inference and actual file/process effects. Populated editor execution, credential/environment configuration, durable output streaming, retained full-output artifacts and broader restart/background behavior remain incomplete. A synchronous adapter observer must be bounded by its caller; the adapter does not preempt an arbitrary blocked C++ callback.

## Durable foreground output

The corrected full source `3297a4bce2d591c0f1c1dd37ccc18a15297369e7` is now running in [run 37635775031](https://github.com/xlang-foundation/xMind/actions/runs/37635775031). The obsolete watcher source was explicitly cancelled only after its process-test Git blob matched the recorded C7692 build failure; it reached terminal cancellation and produced no runtime bundle. [Observed scope](evidence/native-process-superseded-build-cancellation.json). The corrected gate's actual result remains pending; the active source is kept stable while it verifies.

The isolated CI helper now builds the actual process executor contract and its production dependencies before the remaining native targets. Its diagnostic is retained as `native-process-contract-build.log`. A successful priority build still proceeds through the full native build, exact 35-contract registration check and complete CTest gate; no partial build produces a bundle or counts as a milestone pass. PowerShell syntax was checked locally without compiling while the separate benchmark controller remains live.

Output source `b6173ec725ced7d08cd3b49b91ea8094cbfd441c` reached terminal failure in [run 37633769037](https://github.com/xlang-foundation/xMind/actions/runs/37633769037) during test compilation. MSVC reported C7692 for comparing the reconstructed `std::string` to a JSON value in `process_executor_contract.cpp`. [Original complete build output](evidence/native-process-stream-ci-build-failure.log), [provenance](evidence/native-process-stream-ci-build-failure-provenance.json). The comparison now explicitly extracts `std::string`; the exact final-buffer assertion is unchanged. Native tests and extension CI were not run, and no bundle was published. Earlier live-status statements below are historical observations, not passes.

Latest sidebar evidence validation: command approval now requires a present bounded backend executable binding and displays it as an opaque identity; native code remains responsible for checking the actual binary. Process result metrics require safe nonnegative drained/retained counts and exact retained byte lengths for UTF-8/hex data. Inconsistent or missing metrics retain raw result evidence without a structured process outcome. All **40 extension contracts passed locally** with labelled fixtures: [original output](evidence/vscode-process-evidence-validation.log). This is thin-view validation, not a populated editor/live-provider or complete native process pass.

The preceding host-binding source `51f32c6b7b8ee2c99abd4843700141cf7bf9bc66` reached terminal failure in [run 37631649263](https://github.com/xlang-foundation/xMind/actions/runs/37631649263): **34 of 35 native contracts passed**, with the same actual parent-exit handle failure (Windows error 87). [Original complete output](evidence/native-process-host-binding-ci-result.log), [provenance](evidence/native-process-host-binding-ci-provenance.json). Extension execution was skipped and no bundle was published. The corrected/output source below is now running; a live build is not a pass.

Source `b6173ec725ced7d08cd3b49b91ea8094cbfd441c` is queued in [run 37633769037](https://github.com/xlang-foundation/xMind/actions/runs/37633769037). It retains the parent-exit cleanup correction and expands the existing 35-contract native gate. The earlier cleanup-only [run 37632566052](https://github.com/xlang-foundation/xMind/actions/runs/37632566052) reached terminal cancellation without a job result when this newer source entered the branch's pending concurrency slot; cancellation proves no test result. The running earlier source is not restarted or cancelled by this change. No new passing artifact is inferred.

The executor prepares cursor-replayable `process.output` events in the existing run journal through embedded xlang3. Each stored event contains `operation_id`, `profile_id`, `channel`, `encoding:hex`, a per-channel byte `offset`, `retained_bytes` and exact hex `data`. Hex preserves invalid bytes and split UTF-8 characters. Authenticated HTTP/CLI clients consume these persisted events; they never imply exit or effect verification. The final result retains actual drained counts, truncation and lifecycle.

The shared 64 KiB capture bounds this durable prefix. Full 4 KiB chunks flush as they arrive. At most 64 smaller early chunks flush, spaced by 100 ms after the first; remaining partial bytes flush on adapter return, including observed timeout/cancellation. Each operation has at most 82 output events. After the early flush budget is exhausted, sparse output waits for a full chunk or return. Output beyond the capture limit is drained and counted but is not retained. Full-output artifacts and continuous streaming beyond this prefix remain separate requirements.

An event-write failure during execution causes native job retirement and preserves uncertainty. Failure to record the uncertain outcome retains the claimed operation for recovery. A final-buffer write failure after adapter return raises `ProcessOutcomeUnrecorded` and preserves the executing claim. Neither failure acknowledges success or replays effects. Synchronous persistence shares the bounded-observer limitation; no hard deadline for arbitrary database stalls is claimed.

The sidebar reconstructs separate channels from contiguous offsets, shows valid UTF-8 with escaped controls, and leaves invalid/incomplete UTF-8 as labelled hex. Exact duplicate fragments are ignored; conflicting duplicates, gaps and capture overflow show an incomplete-sequence notice without invented bytes. Run/conversation changes clear the stream; transcript refresh preserves it. The view bounds itself to 64 command cards and directs excess output to recorded activity/results. No PID, elapsed time, exit or token metric is inferred from output events.

All **39 extension contracts passed locally** with labelled isolated renderer/host fixtures: [original output](evidence/vscode-process-stream.log). Expanded native checks are source-prepared for actual pre-exit streaming with child acknowledgement, byte-exact invalid/Unicode replay, high-volume capture/drain bounds, SQLite reopen, actual event-write failure and HTTP/CLI cursor replay/model-free restart. Their pass is pending exact-source CI. The interactive preview has no seeded commands/output and remains on its earlier verified bundle.

## Trusted profile registry and executable binding

`ProcessConfigurationStore` stores versioned metadata in the existing information repository through embedded xlang3. A bounded trusted JSON or YAML file supplies `id`, absolute local-drive `executable`, `prefix_arguments` and `max_timeout_ms`. The backend assigns revisions and executable bindings; desired files cannot supply those fields, environment values, credentials or unknown fields. Identical configuration/bytes preserve revision; policy or executable changes rotate it. Removed IDs remain permanently retired. Import is startup/offline administration under the existing database owner lease; there is no live public mutation API.

The binding combines the actual Windows file identity and SHA-256 of the retained executable, with a 256 MiB executable limit. Native [CNG hashing](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptcreatehash) uses owned algorithm/hash handles. The launched file and every executable-parent directory are retained with write/delete sharing denied. A changed file identity or bytes reject dispatch before user code runs. The profile snapshot and proposal both retain this binding; startup of an execution service with an obsolete profile snapshot requires explicit administrator reimport. Model-free inspection can still read saved registry/history. This does not authenticate dependent DLLs, scripts or arbitrary remote effects, and is not an OS sandbox.

Prepared usage after a passing build, for a trusted local xlang3 profile:

```yaml
profiles:
  - id: xlang3
    executable: D:/CantorAI2026/xlang3/build/Release/xlang3.exe
    prefix_arguments: []
    max_timeout_ms: 120000
```

The trusted profile import accepts either JSON or the same bounded YAML subset
used by native AgentFlow authoring. Both formats share duplicate-field,
executable-binding, timeout and retirement checks; changing only the file format
does not revise an identical profile. Stop the backend before importing into
its database:

```powershell
.\build\native\Release\xmind_admin.exe --db STATE.sqlite --modules MODULES --stdlib LIB_SOURCE import-processes profiles.yaml
```

The server loads saved profiles or imports a trusted file at startup with `--process-config FILE` (`Tools/agentflow.ps1 -ProcessConfig FILE`). Actual execution also requires a configured model with supported tools and a verified `--workspace`. The authenticated `GET /v1/process/profiles` and CLI `process-profiles` expose only IDs, revisions and timeout budgets with `runtime_state:per_operation`; they do not invent active processes or expose commands/bindings. These flags are source-prepared and are absent from the older tested preview bundle until a new passing artifact is installed.

Profiles use Windows CRT-style argument quoting, verified against the independent Node fixture. Shell-specific parsing, selection and discovery remain separate required work; configuring a shell executable does not establish shell parity. The prepared configuration contract checks actual executable-byte changes, storage/reopen, rejected replacement preservation, backend revisions and retired identities. The prepared model/server/admin/CLI contract covers real import/owner-lease rejection, exact approved effects, denied/cancelled approval, changed-executable rejection, real result continuation and model-free restart. No live provider or populated editor claim follows from these fixtures.

The executor records the selected profile and revision, exact arguments or shell text, workspace/directory identities, resolved working directory, timeout and output budget in the existing operation proposal. Credential references remain server-side. The same approved proposal is rendered by CLI and VS Code. Shell syntax scanning may help review but cannot prove that a command stays within its working directory.

Before dispatch, retain verified directory handles and revalidate the selected directory identity. Windows process creation uses a path for its working directory; the implementation must establish the same directory before and after launch, and fail closed if it cannot. A working directory and a Windows job do not provide filesystem or network isolation. Initial support is therefore restricted to the existing full-access local-owner deployment. Team processes require scoped workers and explicit authority before shared-server readiness.

## Effect and lifecycle ownership

1. Parse and bound the model request; resolve trusted configuration and the actual workspace directory without launching a child.
2. Record the immutable proposal, await its single-use controller decision, and atomically claim the workspace resource. Denial, expiry and cancellation before launch must produce no process.
3. Create the child suspended with an explicit executable, literal Windows argument quoting, a closed/EOF input channel, captured stdout/stderr and an explicit inherited-handle allowlist. Start with a minimal trusted environment; never inherit backend model/authentication secrets.
4. Attach the suspended child to a kill-on-close job before resuming it. Retain child and job ownership in the backend. Any failed setup must terminate and reap the suspended child. Merely disconnecting a view must not cancel execution.
5. Resume once. After successful resume, filesystem/network side effects may already exist. Stream bounded, channel-labelled output through the owning execution and durable event sequence. Keep byte counts and truncation flags; invalid text bytes need an explicit representation rather than invented Unicode. Never block indefinitely on one pipe while the other fills.
6. Observe the actual exit status and drain both pipes. Cancellation or timeout must terminate the entire job and wait for its processes before releasing ownership. A failed wait, lost owner, outcome journal failure or interruption after dispatch leaves uncertainty and prevents automatic replay.

An observed nonzero exit is a completed command with a nonzero exit code, not evidence that no side effects occurred. Likewise, exit zero proves the process result only; it cannot independently verify arbitrary file or remote effects. Timeout/cancellation results must retain any observed exit/termination evidence without claiming rollback. Restart quarantines dispatched operations and does not rerun the command. A PID alone is never sufficient to adopt or terminate a recovered process because IDs are reused.

## Reviewable acceptance

Use independent Node fixtures strictly as tests; product execution is native C++. Verify actual child effects, not a synthetic process result:

- Literal Unicode/space/quote/backslash arguments, explicit working directory, distinct stdout/stderr and actual exit zero/nonzero.
- Real approval/denial/expiry/pre-dispatch cancellation, duplicate invocation and directory replacement while approval is pending. Denied cases leave no child-effect marker.
- Actual concurrent high-volume stdout/stderr, bounded capture/truncation, invalid UTF-8, EOF without a newline, and descendants holding pipes after the parent exits.
- Actual timeout and cancellation of a child tree. Confirm that a delayed descendant does not write its marker after termination; do not infer tree death from a parent exit alone.
- No inherited backend secret sentinel or unrelated inheritable handle. Environment and credential values must not appear in public configuration/proposals.
- Real outcome-storage failure after a child writes a file, followed by restart. The operation remains quarantined and the effect is not replayed.
- Native server/model/CLI continuation through the same executor, with explicitly synthetic inference; thin editor proposal/output rendering; eventual live-provider repository build/test task separately.

Register each actual contract in the isolated CI gate. Local heavy builds remain deferred while the independent xlang3 timing controller is live. Use the exact passing source/artifact for any visible preview.

## Following components required for parity

Durable background jobs and completion notification, cursor-based retained output, bounded full-output artifacts with retention, shell discovery/configuration, interactive terminal/PTY support, attributed recovery and platform adapters follow the foreground component. The shared runtime owns these services so CLI, VS Code, Electron and remote views can observe the same process. Remote view transport and WebRTC signaling never become process owners.
