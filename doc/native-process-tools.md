# Native coding process tools — implementation contract

Process and shell execution remain unavailable in the current preview bundle. The foreground adapter, journal-owning executor, conditional agent loop and startup/offline-admin profile registry are prepared in native source. Their independent actual-child/approval/configuration/model-server-CLI contracts are registered in CMake and the isolated gate, bringing the expected native set to 35. Node fixture syntax checks passed; local compilation was explicitly deferred by the live xlang3 benchmark controller. No complete process component or native contract pass is claimed until exact-source CI establishes it. This contract defines the next component; it does not establish an execution milestone.

Adapter source `6e348ad1b142f44a05b9c03f333c82eb79b474ff` built in Release, but [isolated run 37624585693](https://github.com/xlang-foundation/xMind/actions/runs/37624585693) failed one of 32 native contracts: `native_process_adapter_contract` reported `Owned process exit is not established`. The other 31 native contracts passed. [Original complete CTest log](evidence/native-process-adapter-ci-failure.log), [original provenance](evidence/native-process-adapter-ci-failure-provenance.json). Extension tests were skipped and no runtime bundle was published. This is not a process milestone pass. The actual editor preview remains on the earlier verified creation bundle.

Executor/conditional-agent source `027f5387cfc4c6c18273ffd45fd20ba622d1dbae` is pushed. Its [isolated run 37625495428](https://github.com/xlang-foundation/xMind/actions/runs/37625495428) reached terminal cancellation without a job result when the newer sidebar checkpoint entered the branch's concurrency queue. Source `add194ece77f9c1a96f87a7a78c790c3621c3c62` built in Release, but [run 37626160239](https://github.com/xlang-foundation/xMind/actions/runs/37626160239) failed both process contracts: the adapter reported the same unestablished process-exit observation; the executor's interrupted-lifecycle assertion failed. The other 31 native contracts passed; extension tests were skipped and no bundle was published. [Original complete output](evidence/native-process-executor-ci-failure.log), [provenance](evidence/native-process-executor-ci-failure-provenance.json). Correction source `765b9037ce7dcb8d383aaa6919884aafadb7e5fb` reached the limited terminal result documented below in [run 37626465263](https://github.com/xlang-foundation/xMind/actions/runs/37626465263).

The original adapter failure was at the zero-time root-process handle probe after job accounting reached zero. That probe cannot wait for asynchronous termination to finish; [Microsoft's process termination documentation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess) requires waiting on the actual process handle, and [job termination](https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-terminatejobobject) applies process termination to the tree. Correction `765b9037ce7dcb8d383aaa6919884aafadb7e5fb` waits for the root handle within the remaining five-second cleanup budget. [Run 37626465263](https://github.com/xlang-foundation/xMind/actions/runs/37626465263) built Release and passed **32 of 33 native contracts**, including the complete direct process executor contract (actual approvals, file effects, storage fault/recovery and interrupted quarantine). The adapter still failed with an opaque post-dispatch observation error. [Original complete output](evidence/native-process-exit-wait-ci-result.log), [provenance](evidence/native-process-exit-wait-ci-provenance.json). Extension tests were skipped; there is no passing process bundle or full process milestone. This narrows the observed failure but does not prove the remaining adapter behavior.

The next correction distinguishes pipe closure (`ERROR_BROKEN_PIPE`/`ERROR_PIPE_NOT_CONNECTED`) from other native observation errors and retains numeric stage-specific error codes for pipe/job failures. Output-observer exceptions remain opaque in the product; only the isolated test fixture prints its own readiness diagnostics. No arbitrary exception/output text is added to public outcomes. Strict descendant-death and delayed-marker assertions remain unchanged. Complete validation is still required; source `e0046d865ebe4450220edf6421f14e9718594382` is currently in [run 37628786269](https://github.com/xlang-foundation/xMind/actions/runs/37628786269).

The thin sidebar renderer now reviews the exact executable/argument vector, profile revision, relative directory and budgets before sending only an operation decision ID. Known process results render separate stdout/stderr, explicit hex bytes, visible control escapes, observed exit/PID/timing and retained/drained counts. Raw JSON remains available. Uncertain commands expose no approval/retry action; malformed proposals cannot be allowed and malformed results retain raw evidence. All **34 extension contracts passed locally** with labeled DOM/host fixtures: [complete output](evidence/vscode-process-review.log). This does not establish populated editor/native process execution. No synthetic command/history was inserted into the interactive preview.

The pinned OpenCode reference is `v2.0.16`, commit `3a103fe0aff726a4edc7492f03f7b88195d9e4c9`. Source inspection of `packages/core/src/tool/plugin/shell.ts`, `packages/core/src/shell.ts` and `packages/core/src/session/shell.ts` shows more than a subprocess call: foreground/background commands, working-directory checks after approval, timeouts, retained output, lifecycle observation and completion notifications. The pinned API inventory includes `session.shell` and shell discovery. Implementing a foreground tool alone will not establish parity with these capabilities. No reference implementation is copied or executed.

## First native component

The first component runs a foreground command through a trusted, explicitly configured Windows executable. It must support build/test tools with literal argument vectors; a separately configured shell profile can interpret a reviewed command string. Executable/profile lookup belongs to the backend, never the model or editor. Models cannot select an arbitrary executable path, supply environment values, disable ownership, or declare a command safe. All commands require an exact recorded approval, including commands described as read-only.

Use a dedicated C++ process adapter and executor. The existing MCP stdio adapter is a protocol transport: it discards stderr content and requires newline-framed writes. Passing build output through it would lose evidence and mix protocol ownership with coding processes. Common Windows handle/quoting/launch helpers can be factored only with regression coverage for existing MCP and schema workers.

The prepared adapter supports trusted absolute local-drive directories with retained, non-reparse ancestor handles and captured file identities. UNC/reparse directories remain unsupported. It has one exclusive reader for each anonymous pipe, alternates bounded reads of bytes already reported available, and keeps draining after its combined retention budget is exhausted. A bounded observer receives raw retained bytes; result counts report all drained bytes. EOF input, explicit environment/handle inheritance, suspended job assignment, tree accounting, timeout/cancellation and output-sink uncertainty are implemented in source. The adapter does not authorize requests or store outcomes.

The executor uses the embedded-xlang3 operation journal. Backend-created immutable profiles choose an executable and fixed prefix; model arguments choose a registered profile, literal argument vector, relative directory and bounded timeout. A proposal retains the complete launched argument vector, profile revision, directory identity and budgets. It claims the workspace and stable `process-profile:<id>` resource before dispatch. The adapter retains the enclosing workspace identity as well as the selected nested directory. Completed zero/nonzero exits are acknowledged process results with `independently_verified:false`; invalid text bytes are encoded as hex. Timeout/cancellation after dispatch and failed output observation preserve quarantine. Failed outcome storage leaves the executing claim for restart and causes the conditional agent loop to fail-stop through the existing service health path. Source tests exercise actual SQLite outcome faults, recovery, another-workspace quarantine, real timeout and cancellation after an actual file effect. These tests are prepared, not yet passed.

`AgentSettings.process_profiles` conditionally advertises/routes `run_process` through this executor. Source now connects the persisted registry through server startup and offline administration; no profile is seeded by default. Independent model/server/admin/CLI continuation and restart tests are prepared with synthetic inference and actual file/process effects. Populated editor execution, credential/environment configuration, durable output streaming, retained full-output artifacts and broader restart/background behavior remain incomplete. A synchronous adapter observer must be bounded by its caller; the adapter does not preempt an arbitrary blocked C++ callback.

## Trusted profile registry and executable binding

`ProcessConfigurationStore` stores versioned metadata in the existing information repository through embedded xlang3. A bounded trusted JSON file supplies `id`, absolute local-drive `executable`, `prefix_arguments` and `max_timeout_ms`. The backend assigns revisions and executable bindings; desired files cannot supply those fields, environment values, credentials or unknown fields. Identical configuration/bytes preserve revision; policy or executable changes rotate it. Removed IDs remain permanently retired. Import is startup/offline administration under the existing database owner lease; there is no live public mutation API.

The binding combines the actual Windows file identity and SHA-256 of the retained executable, with a 256 MiB executable limit. Native [CNG hashing](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptcreatehash) uses owned algorithm/hash handles. The launched file and every executable-parent directory are retained with write/delete sharing denied. A changed file identity or bytes reject dispatch before user code runs. The profile snapshot and proposal both retain this binding; startup of an execution service with an obsolete profile snapshot requires explicit administrator reimport. Model-free inspection can still read saved registry/history. This does not authenticate dependent DLLs, scripts or arbitrary remote effects, and is not an OS sandbox.

Prepared usage after a passing build, for a trusted local xlang3 profile:

```json
{
  "profiles": [{
    "id": "xlang3",
    "executable": "D:/CantorAI2026/xlang3/build/Release/xlang3.exe",
    "prefix_arguments": [],
    "max_timeout_ms": 120000
  }]
}
```

Stop the backend before importing into its database:

```powershell
.\build\native\Release\xmind_admin.exe --db STATE.sqlite --modules MODULES --stdlib LIB_SOURCE import-processes profiles.json
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
