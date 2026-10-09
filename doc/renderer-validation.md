# Renderer callback validation

The shared renderer's test harness now records unexpected window callback
errors and fails fixture cleanup if any occurred. This closes a gap observed
in the previous hosted job: JSDOM reported `settings.showModal is not a function`
inside click handlers while the test runner still returned success.

JSDOM does not implement native dialog methods. The harness explicitly models
the browser platform's `showModal`/`close` methods, open attribute and close event.
It continues to execute production renderer handlers and inspect their posted
messages. The provider Settings test now checks that opening reached the dialog
API. A deliberately broken dialog callback verifies that the fixture fails;
unexpected callback errors are not merely hidden from the console.

All 215 extension and 39 browser tests pass with this harness, with 43 input files
and 11 freshly generated browser assets unchanged across the complete suites.
These are isolated synthetic DOM/adapter contracts. The harness does not render
an installed editor, execute a native backend, call a model or approve effects.
It does not establish installed-browser or VS Code writing acceptance.

[Exact client evidence](evidence/renderer-callback-validation.json).

Exact revision `14179542b75fb0ef90d02f8b61e8c9f4a07837d2` also passed its
[hosted client gate](https://github.com/xlang-foundation/xMind/actions/runs/37969986513).
The returned archive matched GitHub's advertised digest and size. Its receipt,
43 before/after source hashes, 11 before/after asset hashes and every archived
asset byte were verified. The extension log contains no swallowed dialog API
errors. The scope remains synthetic client/controller validation, with no native
inference or installed writing claim.
[Hosted evidence](evidence/renderer-callback-hosted.json).
