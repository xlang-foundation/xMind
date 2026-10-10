#include "agentflow/local_profile.hpp"
#include "agentflow/context_records.hpp"
#include "agentflow/owner_process.hpp"
#include "agentflow/runtime_generation.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace {
using Json = nlohmann::json;
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
void verify_client_image() {
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
    generation.require_current_server();
}
} // namespace

int run_local_view(int argc, char **argv) {
    agentflow::LocalProfileOptions options;
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
            require(i + 1 < argc && *argv[i + 1], "Native view option requires a value");
            const std::string value = argv[++i];
            if (key == "--workspace")
                options.workspace = value;
            else if (key == "--profile-root")
                options.profile_root = value;
            else if (key == "--config")
                options.provider_config = value;
            else if (key == "--graphs-config")
                options.graphs_config = value;
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
        agentflow::validate_local_view_ready(ready, options.workspace);
        verify_client_image();
        auto connection = agentflow::connect_local_profile(options);
        httplib::Server server;
        server.new_task_queue = [] { return new httplib::ThreadPool(4, 4, 32); };
        server.set_payload_max_length(1024 * 1024);
        server.set_read_timeout(5, 0);
        server.set_write_timeout(5, 0);
        server.set_keep_alive_timeout(1);
        const int port = server.bind_to_any_port("127.0.0.1");
        require(port > 0, "Cannot bind native view adapter");
        const auto origin = "http://127.0.0.1:" + std::to_string(port);
        const std::string master(reinterpret_cast<const char *>(connection.auth.view().data()),
                                 connection.auth.view().size());
        std::mutex credential_mutex;
        std::string credential;
        std::int64_t expires = 0;
        const auto configure = [](httplib::Client &client) {
            client.set_connection_timeout(2, 0);
            client.set_read_timeout(15, 0);
            client.set_write_timeout(5, 0);
            client.set_follow_location(false);
        };
        const auto issue = [&] {
            httplib::Client client("127.0.0.1", connection.port);
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
                       (request.method != "GET" && request.method != "POST")) {
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
                    lease["process_id"] = GetCurrentProcessId();
                    lease["process_birth"] =
                        agentflow::inspect_owner_process_birth(GetCurrentProcessId());
                    body = lease.dump();
                    headers.emplace("Authorization", "Bearer " + master);
                } else if (host) {
                    std::lock_guard lock(credential_mutex);
                    if (expires - now() < 60000)
                        issue();
                    headers.emplace("Authorization", "View " + credential);
                    headers.emplace("X-XMind-View-Origin", origin);
                } else {
                    headers.emplace("Authorization", supplied);
                        headers.emplace("X-XMind-View-Origin",
                                        request.get_header_value("X-XMind-View-Origin"));
                }
                const bool streaming=request.method=="GET"&&(request.path.ends_with("/events/stream")||request.path.ends_with("/tree-events/stream"));
                if(streaming&&request.has_header("Last-Event-ID")){
                    require(request.get_header_value_count("Last-Event-ID")==1,"Duplicate stream cursor");
                    headers.emplace("Last-Event-ID",request.get_header_value("Last-Event-ID"));
                }
                httplib::Client client("127.0.0.1", connection.port);
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
                                        : client.Post(request.target, headers, body,
                                                      request.get_header_value("Content-Type"));
                if (!result)
                    response(output, 503,
                             "Native backend observation unavailable; reconnect this profile");
                else {
                    output.status = result->status;
                    output.set_content(result->body, result->get_header_value("Content-Type"));
                }
            } catch (const std::invalid_argument &) {
                response(output, 400, "Invalid view request");
            } catch (const Json::exception &) {
                response(output, 400, "Invalid view request");
            } catch (...) {
                response(output, 503, "Native view observation unavailable");
            }
        };
        server.Get(R"(/v1/.*)", forward);
        server.Post(R"(/v1/.*)", forward);
        httplib::Client metadata_client("127.0.0.1", connection.port);
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
                 {"backend_origin", "http://127.0.0.1:" + std::to_string(connection.port)},
                 {"backend_process_id", connection.process_id},
                 {"profile_directory", connection.directory},
                 {"started_backend", connection.started}}
                .dump(),
            connection.workspace);
        active_view = &server;
        SetConsoleCtrlHandler(stop_view, TRUE);
        const auto ok = server.listen_after_bind();
        SetConsoleCtrlHandler(stop_view, FALSE);
        active_view = nullptr;
        httplib::Client cleanup("127.0.0.1", connection.port);
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
