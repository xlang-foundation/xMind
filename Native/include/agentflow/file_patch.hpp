#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
#include <stop_token>
#include <vector>

namespace agentflow {
struct PatchInvalid : std::invalid_argument {using std::invalid_argument::invalid_argument;};
struct PatchConflict : std::runtime_error {using std::runtime_error::runtime_error;};
struct PatchCancelled : std::runtime_error {using std::runtime_error::runtime_error;};
enum class PatchAction {add,update,remove};
struct PatchLine {char kind;std::string text;};
struct PatchChunk {std::string anchor;std::vector<PatchLine> lines;bool end_of_file=false;};
struct FilePatch {PatchAction action;std::string path,move_to,content;std::vector<PatchChunk> chunks;};
// Parse/prepare only. Authority, snapshots, approval and effect journaling belong
// to the workspace and executor contracts; these functions never access files.
std::vector<FilePatch> parse_file_patch(std::string_view patch);
std::string prepare_patch_update(std::string_view before,const FilePatch& patch,std::stop_token cancel={});
}
