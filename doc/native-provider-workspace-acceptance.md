# Native providers and the opened VS Code folder

Source `d69a681886ebabcc38174985292fcd88dd7fc7ee` passed real native
workspace reads with OpenAI, Claude, Gemini and DeepSeek on 2026-10-08
(America/Los_Angeles). Each provider discovered models using its existing key,
selected a model through the production host controller, invoked `read_file`
inside `D:\CantorAI2026\TestProj`, and returned the exact random file contents.
The marker was absent from the prompt. Stored tool events and conversation
history verified the read. The disposable backend imported the single
`.config/providers.yaml` itself; the client did not read the keys.

| Provider | Tested model | Discovered IDs | Native read and exact reply |
| --- | --- | ---: | --- |
| OpenAI | gpt-5.6-sol | 135 | Passed |
| Claude | claude-haiku-4-5-20251001 | 13 | Passed |
| Gemini | models/gemini-3.5-flash | 11 | Passed |
| DeepSeek | deepseek-flash | 2 | Passed |

These results establish the four tested models, not coding capability for every
discovered ID. Provider usage and timing are retained without inventing missing
totals. This was real provider inference through Native and the production host
controller, separate from a rendered browser or VS Code interaction.

The first attempt passed three providers and failed Claude. A separate live wire
diagnostic captured an HTTP 200 tool block containing `caller: {"type":"direct"}`.
The unmodified C++ decoder rejected that field. The repaired decoder accepted
the identical 2,050 captured bytes and preserved the raw caller metadata for
history replay. Native accepts only the supported direct caller shape and
rejects malformed, extra or server-execution caller fields. Caller metadata
does not authorize tool effects. This corresponds to Claude's documented
[tool caller field](https://platform.claude.com/docs/en/agents-and-tools/tool-use/programmatic-tool-calling).
The original failed attempt remains separate in the evidence.

The shared UI controller now tells a user with saved providers to select one
in the sidebar, then select a model. It no longer asks that user to enter a
new key automatically. Explicitly adding a provider still requires a key.
Workspace epochs prevent stale discovery from updating a different folder.

The complete local gate passed **88/88 native tests in 174.89 seconds**, with
560 frozen inputs unchanged and no excluded or skipped tests. Separate source
tests passed **173 extension and 33 browser tests**, with 39 inputs unchanged.
Native provider fixtures in those contract suites are synthetic; the live
four-provider results above are separate. The accepted xlang3 SDK remains
`ad8040ffb8aba6eeabeb09053a8e222df09a4e7a`: its earlier 41/42 selected
correctness result includes one recorded baseline failure. No CPython was run.

Independent ZIP verification checked all 1,924 package entries, all 1,890
runtime files against the manifest, and six extension source files. A freshly
installed package passed actual VS Code workspace API and authenticated native
identity checks for Open Folder on `TestProj`, without a `.code-workspace` file
or manual server token. That isolated test disabled trust only in its test
profile; it did not exercise rendered model interaction.

The updated package is installed in the normal persistent VS Code profile.
Its settings and trust configuration were preserved. The existing window
must reload to activate the new files; its last observed state was untrusted.
No screenshot, user trust choice or successful model interaction in that
ordinary window is claimed. The older browser preview remains unchanged.
OpenAI model eligibility filtering, preview migration and broader coding/MCP/A2A
parity remain incomplete.

[Source, live-run and package evidence](evidence/native-provider-workspace-provenance.json).
