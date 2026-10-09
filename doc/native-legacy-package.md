# Installed versioned migration package

The extension is now version **0.1.1**, so installation does not overwrite the
live 0.1.0 generation's files in place. The local gate passed **98 native
contracts in 196.52 seconds**, with 631 inputs unchanged, and **198 extension /
39 browser tests** with frozen view/asset bytes. Native C++ source is unchanged
from `5d8b175`; the extension version metadata bump is included in the tested
source map. xlang3 remains pinned to `7b8b32ae`, bridge disabled; no CPython
interpreter was executed.

The real local runtime bundle contains eight native executables/libraries,
1,869 pure library source files, 21 license files and one provenance file.
Independent ZIP verification matched all **1,899 runtime files**, the manifest
and six required extension source files in the **1,933-entry VSIX**. The shared
browser verifier checked its 18 required renderer/access/license assets. A real
qualified server started using this complete bundle, persisted a session through
embedded xlang3 and gracefully retired/exited. No model was configured in that
smoke test. The first fixture put its workspace under `.agentflow` and was
correctly rejected; a fresh ordinary temporary workspace passed without relaxing
the native exclusion.

The package was installed through VS Code's CLI into the existing profile. The
old view was closed before clearing its obsolete runtime-directory setting, and
TestProj reopened in a **normal installed window**, without the previous
development helper. The original native process continued running. The sidebar
reconnected to its saved backend, retained its transcript, OpenAI profile/model
and actual recorded usage (1,773 input, 133 output, 1,906 total, 1,727 cached,
27 reasoning tokens). No key was requested again and no replacement database was
created.

VS Code cleanup renamed the old extension directory and removed 1,859 unloaded
runtime files while its server remained alive/listening. This broke native image
lookup. The original directory name was restored, preserving the loaded image's
file, and only absent files were restored from the independently verified
pre-update runtime copy. All original runtime bytes matched its exact manifest
again. Database and SecretStorage were untouched. Native listener inspection
then observed the original PID **32028**, creation time
**134360001610207446**, and executable hash successfully. This is a recorded
legacy installation repair; it is not a claim that the old generation already
used immutable private runtime retention. New managed generations do.

The installed command completed native authenticated source preflight and is at
the product **Stop and migrate** confirmation for TestProj. Windows computer-use
confirmation is awaiting the user's response; its button has not been clicked
by the agent. Native migration and real rendered file create/edit acceptance
are still pending. The installed backend remains the original read-only source.
The code and fixtures implement the [migration path](native-legacy-stop.md);
the installed acceptance is a separate claim.

[Exact tested inputs, package hashes and installed scope](evidence/native-legacy-package-local.json),
[complete native output](evidence/native-legacy-package-local-ctest.log).
