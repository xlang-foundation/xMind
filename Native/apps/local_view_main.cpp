#include "agentflow/local_profile.hpp"
#include "agentflow/context_records.hpp"
#include "agentflow/owner_process.hpp"
#include "agentflow/runtime_generation.hpp"
#include "agentflow/workspace_tools.hpp"
#include "agentflow/workspace_registry.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {
using Json = nlohmann::json;
struct Unauthorized : std::runtime_error { using std::runtime_error::runtime_error; };
std::atomic<httplib::Server *> active_view = nullptr;
BOOL WINAPI stop_view(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        if (auto *server = active_view.load())
            server->stop();
        return TRUE;
    }
    return FALSE;
}
void require(bool value, const char *message) {
    if (!value)
        throw std::invalid_argument(message);
}
bool allowed_view_delete_route(const std::string &path) {
    constexpr std::string_view prefix = "/v1/sessions/";
    if (!path.starts_with(prefix))
        return false;
    const auto id_start = prefix.size();
    const auto id_end = path.find('/', id_start);
    const auto valid_id = [&](std::size_t start, std::size_t end) {
        if (end <= start)
            return false;
        for (auto i = start; i < end; ++i) {
            const auto c = static_cast<unsigned char>(path[i]);
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-'))
                return false;
        }
        return true;
    };
    if (id_end == std::string::npos)
        return valid_id(id_start, path.size());
    if (!valid_id(id_start, id_end))
        return false;
    constexpr std::string_view grants = "/permission-grants/";
    constexpr std::string_view inbox = "/inbox/";
    const auto suffix = std::string_view(path).substr(id_end);
    for (const auto route : {grants, inbox})
        if (suffix.starts_with(route))
            return valid_id(id_end + route.size(), path.size());
    return false;
}
bool equal(const std::string &left, const std::string &right) {
    std::size_t difference = left.size() ^ right.size();
    for (std::size_t i = 0; i < left.size(); ++i)
        difference |= static_cast<unsigned char>(left[i]) ^
                      (i < right.size() ? static_cast<unsigned char>(right[i]) : 0);
    return difference == 0;
}
std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
void response(httplib::Response &output, int status, const char *detail) {
    output.status = status;
    output.set_content(Json{{"detail", detail}}.dump(), "application/json");
}
std::optional<std::string> choose_workspace_folder() {
    std::optional<std::string> selected;
    std::exception_ptr failure;
    std::thread picker([&] {
        const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(initialized)) {
            failure = std::make_exception_ptr(std::runtime_error("Cannot initialize the native folder picker"));
            return;
        }
        IFileOpenDialog *dialog = nullptr;
        try {
            auto status = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&dialog));
            if (FAILED(status)) throw std::runtime_error("Cannot create the native folder picker");
            DWORD options = 0;
            status = dialog->GetOptions(&options);
            if (SUCCEEDED(status)) status = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            if (SUCCEEDED(status)) status = dialog->SetTitle(L"Choose an xMind workspace folder");
            if (FAILED(status)) throw std::runtime_error("Cannot configure the native folder picker");
            status = dialog->Show(nullptr);
            if (status == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
                selected.reset();
            } else if (FAILED(status)) {
                throw std::runtime_error("The native folder picker failed");
            } else {
                IShellItem *item = nullptr;
                status = dialog->GetResult(&item);
                if (FAILED(status)) throw std::runtime_error("The native folder picker returned no folder");
                PWSTR path = nullptr;
                status = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                item->Release();
                if (FAILED(status) || !path) throw std::runtime_error("The selected workspace path is unavailable");
                const auto free_path = std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)>(path, CoTaskMemFree);
                const auto encoded = std::filesystem::path(path).u8string();
                selected = std::string(reinterpret_cast<const char *>(encoded.data()), encoded.size());
            }
        } catch (...) {
            failure = std::current_exception();
        }
        if (dialog) dialog->Release();
        CoUninitialize();
    });
    picker.join();
    if (failure) std::rethrow_exception(failure);
    return selected;
}
std::string verify_client_image() {
    std::wstring image(32768, L'\0');
    const auto used = GetModuleFileNameW(nullptr, image.data(), static_cast<DWORD>(image.size()));
    require(used && used < image.size(), "Cannot qualify native view image");
    image.resize(used);
    const auto root = std::filesystem::path(image).parent_path();
    const auto bytes = root.u8string();
    const std::string directory(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    std::ifstream input(root / L"native-runtime-manifest.json", std::ios::binary);
    require(static_cast<bool>(input), "Native view requires a verified package");
    require(std::filesystem::file_size(root / L"native-runtime-manifest.json") <= 4 * 1024 * 1024,
            "Native view manifest exceeds its limit");
    std::string manifest((std::istreambuf_iterator<char>(input)), {});
    require(manifest.size() <= 4 * 1024 * 1024, "Native view manifest exceeds its limit");
    // The retained backend and this client must each qualify their own image.
    agentflow::VerifiedRuntimeGeneration generation(directory, agentflow::context_digest(manifest));
    generation.require_loaded_server_image();
    return directory;
}
} // namespace

