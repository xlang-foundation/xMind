# Native runtime package verification

`VerifiedRuntimeGeneration` implements the C++ package-integrity check needed
by the [runtime handoff](native-runtime-handoff.md). The newer optional
[owner controller](native-owner-control.md) invokes it; the production server
has not enabled that controller. This checkpoint does not upgrade
the running VS Code backend or enable writing there.

An owner-controlled caller supplies an absolute local generation directory and
the exact manifest digest independently accepted from packaging. Native checks
the complete file inventory and every file hash, required native binaries and
pure standard-library sources, license notices, platform and bridge metadata.
It rejects unexpected files, invalid/duplicate names, path escapes, Windows
aliases, reparse points, hard links and a generation inside the excluded
workspace. Traversal depth, entry counts, manifest size and total byte counts
are bounded. Roots and filenames support UTF-8 through Windows wide APIs.

Native retains read handles for the verified files and directories. Existing
package files cannot be overwritten, deleted or renamed while pinned.
`revalidate()` checks identities, paths, metadata, link counts and the complete
inventory again. Changes in unrelated ancestor-directory contents do not
invalidate a generation; ancestor identities and paths remain checked.
`require_current_server()` compares the actual loaded process image with the
verified `xmind_server.exe`, using OS file identity and bytes. A matching file
name or copied hash alone cannot qualify a different running image.

Hash agreement proves integrity against the supplied digest. It does not prove
the package passed its separate native gate, nor make caller-supplied metadata
an authorization decision. The future owner controller must use the exact
independently verified package, authenticate the operator, validate workspace
and generation preconditions, and perform the native admission/retirement
sequence before replacement startup.

The complete local gate passed **94 native contracts in 183.90 seconds** with
all 618 inputs unchanged. Actual Windows tests covered loaded-image identity,
Unicode roots and filenames, malformed/changed/incomplete inventories,
aliases, exclusion from workspace storage and modification of pinned source.
The operating system prevented the attempted late hard link; the additional
link-count rejection branch is not claimed as separately exercised. Fixture
package metadata and unused package members are synthetic integrity inputs.
The copied C++ test process verifies its actual loaded image, rather than
representing an agent or a working xMind server.

A separate file-only invocation verified all **1,861 inventory files** of the
previously independently accepted `c42a5c4` runtime bundle. That candidate was
not executed or installed. No provider requests or CPython execution occurred.
[Exact verification and retained prior gate](evidence/native-runtime-generation-local.json).

Native owner transport, retirement, target-qualified bootstrap, the explicit
legacy migration path, and installed/rendered file create/edit acceptance are
still required. The existing `TestProj` owner retains its read-only startup
policy and its profile has not been changed by this work.
