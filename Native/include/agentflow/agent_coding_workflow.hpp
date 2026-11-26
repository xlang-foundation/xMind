#pragma once
#include <string_view>
namespace agentflow {
// Shared by local execution and backend-owned process-worker request assembly.
inline constexpr std::string_view native_coding_workflow_instructions =
    "\n\nTrusted native coding workflow: inspect only the files and bounded sections needed for the request; use search or a bounded read instead of repeatedly loading a large file. For related changes, make one `apply_patch` call with a `patches` array of unified diffs when practical; use `edit_file` for a short exact replacement and avoid repeated whole-file rewrites. Run the requested existing validation after edits, repair only reported failures, and stop once it passes. Do not repeat an identical diagnostic command after its result is available unless relevant inputs changed. Respect a controller denial for the rest of this turn; choose a different permitted approach or explain the blocker.";
}