int run_local_view(int argc, char **argv) {
    agentflow::LocalProfileOptions options;
    std::string trusted_provider_config;
    std::string ready;
    try {
        std::map<std::string, bool> seen;
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            require(seen.emplace(key, true).second, "Duplicate native view option");
            if (key == "--read-only") {
                options.approved_edits = false;
                continue;
            }
            if (key == "--workspace-edits") {
                require(i + 1 < argc && std::string_view(argv[i + 1]) == "approved",
                        "--workspace-edits requires the value approved");
                ++i;
                options.approved_edits = true;
                continue;
            }
            if (key == "--upgrade-backend") {
                options.upgrade_backend = true;
                continue;
            }
            require(i + 1 < argc && *argv[i + 1], "Native view option requires a value");
            const std::string value = argv[++i];
            if (key == "--workspace")
                options.workspace = value;
            else if (key == "--profile-root")
                options.profile_root = value;
            else if (key == "--config" || key == "--trusted-provider-config") {
                require(trusted_provider_config.empty(), "Duplicate trusted provider configuration path");
                trusted_provider_config = value;
            }
            else if (key == "--graphs-config")
                options.graphs_config = value;
            else if (key == "--python-tools-config")
                options.python_tools_config = value;
            else if (key == "--language-server-config")
                options.language_server_config = value;
            else if (key == "--process-config")
                options.process_config = value;
            else if (key == "--ready-file")
                ready = value;
            else
                throw std::invalid_argument("Unknown native view option");
        }
        require(!ready.empty(), "Native view requires a private ready-file location");
        const auto *configured = std::getenv("XMIND_VIEW_TOKEN");
        require(configured != nullptr, "Native view access authentication is unavailable");
        std::string access = configured;
        require(access.size() == 64 && access.find_first_not_of("0123456789abcdef") == std::string::npos,
                "Invalid native view access authentication");
        const std::string authorization = "Bearer " + access;
        SecureZeroMemory(access.data(), access.size());
        SetEnvironmentVariableW(L"XMIND_VIEW_TOKEN", nullptr);
        if (options.workspace.empty()) {
            const auto path = std::filesystem::current_path().u8string();
            options.workspace.assign(reinterpret_cast<const char *>(path.data()), path.size());
        }
        // Each editor/browser adapter has a verified package. Bring an older
        // managed profile forward before exposing that package's API surface.
        // If active native work prevents a safe handoff, keep using the healthy
        // current owner and retry on a later view launch.
        options.upgrade_backend = true;
        options.defer_busy_upgrade = true;
        agentflow::validate_local_view_ready(ready, options.workspace);
        require(trusted_provider_config.empty() ||
                    std::filesystem::path(trusted_provider_config).is_absolute(),
                "Trusted provider configuration path must be absolute");
        // Provider YAML is imported by the adapter through a host-authenticated
        // API call. It is not part of the backend's profile launch identity.
        options.provider_config.clear();
        const auto client_package_root = verify_client_image();
        auto connection = std::make_shared<agentflow::LocalProfileConnection>(
            agentflow::connect_local_profile(options));
        const auto profile_root =
            std::filesystem::path(connection->directory).parent_path().u8string();
        const std::string profile_root_utf8(reinterpret_cast<const char *>(profile_root.data()),
                                            profile_root.size());
        const auto profile_registry_path = std::filesystem::path(profile_root) / L"workspaces.sqlite";
        const auto profile_registry_bytes = profile_registry_path.u8string();
        const std::string profile_registry_file(reinterpret_cast<const char *>(profile_registry_bytes.data()),
                                                profile_registry_bytes.size());
        auto workspace_registry = std::make_shared<agentflow::WorkspaceRegistry>(
            profile_registry_file,
            std::vector<std::string>{client_package_root + "/modules", client_package_root + "/stdlib"},
            profile_root_utf8);
        const auto selected_workspace_id = agentflow::WorkspaceTools(connection->workspace).identity();
        std::mutex browser_state_mutex;
        std::mutex browser_switch_mutex;
        std::map<std::string, std::shared_ptr<agentflow::LocalProfileConnection>> workspaces;
        struct SessionBinding { std::string workspace_id, browser_origin; };
        std::map<std::string, SessionBinding> browser_sessions;
        workspaces.emplace(selected_workspace_id, connection);
        httplib::Server server;
        server.new_task_queue = [] { return new httplib::ThreadPool(4, 4, 32); };
        server.set_payload_max_length(1024 * 1024);
        server.set_read_timeout(5, 0);
        server.set_write_timeout(5, 0);
        server.set_keep_alive_timeout(1);
        const int port = server.bind_to_any_port("127.0.0.1");
        require(port > 0, "Cannot bind native view adapter");
        const auto origin = "http://127.0.0.1:" + std::to_string(port);
        const std::string master(reinterpret_cast<const char *>(connection->auth.view().data()),
                                 connection->auth.view().size());
        std::mutex credential_mutex;
        std::string credential;
        std::int64_t expires = 0;
        const auto configure = [](httplib::Client &client) {
            client.set_connection_timeout(2, 0);
            client.set_read_timeout(15, 0);
            client.set_write_timeout(5, 0);
            client.set_follow_location(false);
        };
        // Reapply the trusted local YAML whenever this view attaches a workspace
        // profile. The native registry merges supplied entries, so profiles
        // already present do not prevent newly added or rotated keys from loading.
        const auto import_shared_provider_config = [&](const std::shared_ptr<agentflow::LocalProfileConnection> &target) {
            if (trusted_provider_config.empty())
                return;
            httplib::Client client("127.0.0.1", target->port);
            configure(client);
            const auto secret = target->auth.view();
            std::string authorization = "Bearer " +
                std::string(reinterpret_cast<const char *>(secret.data()), secret.size());
            const httplib::Headers headers{{"Authorization", authorization}};
            const auto current = client.Get("/v1/provider/profiles", headers);
            if (!authorization.empty())
                SecureZeroMemory(authorization.data(), authorization.size());
            // Older retained runtimes do not implement provider-profile import.
            // Let workspace selection proceed; the matching native package will
            // apply this shared local configuration when that profile is opened.
            if (!current || current->status == 404)
                return;
            require(current->status == 200, "Native provider settings are unavailable");
            auto metadata = Json::parse(current->body);
            require(metadata.is_object() && metadata.contains("revision") &&
                        metadata["revision"].is_number_integer() && metadata["revision"] >= 0 &&
                        metadata.contains("profiles") && metadata["profiles"].is_array(),
                    "Native provider settings are invalid");
            const auto config_path = std::filesystem::absolute(trusted_provider_config).u8string();
            const std::string config(reinterpret_cast<const char *>(config_path.data()), config_path.size());
            auto import = client.Post("/v1/provider/configuration/import", headers,
                Json{{"path", config}, {"expected_revision", metadata["revision"]}}.dump(),
                "application/json");
            if (!import)
                throw std::runtime_error("Native provider settings could not be applied");
            if (import->status == 409) {
                const auto refreshed = client.Get("/v1/provider/profiles", headers);
                require(refreshed && refreshed->status == 200,
                        "Native provider settings could not be refreshed");
                const auto latest = Json::parse(refreshed->body);
                require(latest.is_object() && latest.contains("profiles") && latest["profiles"].is_array(),
                        "Native provider settings are invalid");
                require(latest.contains("revision") && latest["revision"].is_number_integer() &&
                            latest["revision"] >= 0,
                        "Native provider settings are invalid");
                import = client.Post("/v1/provider/configuration/import", headers,
                    Json{{"path", config}, {"expected_revision", latest["revision"]}}.dump(),
                    "application/json");
                if (!import)
                    throw std::runtime_error("Native provider settings could not be applied");
                if (import->status == 409)
                    throw std::runtime_error("Native provider settings changed during workspace connection");
            }
            require(import->status == 200, "Shared provider configuration was rejected");
        };
        const auto issue = [&] {
            httplib::Client client("127.0.0.1", connection->port);
            configure(client);
            const auto lease =
                Json{{"origin", origin},
                     {"process_id", GetCurrentProcessId()},
                     {"process_birth", agentflow::inspect_owner_process_birth(GetCurrentProcessId())}}
                    .dump();
            const auto result = client.Post("/v1/view-sessions", {{"Authorization", "Bearer " + master}},
                                            lease, "application/json");
            require(result && result->status == 200, "Native view access could not be issued");
            const auto value = Json::parse(result->body);
            const auto replacement = value.at("credential").get<std::string>();
            require(replacement.size() == 129 && replacement[64] == '.', "Invalid native view session");
            if (!credential.empty()) {
                client.Post("/v1/view-sessions/revoke",
                            {{"Authorization", "View " + credential}, {"X-XMind-View-Origin", origin}}, "{}",
                            "application/json");
            }
            credential = replacement;
            expires = value.at("expires_unix_ms").get<std::int64_t>();
            require(expires > now(), "Native view session already expired");
        };
        const auto browser_workspace = [&](const std::string &supplied, const std::string &browser_origin) {
            require(supplied.starts_with("View ") && supplied.size() == 134 &&
                        supplied[69] == '.' && supplied.substr(5, 64).find_first_not_of("0123456789abcdef") == std::string::npos &&
                        supplied.substr(70).find_first_not_of("0123456789abcdef") == std::string::npos,
                    "Invalid browser view session");
            const auto token = supplied.substr(5);
            std::lock_guard lock(browser_state_mutex);
            const auto binding = browser_sessions.find(token);
            if (binding == browser_sessions.end() || binding->second.browser_origin != browser_origin)
                throw Unauthorized("Browser view session is no longer active");
            const auto target = workspaces.find(binding->second.workspace_id);
            if (target == workspaces.end())
                throw Unauthorized("Browser workspace is no longer available");
            return std::pair<std::string, std::shared_ptr<agentflow::LocalProfileConnection>>{
                binding->second.workspace_id, target->second};
        };
        const auto validate_browser_session = [&](const std::string &supplied,
                                                  const std::shared_ptr<agentflow::LocalProfileConnection> &target,
                                                  const std::string &browser_origin) {
            httplib::Client client("127.0.0.1", target->port);
            configure(client);
            const auto current = client.Post("/v1/view-sessions/current",
                                             {{"Authorization", supplied},
                                              {"X-XMind-View-Origin", browser_origin}}, "{}", "application/json");
            return current && current->status == 200;
        };
        const auto issue_browser_session = [&](const std::shared_ptr<agentflow::LocalProfileConnection> &target,
                                               const std::string &browser_origin) {
            httplib::Client client("127.0.0.1", target->port);
            configure(client);
            const auto lease =
                Json{{"origin", browser_origin},
                     {"process_id", GetCurrentProcessId()},
                     {"process_birth", agentflow::inspect_owner_process_birth(GetCurrentProcessId())}}
                    .dump();
            auto auth = std::string(reinterpret_cast<const char *>(target->auth.view().data()),
                                    target->auth.view().size());
            const auto issued = client.Post("/v1/view-sessions", {{"Authorization", "Bearer " + auth}},
                                            lease, "application/json");
            if (!auth.empty())
                SecureZeroMemory(auth.data(), auth.size());
            require(issued && issued->status == 200, "Native workspace rejected browser access");
            const auto value = Json::parse(issued->body);
            const auto token = value.at("credential").get<std::string>();
            require(token.size() == 129 && token[64] == '.' &&
                        token.find_first_not_of("0123456789abcdef.") == std::string::npos &&
                        value.at("expires_unix_ms").get<std::int64_t>() > now() &&
                        value.at("max_age_seconds").get<std::int64_t>() > 0 &&
                        value.at("max_age_seconds").get<std::int64_t>() <= 28800,
                    "Native workspace returned an invalid browser session");
            return value;
        };
        const auto revoke_browser_session = [&](const std::string &token,
                                                const std::shared_ptr<agentflow::LocalProfileConnection> &target,
                                                const std::string &browser_origin) {
            httplib::Client client("127.0.0.1", target->port);
            configure(client);
            const auto revoked = client.Post("/v1/view-sessions/revoke",
                                             {{"Authorization", "View " + token},
                                              {"X-XMind-View-Origin", browser_origin}},
                                             "{}", "application/json");
            return revoked && (revoked->status == 200 || revoked->status == 401);
        };
        import_shared_provider_config(connection);
        issue();
        server.set_pre_routing_handler([&](const httplib::Request &request, httplib::Response &output) {
            output.set_header("Cache-Control", "no-store");
            output.set_header("X-Content-Type-Options", "nosniff");
            if (request.get_header_value_count("Host") != 1 ||
                request.get_header_value("Host") != "127.0.0.1:" + std::to_string(port)) {
                response(output, 400, "Invalid view host");
            } else if (request.has_header("Origin")) {
                response(output, 403, "Browser origins require the browser access adapter");
            } else if (!request.path.starts_with("/v1/") ||
                       (request.method != "GET" && request.method != "POST" &&
                        !(request.method == "DELETE" && allowed_view_delete_route(request.path)))) {
                response(output, 404, "View route not found");
            } else if (request.get_header_value_count("Authorization") != 1) {
                response(output, 401, "View authentication required");
            } else {
                const auto supplied = request.get_header_value("Authorization");
                const bool host = equal(supplied, authorization);
                const bool browser = supplied.starts_with("View ") &&
                                     request.get_header_value_count("X-XMind-View-Origin") == 1;
                if (!host && !browser) {
                    response(output, 401, "View authentication required");
                } else {
                    // Admission runs before cpp-httplib reads the request body.
                    // Only the regular route handler may forward that body.
                    return httplib::Server::HandlerResponse::Unhandled;
                }
            }
            return httplib::Server::HandlerResponse::Handled;
        });
        const auto forward = [&](const httplib::Request &request, httplib::Response &output) {
            const auto supplied = request.get_header_value("Authorization");
            const bool host = equal(supplied, authorization);
            try {
                httplib::Headers headers;
                std::string body = request.body;
                std::string requested_browser_origin;
                const std::string browser_origin = request.get_header_value("X-XMind-View-Origin");
                const bool folder_listing = request.path == "/v1/workspace/folders/files";
                const bool folder_preview = request.path == "/v1/workspace/folders/file";
                const bool folder_find = request.path == "/v1/workspace/folders/find";
                if ((folder_listing || folder_preview || folder_find) && request.method == "GET") {
                    if (host || request.get_header_value_count("X-XMind-View-Origin") != 1) {
                        response(output, 400, "Workspace folder browsing requires a browser view session");
                        return;
                    }
                    const auto [workspace_id, target] = browser_workspace(supplied, browser_origin);
                    if (!validate_browser_session(supplied, target, browser_origin)) {
                        response(output, 401, "Browser view session expired or was revoked");
                        return;
                    }
                    bool has_folder_id = false, has_path = false, has_query = false, has_type = false,
                         has_limit = false, has_hidden = false, has_respect_ignore = false;
                    for (const auto &[key, value] : request.params) {
                        if (key == "folder_id") {
                            require(!has_folder_id && !value.empty() && value.size() <= 256,
                                    "Workspace folder ID must appear once");
                            has_folder_id = true;
                        } else if (key == "path") {
                            require(!has_path && !value.empty() && value.size() <= 32768,
                                    "Workspace folder path must appear at most once and fit limits");
                            has_path = true;
                        } else if (folder_find && key == "query") {
                            require(!has_query && !value.empty() && value.size() <= 4096,
                                    "Workspace folder filename query must appear once and fit limits");
                            has_query = true;
                        } else if (folder_find && key == "type") {
                            require(!has_type && (value == "file" || value == "directory"),
                                    "Workspace folder filename type must be file or directory");
                            has_type = true;
                        } else if (folder_find && key == "limit") {
                            require(!has_limit && !value.empty(), "Workspace folder filename limit must appear once");
                            has_limit = true;
                        } else if (folder_find && key == "hidden") {
                            require(!has_hidden && (value == "true" || value == "false"),
                                    "Workspace folder hidden option must be true or false");
                            has_hidden = true;
                        } else if (folder_find && key == "respect_ignore") {
                            require(!has_respect_ignore && (value == "true" || value == "false"),
                                    "Workspace folder ignore option must be true or false");
                            has_respect_ignore = true;
                        } else {
                            throw std::invalid_argument("Unexpected workspace folder query parameter");
                        }
                    }
                    require(has_folder_id && request.target.find('?') != std::string::npos,
                            "Workspace folder browsing requires one folder ID");
                    const auto folder_id = request.get_param_value("folder_id");
                    const auto relative_path = has_path ? request.get_param_value("path") : ".";
                    require(!folder_find || has_query, "Workspace folder filename search requires one query");
                    if (folder_preview)
                        require(has_path && relative_path != ".", "Workspace file path is required");
                    const auto group = workspace_registry->get(workspace_id);
                    const auto folder = std::find_if(group.folders.begin(), group.folders.end(),
                        [&](const auto &member) { return member.folder_id == folder_id; });
                    if (folder == group.folders.end()) {
                        response(output, 404, "Folder is not a member of the selected workspace");
                        return;
                    }
                    agentflow::WorkspaceTools verified_folder(folder->root);
                    require(verified_folder.identity() == folder->identity &&
                                verified_folder.root_path() == folder->root,
                            "Workspace folder native identity changed since registration");
                    if (folder_listing) {
                        const auto listing = verified_folder.browse_files(relative_path);
                        Json entries = Json::array();
                        for (const auto &entry : listing.entries)
                            entries.push_back({{"name", entry.name}, {"kind", entry.kind}});
                        output.status = 200;
                        output.set_content(Json{{"workspace_id", workspace_id}, {"revision", group.revision},
                                                {"folder_id", folder_id}, {"path", relative_path},
                                                {"entries", std::move(entries)}, {"truncated", listing.truncated}}.dump(),
                                           "application/json");
                    } else if (folder_preview) {
                        const auto file = verified_folder.browse_file(relative_path);
                        output.status = 200;
                        output.set_content(Json{{"workspace_id", workspace_id}, {"revision", group.revision},
                                                {"folder_id", folder_id}, {"path", file.path},
                                                {"content", file.content}}.dump(),
                                           "application/json");
                    } else {
                        std::size_t limit = 100;
                        if (has_limit) {
                            const auto value = request.get_param_value("limit");
                            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), limit);
                            require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
                                        limit > 0 && limit <= 1000,
                                    "Workspace folder filename limit must be between 1 and 1000");
                        }
                        const auto boolean = [&](const char *key, bool fallback) {
                            if (!request.has_param(key)) return fallback;
                            return request.get_param_value(key) == "true";
                        };
                        const auto type = has_type ? request.get_param_value("type") : std::string("file");
                        const auto found = verified_folder.find_files(request.get_param_value("query"), relative_path,
                            limit, {}, boolean("hidden", false), boolean("respect_ignore", true), type);
                        Json entries = Json::array();
                        for (const auto &entry : found.entries)
                            entries.push_back({{"path", entry.path}, {"type", entry.type}});
                        output.status = 200;
                        output.set_content(Json{{"workspace_id", workspace_id}, {"revision", group.revision},
                                                {"folder_id", folder_id}, {"path", relative_path},
                                                {"entries", std::move(entries)}, {"truncated", found.truncated},
                                                {"limits", found.limits}}.dump(),
                                           "application/json");
                    }
                    return;
                }
                if (request.path == "/v1/workspace/membership" && request.method == "GET") {
                    if (!host || request.target != request.path) {
                        response(output, 400, "Workspace membership requires the local backend owner");
                        return;
                    }
                    try {
                        const agentflow::WorkspaceTools primary(connection->workspace);
                        const auto group = workspace_registry->get(primary.identity());
                        require(!group.folders.empty() && group.primary_folder_id == primary.identity() &&
                                    group.folders.front().root == primary.root_path(),
                                "Registered workspace primary folder differs from the connected backend");
                        Json folders = Json::array();
                        for (const auto &folder : group.folders)
                            folders.push_back({{"folder_id", folder.folder_id}, {"name", folder.name},
                                               {"root", folder.root}, {"ordinal", folder.ordinal}});
                        output.status = 200;
                        output.set_content(Json{{"workspace_id", group.workspace_id}, {"name", group.name},
                                                {"root", group.folders.front().root}, {"revision", group.revision},
                                                {"primary_folder_id", group.primary_folder_id},
                                                {"folders", std::move(folders)}}.dump(),
                                           "application/json");
                    } catch (const std::out_of_range &error) {
                        response(output, 404, error.what());
                    } catch (const std::invalid_argument &error) {
                        response(output, 409, error.what());
                    }
                    return;
                }
                if (request.path == "/v1/workspaces" && request.method == "GET") {
                    if (host || request.target != request.path ||
                        request.get_header_value_count("X-XMind-View-Origin") != 1) {
                        response(output, 400, "Invalid workspace catalogue request");
                        return;
                    }
                    const auto [workspace_id, target] = browser_workspace(supplied, browser_origin);
                    if (!validate_browser_session(supplied, target, browser_origin)) {
                        response(output, 401, "Browser view session expired or was revoked");
                        return;
                    }
                    Json entries = Json::array();
                    for (const auto &profile : agentflow::list_local_workspace_profiles(profile_root_utf8)) {
                        const auto group = workspace_registry->create(profile.root);
                        Json folders = Json::array();
                        for (const auto &folder : group.folders)
                            folders.push_back({{"folder_id", folder.folder_id}, {"name", folder.name},
                                               {"root", folder.root}, {"ordinal", folder.ordinal}});
                        entries.push_back({{"workspace_id", group.workspace_id}, {"root", group.folders.front().root},
                                           {"name", group.name}, {"revision", group.revision},
                                           {"primary_folder_id", group.primary_folder_id}, {"folders", std::move(folders)}});
                    }
                    output.status = 200;
                    output.set_content(Json{{"can_add", true},
                                            {"selected_workspace_id", workspace_id},
                                            {"workspaces", std::move(entries)}}.dump(),
                                       "application/json");
                    return;
                }
                if (request.path == "/v1/workspaces/pick" && request.method == "POST") {
                    if (host || request.target != request.path ||
                        request.get_header_value_count("X-XMind-View-Origin") != 1 ||
                        request.get_header_value("Content-Type") != "application/json" ||
                        request.body.size() > 16) {
                        response(output, 400, "Invalid workspace picker request");
                        return;
                    }
                    const auto [current_id, current] = browser_workspace(supplied, browser_origin);
                    if (!validate_browser_session(supplied, current, browser_origin)) {
                        response(output, 401, "Browser view session expired or was revoked");
                        return;
                    }
                    try {
                        const auto request_body = Json::parse(body);
                        require(request_body.is_object() && request_body.empty(), "Workspace picker accepts an empty object");
                    } catch (const std::exception &) {
                        response(output, 400, "Workspace picker accepts an empty object");
                        return;
                    }
                    const auto chosen = choose_workspace_folder();
                    std::lock_guard switch_lock(browser_switch_mutex);
                    const auto [latest_id, latest] = browser_workspace(supplied, browser_origin);
                    if (latest_id != current_id || !validate_browser_session(supplied, latest, browser_origin)) {
                        response(output, 409, "Browser workspace changed while the folder picker was open");
                        return;
                    }
                    output.status = 200;
                    output.set_content(Json{{"cancelled", !chosen.has_value()},
                                            {"root", chosen ? Json(*chosen) : Json(nullptr)}}.dump(),
                                       "application/json");
                    return;
                }
                if (request.path == "/v1/workspaces/add" && request.method == "POST") {
                    if (request.target != request.path ||
                        (!host && request.get_header_value_count("X-XMind-View-Origin") != 1) ||
                        request.get_header_value("Content-Type") != "application/json") {
                        response(output, 400, "Invalid workspace folder attachment request");
                        return;
                    }
                    const auto current_id = host ? agentflow::WorkspaceTools(connection->workspace).identity() :
                        browser_workspace(supplied, browser_origin).first;
                    if (!host) {
                        const auto [latest_id, current] = browser_workspace(supplied, browser_origin);
                        if (latest_id != current_id || !validate_browser_session(supplied, current, browser_origin)) {
                            response(output, 401, "Browser view session expired or was revoked");
                            return;
                        }
                    }
                    std::set<std::string> keys;
                    const auto input = Json::parse(body, [&](int depth, Json::parse_event_t event, Json &value) {
                        require(depth <= 2, "Invalid workspace folder attachment");
                        if (event == Json::parse_event_t::key)
                            require(keys.insert(value.get<std::string>()).second,
                                    "Duplicate workspace folder attachment field");
                        return true;
                    });
                    require(input.is_object() && input.size() == 2 && input.contains("workspace_id") &&
                                input.at("workspace_id").is_string() && input.at("workspace_id") == current_id &&
                                input.contains("root") && input.at("root").is_string(),
                            "Attach a folder to the currently selected workspace");
                    const auto requested_root = input.at("root").get<std::string>();
                    const bool drive_path = requested_root.size() >= 3 &&
                                            ((requested_root[0] >= 'A' && requested_root[0] <= 'Z') ||
                                             (requested_root[0] >= 'a' && requested_root[0] <= 'z')) &&
                                            requested_root[1] == ':' &&
                                            (requested_root[2] == '\\' || requested_root[2] == '/');
                    const bool unc_path = requested_root.starts_with("\\\\");
                    require(!requested_root.empty() && requested_root.size() <= 32760 &&
                                std::none_of(requested_root.begin(), requested_root.end(), [](unsigned char c) { return c < 32; }) &&
                                (drive_path || unc_path),
                            "Workspace folder must be an absolute local or UNC path");
                    try {
                        const auto group = workspace_registry->add_folder(current_id, requested_root);
                        Json folders = Json::array();
                        for (const auto &folder : group.folders)
                            folders.push_back({{"folder_id", folder.folder_id}, {"name", folder.name},
                                               {"root", folder.root}, {"ordinal", folder.ordinal}});
                        output.status = 200;
                        output.set_content(Json{{"workspace_id", group.workspace_id}, {"name", group.name},
                                                {"root", group.folders.front().root}, {"revision", group.revision},
                                                {"primary_folder_id", group.primary_folder_id}, {"folders", std::move(folders)}}.dump(),
                                           "application/json");
                    } catch (const std::invalid_argument &error) {
                        response(output, 400, error.what());
                    } catch (const std::out_of_range &error) {
                        response(output, 404, error.what());
                    }
                    return;
                }
                if (request.path == "/v1/workspaces/select" && request.method == "POST") {
                    if (host || request.target != request.path ||
                        request.get_header_value_count("X-XMind-View-Origin") != 1 ||
                        request.get_header_value("Content-Type") != "application/json") {
                        response(output, 400, "Invalid workspace selection request");
                        return;
                    }
                    std::lock_guard switch_lock(browser_switch_mutex);
                    const auto [current_id, current] = browser_workspace(supplied, browser_origin);
                    if (!validate_browser_session(supplied, current, browser_origin)) {
                        response(output, 401, "Browser view session expired or was revoked");
                        return;
                    }
                    std::set<std::string> keys;
                    const auto selection = Json::parse(body, [&](int depth, Json::parse_event_t event, Json &value) {
                        require(depth <= 2, "Invalid workspace selection");
                        if (event == Json::parse_event_t::key)
                            require(keys.insert(value.get<std::string>()).second, "Duplicate workspace selection field");
                        return true;
                    });
                    require(selection.is_object() && selection.size() == 1 &&
                                selection.contains("workspace_id") && selection.at("workspace_id").is_string() &&
                                selection.at("workspace_id").get_ref<const std::string &>().size() <= 256,
                            "Workspace selection requires one opaque workspace ID");
                    const auto requested_id = selection.at("workspace_id").get<std::string>();
                    const auto catalogue = agentflow::list_local_workspace_profiles(profile_root_utf8);
                    const auto chosen = std::find_if(catalogue.begin(), catalogue.end(),
                                                     [&](const auto &item) { return item.workspace_id == requested_id; });
                    if (chosen == catalogue.end()) {
                        response(output, 404, "Workspace is not in the native local profile catalogue");
                        return;
                    }
                    std::shared_ptr<agentflow::LocalProfileConnection> target;
                    {
                        std::lock_guard lock(browser_state_mutex);
                        const auto found = workspaces.find(requested_id);
                        if (found != workspaces.end())
                            target = found->second;
                        else if (workspaces.size() >= 32) {
                            response(output, 429, "Too many workspaces are attached to this browser view");
                            return;
                        }
                    }
                    if (!target) {
                        auto selected = options;
                        selected.workspace = chosen->root;
                        selected.profile_root = profile_root_utf8;
                        selected.provider_config.clear();
                        selected.graphs_config.clear();
                        selected.approved_edits.reset();
                        selected.require_existing_profile = true;
                        target = std::make_shared<agentflow::LocalProfileConnection>(
                            agentflow::connect_local_profile(selected));
                        import_shared_provider_config(target);
                    }
                    require(agentflow::WorkspaceTools(target->workspace).identity() == requested_id,
                            "Native profile resolved to a different workspace");
                    const auto issued = issue_browser_session(target, browser_origin);
                    const auto replacement = issued.at("credential").get<std::string>();
                    httplib::Client metadata_client("127.0.0.1", target->port);
                    configure(metadata_client);
                    const auto metadata = metadata_client.Get(
                        "/v1/workspace", httplib::Headers{{"Authorization", "View " + replacement},
                                                           {"X-XMind-View-Origin", browser_origin}});
                    if (!metadata || metadata->status != 200) {
                        revoke_browser_session(replacement, target, browser_origin);
                        response(output, 503, "Selected native workspace is unavailable");
                        return;
                    }
                    {
                        std::lock_guard lock(browser_state_mutex);
                        const auto previous = browser_sessions.find(supplied.substr(5));
                        if (previous == browser_sessions.end() || previous->second.workspace_id != current_id ||
                            previous->second.browser_origin != browser_origin) {
                            revoke_browser_session(replacement, target, browser_origin);
                            response(output, 409, "Browser workspace changed during selection");
                            return;
                        }
                        if (!revoke_browser_session(supplied.substr(5), current, browser_origin)) {
                            revoke_browser_session(replacement, target, browser_origin);
                            response(output, 503, "Previous browser workspace could not be detached");
                            return;
                        }
                        workspaces.emplace(requested_id, target);
                        browser_sessions.erase(previous);
                        browser_sessions.emplace(replacement,
                                                 SessionBinding{requested_id, browser_origin});
                    }
                    output.status = 200;
                    output.set_content(Json{{"credential", replacement},
                                            {"expires_unix_ms", issued.at("expires_unix_ms")},
                                            {"max_age_seconds", issued.at("max_age_seconds")},
                                            {"workspace", Json::parse(metadata->body)}}.dump(),
                                       "application/json");
                    return;
                }
                if (request.path == "/v1/workspaces/create" && request.method == "POST") {
                    if (host || request.target != request.path ||
                        request.get_header_value_count("X-XMind-View-Origin") != 1 ||
                        request.get_header_value("Content-Type") != "application/json") {
                        response(output, 400, "Invalid workspace creation request");
                        return;
                    }
                    std::lock_guard switch_lock(browser_switch_mutex);
                    const auto [current_id, current] = browser_workspace(supplied, browser_origin);
                    if (!validate_browser_session(supplied, current, browser_origin)) {
                        response(output, 401, "Browser view session expired or was revoked");
                        return;
                    }
                    std::set<std::string> keys;
                    const auto selection = Json::parse(body, [&](int depth, Json::parse_event_t event, Json &value) {
                        require(depth <= 2, "Invalid workspace creation");
                        if (event == Json::parse_event_t::key)
                            require(keys.insert(value.get<std::string>()).second, "Duplicate workspace creation field");
                        return true;
                    });
                    require(selection.is_object() && selection.size() == 1 && selection.contains("root") &&
                                selection.at("root").is_string(),
                            "Workspace creation requires one absolute folder path");
                    const auto requested_root = selection.at("root").get<std::string>();
                    const bool drive_path = requested_root.size() >= 3 &&
                                            ((requested_root[0] >= 'A' && requested_root[0] <= 'Z') ||
                                             (requested_root[0] >= 'a' && requested_root[0] <= 'z')) &&
                                            requested_root[1] == ':' &&
                                            (requested_root[2] == '\\' || requested_root[2] == '/');
                    const bool unc_path = requested_root.starts_with("\\\\");
                    require(!requested_root.empty() && requested_root.size() <= 32760 &&
                                std::none_of(requested_root.begin(), requested_root.end(), [](unsigned char c) { return c < 32; }) &&
                                (drive_path || unc_path),
                            "Workspace folder must be an absolute local or UNC path");
                    std::unique_ptr<agentflow::WorkspaceTools> requested_workspace;
                    try {
                        requested_workspace = std::make_unique<agentflow::WorkspaceTools>(requested_root);
                    } catch (...) {
                        response(output, 400,
                                 "Choose an existing accessible folder outside xMind's private profile storage");
                        return;
                    }
                    const auto requested_id = requested_workspace->identity();
                    const auto canonical_root = requested_workspace->root_path();
                    const auto group = workspace_registry->create(canonical_root);
                    require(group.workspace_id == requested_id && group.primary_folder_id == requested_id &&
                                !group.folders.empty() && group.folders.front().root == canonical_root,
                            "Native workspace registry resolved a different primary folder");
                    std::shared_ptr<agentflow::LocalProfileConnection> target;
                    {
                        std::lock_guard lock(browser_state_mutex);
                        const auto existing = workspaces.find(requested_id);
                        if (existing != workspaces.end())
                            target = existing->second;
                        else if (workspaces.size() >= 32) {
                            response(output, 429, "Too many workspaces are attached to this browser view");
                            return;
                        }
                    }
                    if (!target) {
                    auto selected = options;
                    selected.workspace = canonical_root;
                    selected.profile_root = profile_root_utf8;
                    selected.provider_config.clear();
                    selected.graphs_config.clear();
                    selected.approved_edits.reset();
                    selected.require_existing_profile = false;
                    try {
                        target = std::make_shared<agentflow::LocalProfileConnection>(
                            agentflow::connect_local_profile(selected));
                        import_shared_provider_config(target);
                    } catch (const std::invalid_argument &) {
                        response(output, 400,
                                 "Choose an existing accessible folder outside xMind's private profile storage");
                        return;
                    }
                    }
                    require(agentflow::WorkspaceTools(target->workspace).identity() == requested_id,
                            "Native workspace identity changed during creation");
                    const auto issued = issue_browser_session(target, browser_origin);
                    const auto replacement = issued.at("credential").get<std::string>();
                    httplib::Client metadata_client("127.0.0.1", target->port);
                    configure(metadata_client);
                    const auto metadata = metadata_client.Get(
                        "/v1/workspace", httplib::Headers{{"Authorization", "View " + replacement},
                                                           {"X-XMind-View-Origin", browser_origin}});
                    if (!metadata || metadata->status != 200 ||
                        Json::parse(metadata->body).value("workspace_id", std::string{}) != requested_id) {
                        revoke_browser_session(replacement, target, browser_origin);
                        response(output, 503, "Added native workspace is unavailable");
                        return;
                    }
                    {
                        std::lock_guard lock(browser_state_mutex);
                        const auto previous = browser_sessions.find(supplied.substr(5));
                        if (previous == browser_sessions.end() || previous->second.workspace_id != current_id ||
                            previous->second.browser_origin != browser_origin) {
                            revoke_browser_session(replacement, target, browser_origin);
                            response(output, 409, "Browser workspace changed during creation");
                            return;
                        }
                        if (!revoke_browser_session(supplied.substr(5), current, browser_origin)) {
                            revoke_browser_session(replacement, target, browser_origin);
                            response(output, 503, "Previous browser workspace could not be detached");
                            return;
                        }
                        workspaces.insert_or_assign(requested_id, target);
                        browser_sessions.erase(previous);
                        browser_sessions.emplace(replacement,
                                                 SessionBinding{requested_id, browser_origin});
                    }
                    output.status = 200;
                    output.set_content(Json{{"credential", replacement},
                                            {"expires_unix_ms", issued.at("expires_unix_ms")},
                                            {"max_age_seconds", issued.at("max_age_seconds")},
                                            {"workspace", Json::parse(metadata->body)}}.dump(),
                                       "application/json");
                    return;
                }
                if (host && request.path == "/v1/view-sessions" && request.method == "POST") {
                    // A trusted host may open the existing browser adapter.
                    std::set<std::string> keys;
                    const auto input =
                        Json::parse(body, [&](int depth, Json::parse_event_t event, Json &value) {
                            require(depth <= 2, "Invalid browser session request");
                            if (event == Json::parse_event_t::key)
                                require(keys.insert(value.get<std::string>()).second,
                                        "Duplicate browser session field");
                            return true;
                        });
                    require(input.is_object() && input.size() == 1 && input.contains("origin") &&
                                input.at("origin").is_string(),
                            "Browser session requires only its origin");
                    auto lease = input;
                    requested_browser_origin = input.at("origin").get<std::string>();
                    lease["process_id"] = GetCurrentProcessId();
                    lease["process_birth"] =
                        agentflow::inspect_owner_process_birth(GetCurrentProcessId());
                    body = lease.dump();
                    headers.emplace("Authorization", "Bearer " + master);
                } else if (host && request.path == "/v1/provider/configuration/import" && request.method == "POST") {
                    require(request.target == request.path && !trusted_provider_config.empty(),
                            "Trusted provider configuration import is unavailable");
                    std::set<std::string> keys;
                    const auto input = Json::parse(body, [&](int depth, Json::parse_event_t event, Json &value) {
                        require(depth <= 2, "Invalid trusted provider import request");
                        if (event == Json::parse_event_t::key)
                            require(keys.insert(value.get<std::string>()).second,
                                    "Duplicate trusted provider import field");
                        return true;
                    });
                    require(input.is_object() && input.size() == 2 && input.contains("path") &&
                                input.at("path").is_string() && input.at("path").get<std::string>() == trusted_provider_config &&
                                input.contains("expected_revision") && input.at("expected_revision").is_number_integer() &&
                                input.at("expected_revision").get<std::int64_t>() >= 0,
                            "Trusted provider import must use its configured path and current revision");
                    // The backend deliberately excludes configuration-file import
                    // from view-session permissions. Only this authenticated host
                    // path can import the exact file selected at adapter startup.
                    headers.emplace("Authorization", "Bearer " + master);
                } else if (host) {
                    std::lock_guard lock(credential_mutex);
                    if (expires - now() < 60000)
                        issue();
                    headers.emplace("Authorization", "View " + credential);
                    headers.emplace("X-XMind-View-Origin", origin);
                } else {
                    if (request.path == "/v1/provider/configuration/import") {
                        response(output, 403, "Trusted provider configuration import requires the local host");
                        return;
                    }
                    if (request.get_header_value_count("X-XMind-View-Origin") != 1) {
                        response(output, 403, "Browser view origin required");
                        return;
                    }
                    (void)browser_workspace(supplied, browser_origin);
                    headers.emplace("Authorization", supplied);
                        headers.emplace("X-XMind-View-Origin",
                                        request.get_header_value("X-XMind-View-Origin"));
                }
                const bool streaming=request.method=="GET"&&(request.path.ends_with("/events/stream")||request.path.ends_with("/tree-events/stream"));
                if(streaming&&request.has_header("Last-Event-ID")){
                    require(request.get_header_value_count("Last-Event-ID")==1,"Duplicate stream cursor");
                    headers.emplace("Last-Event-ID",request.get_header_value("Last-Event-ID"));
                }
                std::shared_ptr<agentflow::LocalProfileConnection> browser_target;
                if (!host) {
                    browser_target = browser_workspace(supplied, browser_origin).second;
                }
                const auto backend_port = host ? connection->port : browser_target->port;
                httplib::Client client("127.0.0.1", backend_port);
                configure(client);
                if(streaming){
                    auto stream=std::make_shared<httplib::ClientImpl::StreamHandle>(client.open_stream("GET",request.target,{},headers));
                    if(!stream->is_valid()){response(output,503,"Native event observation unavailable");return;}
                    if(stream->response->status!=200){response(output,stream->response->status,"Native event observation rejected");return;}
                    require(stream->response->get_header_value("Content-Type")=="text/event-stream","Invalid native event response");
                    output.set_header("X-Accel-Buffering","no");
                    output.set_chunked_content_provider("text/event-stream",[stream,total=std::size_t{0}](std::size_t,httplib::DataSink& sink)mutable{
                        if(!sink.is_writable())return false;
                        char bytes[16384];const auto count=stream->read(bytes,sizeof(bytes));
                        if(count<0||stream->has_read_error())return false;
                        if(count==0){sink.done();return true;}
                        total+=static_cast<std::size_t>(count);if(total>64*1024*1024)return false;
                        return sink.write(bytes,static_cast<std::size_t>(count));
                    });return;
                }
                const auto result = request.method == "GET"
                                        ? client.Get(request.target, headers)
                                        : request.method == "DELETE"
                                              ? client.Delete(request.target, headers, body,
                                                              request.get_header_value("Content-Type"))
                                              : client.Post(request.target, headers, body,
                                                            request.get_header_value("Content-Type"));
                if (!result)
                    response(output, 503,
                             "Native backend observation unavailable; reconnect this profile");
                else {
                    if (host && request.path == "/v1/view-sessions" && request.method == "POST" &&
                        result->status == 200) {
                        const auto issued = Json::parse(result->body);
                        const auto token = issued.at("credential").get<std::string>();
                        const auto requested_origin = requested_browser_origin;
                        require(token.size() == 129 && token[64] == '.' &&
                                    !requested_origin.empty(),
                                "Native server returned an invalid browser credential");
                        std::lock_guard lock(browser_state_mutex);
                        require(browser_sessions.size() < 128, "Browser session registry is full");
                        browser_sessions.emplace(token,
                                                 SessionBinding{selected_workspace_id, requested_origin});
                    } else if (!host && request.path == "/v1/view-sessions/revoke" &&
                               request.method == "POST" && result->status == 200) {
                        std::lock_guard lock(browser_state_mutex);
                        browser_sessions.erase(supplied.substr(5));
                    }
                    output.status = result->status;
                    output.set_content(result->body, result->get_header_value("Content-Type"));
                }
            } catch (const std::invalid_argument &) {
                response(output, 400, "Invalid view request");
            } catch (const Json::exception &) {
                response(output, 400, "Invalid view request");
            } catch (const Unauthorized &) {
                response(output, 401, "Browser view session is no longer active");
            } catch (...) {
                response(output, 503, "Native view observation unavailable");
            }
        };
        server.Get(R"(/v1/.*)", forward);
        server.Post(R"(/v1/.*)", forward);
        server.Delete(R"(/v1/.*)", forward);
        httplib::Client metadata_client("127.0.0.1", connection->port);
        configure(metadata_client);
        const auto metadata =
            metadata_client.Get("/v1/workspace", httplib::Headers{{"Authorization", "View " + credential},
                                                                  {"X-XMind-View-Origin", origin}});
        require(metadata && metadata->status == 200, "Native view workspace unavailable");
        const auto workspace = Json::parse(metadata->body);
        agentflow::publish_local_view_ready(
            ready,
            Json{{"origin", origin},
                 {"workspace", workspace},
                 {"process_id", GetCurrentProcessId()},
                 {"process_birth", agentflow::inspect_owner_process_birth(GetCurrentProcessId())},
                 {"backend_origin", "http://127.0.0.1:" + std::to_string(connection->port)},
                 {"backend_process_id", connection->process_id},
                 {"profile_directory", connection->directory},
                 {"started_backend", connection->started}}
                .dump(),
            connection->workspace);
        active_view = &server;
        SetConsoleCtrlHandler(stop_view, TRUE);
        const auto ok = server.listen_after_bind();
        SetConsoleCtrlHandler(stop_view, FALSE);
        active_view = nullptr;
        httplib::Client cleanup("127.0.0.1", connection->port);
        configure(cleanup);
        cleanup.Post("/v1/view-sessions/revoke",
                     {{"Authorization", "View " + credential}, {"X-XMind-View-Origin", origin}}, "{}",
                     "application/json");
        return ok ? 0 : 1;
    } catch (const std::exception &error) {
        try {
            if (!ready.empty() && !options.workspace.empty()) {
                const auto path = std::filesystem::u8path(ready).parent_path() / "error.json";
                const auto raw = path.u8string();
                const std::string file(reinterpret_cast<const char *>(raw.data()), raw.size());
                std::string detail = error.what();
                if (detail.size() > 1024)
                    detail = "Native view startup failed";
                agentflow::publish_local_view_ready(file,
                                                    Json{{"error_code", "native_view_startup_failed"},
                                                         {"detail", detail},
                                                         {"process_id", GetCurrentProcessId()}}
                                                        .dump(),
                                                    options.workspace);
            }
        } catch (...) { /* Preserve the original failure; never write an unsafe path. */
        }
        std::cerr << error.what() << '\n';
        return 2;
    }
}
