# 0.1.61 package byte qualification (2026-10-22)

Source checkpoint: 1c9c0154. This candidate contains the effective selected-agent policy implementation in aa249a1e. Its native executable matches the accepted 17-contract focused gate; current-source full native validation is still running and has observed a real Rust Analyzer definition lookup failure. This package receipt is not full-suite or release acceptance.

A clean runtime was staged without executing it or accessing configuration. The first VSIX packaging attempt used dependencies:false and omitted the Markdown renderer and sanitizer under node_modules. Its archive and original receipt remain local and are unqualified.

The corrected package uses vsce pack() on an isolated extension staging directory, with production dependencies enabled. This avoids executing prepublish against the live source tree. The corrected packager's initial archive audit failed because it expected .vscodeignore inside the VSIX; vsce excludes its packaging control file. The archive was retained unchanged. The separate qualified verifier omits only that packaging control file and verifies every expected client/browser/library file and every staged runtime file by SHA-256 after reading the actual archive.

The corrected archive has 2,446 entries. Its full entry hashes, archive SHA-256 and size are in the qualified receipt. Both required JavaScript libraries and their licenses are included. No package has been installed, no visible editor or browser opened, and no model call or CPython execution occurred during packaging. Native full-suite, managed CLI/browser shared-history checks, installation and rendered VS Code acceptance remain separate required work. The previously observed intermittent SDK fault remains unresolved.
