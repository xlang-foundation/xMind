#include "agentflow/local_profile.hpp"
#include "agentflow/provider_yaml_config.hpp"
#include "agentflow/client_auth.hpp"
#include "agentflow/local_auth.hpp"
#include "agentflow/mcp_configuration.hpp"
#include "agentflow/context_records.hpp"
#include "agentflow/owner_process.hpp"
#include "agentflow/runtime_generation.hpp"
#include "agentflow/interrupted_owner_recovery.hpp"
#include "agentflow/backend_owner_control.hpp"
#include "agentflow/backend_lease.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/workspace_tools.hpp"
#include "httplib.h"
#include "native_request_channel.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include "profile_record_publication.hpp"

#include <aclapi.h>
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <chrono>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sddl.h>
#include <set>
#include <thread>
namespace agentflow {
namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
struct ProfileRequestError : std::runtime_error {
    int status;
    std::string body;
    ProfileRequestError(int value, const std::string& path, std::string response)
        : std::runtime_error("Managed backend owner request was rejected: " + path + ": " + response),
          status(value), body(std::move(response)) {}
};
void require(bool condition, const char *message = "Local profile validation failed; stored state was preserved") {
    if (!condition)
        throw std::runtime_error(message);
}
void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::wstring wide(const std::string &value) {
    require(!value.empty() && value.size() <= 131072 && value.find('\0') == std::string::npos);
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    require(size > 0);
    std::wstring out(size, L'\0');
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                out.data(), size) == size);
    return out;
}
std::string utf8(const std::wstring &value) {
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    require(size > 0);
    std::string out(size, '\0');
    require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                out.data(), size, nullptr, nullptr) == size);
    return out;
}
std::string text(const fs::path &value) { return utf8(value.native()); }
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE v = INVALID_HANDLE_VALUE) : value(v) {}
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE && value)
            CloseHandle(value);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&other) noexcept : value(other.value) { other.value = INVALID_HANDLE_VALUE; }
};
std::string random_id() {
    std::array<unsigned char, 32> bytes{};
    require(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0,
            "Cannot generate local profile authentication");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (auto byte : bytes) {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    SecureZeroMemory(bytes.data(), bytes.size());
    return result;
}
bool hex(const std::string &value, std::size_t size) {
    return value.size() == size && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::string encode(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (auto byte : bytes) {
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}
std::vector<std::uint8_t> decode(const std::string &value) {
    require(!value.empty() && value.size() <= 1048576 && value.size() % 2 == 0 && hex(value, value.size()));
    std::vector<std::uint8_t> result;
    for (std::size_t i = 0; i < value.size(); i += 2) {
        const auto nib = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        result.push_back(static_cast<std::uint8_t>(nib(value[i]) * 16 + nib(value[i + 1])));
    }
    return result;
}
Json parse(const std::string &source) {
    std::vector<std::set<std::string>> fields;
    try {
        return Json::parse(source, [&](int depth, Json::parse_event_t event, Json &value) {
            require(depth <= 8);
            if (event == Json::parse_event_t::object_start)
                fields.emplace_back();
            else if (event == Json::parse_event_t::object_end)
                fields.pop_back();
            else if (event == Json::parse_event_t::key)
                require(fields.back().insert(value.get<std::string>()).second);
            return true;
        });
    } catch (...) {
        throw std::runtime_error("Invalid protected local profile record; stored state was preserved");
    }
}
struct Security {
    std::vector<std::uint8_t> user;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    Security() {
        Handle token;
        require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value) != 0);
        DWORD size = 0;
        GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
        require(size > 0 && size < 65536);
        user.resize(size);
        require(GetTokenInformation(token.value, TokenUser, user.data(), size, &size) != 0);
        LPWSTR sid = nullptr;
        require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid, &sid) != 0);
        // Creation must satisfy verify() even when the token's default owner
        // is a group. Do not relax existing-file ownership checks or repair
        // somebody else's storage in place.
        const auto sddl = L"O:" + std::wstring(sid) + L"D:P(A;;FA;;;SY)(A;;FA;;;" + std::wstring(sid) + L")";
        LocalFree(sid);
        require(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                                     &descriptor, nullptr) != 0);
        attributes.lpSecurityDescriptor = descriptor;
    }
    ~Security() {
        if (descriptor)
            LocalFree(descriptor);
    }
    void verify(HANDLE handle) const {
        PSID owner = nullptr;
        PACL acl = nullptr;
        PSECURITY_DESCRIPTOR sd = nullptr;
        require(GetSecurityInfo(handle, SE_FILE_OBJECT,
                                OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &acl,
                                nullptr, &sd) == ERROR_SUCCESS);
        struct Free {
            PSECURITY_DESCRIPTOR p;
            ~Free() { LocalFree(p); }
        } free{sd};
        require(owner && acl && EqualSid(owner, reinterpret_cast<const TOKEN_USER *>(user.data())->User.Sid),
                "Local profile storage is not owned by this Windows user");
        for (DWORD i = 0; i < acl->AceCount; ++i) {
            void *entry = nullptr;
            require(GetAce(acl, i, &entry) != 0);
            const auto *head = static_cast<ACE_HEADER *>(entry);
            if (head->AceType == ACCESS_DENIED_ACE_TYPE)
                continue;
            require(head->AceType == ACCESS_ALLOWED_ACE_TYPE);
            const auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(entry);
            const auto sid = const_cast<DWORD *>(&ace->SidStart);
            const bool trusted = EqualSid(sid, reinterpret_cast<const TOKEN_USER *>(user.data())->User.Sid) ||
                                 IsWellKnownSid(sid, WinLocalSystemSid) ||
                                 IsWellKnownSid(sid, WinBuiltinAdministratorsSid);
            if (!trusted)
                require((ace->Mask & (GENERIC_ALL | GENERIC_WRITE | WRITE_DAC | WRITE_OWNER | DELETE |
                                      FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_DELETE_CHILD)) == 0,
                        "Local profile storage permits another principal to modify it");
        }
    }
};
fs::path absolute(const std::string &input) {
    auto path = fs::path(wide(input));
    require(path.is_absolute() && path.native().size() < 32768 && path.native().size() > 3 &&
                path.native()[1] == L':' && !path.native().starts_with(L"\\\\"),
            "Local profiles require an absolute local Windows directory");
    for (const auto &part : path.relative_path()) {
        const auto component = part.native();
        require(component != L"." && component != L".." && !component.empty() && component.back() != L'.' &&
                component.back() != L' ' && component.find_first_of(L"<>:\"|?*") == std::wstring::npos);
    }
    return path.lexically_normal();
}
std::wstring final_path(HANDLE handle) {
    std::wstring out(32768, L'\0');
    const auto size =
        GetFinalPathNameByHandleW(handle, out.data(), static_cast<DWORD>(out.size()), FILE_NAME_NORMALIZED);
    require(size > 0 && size < out.size());
    out.resize(size);
    require(out.starts_with(L"\\\\?\\") && !out.starts_with(L"\\\\?\\UNC\\"));
    return out.substr(4);
}
std::wstring extended(const fs::path &path) {
    auto value = path.native();
    std::replace(value.begin(), value.end(), L'/', L'\\');
    return value.starts_with(L"\\\\?\\") ? value : L"\\\\?\\" + value;
}
bool same(const std::wstring &a, const std::wstring &b) {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()),
                                TRUE) == CSTR_EQUAL;
}
bool contains(const fs::path &parent, const fs::path &child) {
    const auto p = parent.native(), c = child.native();
    return same(p, c) || (c.size() > p.size() && c[p.size()] == L'\\' && same(p, c.substr(0, p.size())));
}
class Store {
  public:
    Security security;
    fs::path directory;
    std::vector<Handle> ancestors;
    explicit Store(fs::path path, bool create = false) : directory(std::move(path)) {
        auto part = directory.root_path();
        for (const auto &component : directory.relative_path()) {
            part /= component;
            const auto native_part = extended(part);
            if (create && !fs::exists(fs::path(native_part))) {
                require(CreateDirectoryW(native_part.c_str(), &security.attributes) != 0 ||
                            GetLastError() == ERROR_ALREADY_EXISTS,
                        "Cannot create protected local profile directory");
            }
            Handle held(CreateFileW(native_part.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            require(held.value != INVALID_HANDLE_VALUE);
            BY_HANDLE_FILE_INFORMATION info{};
            require(GetFileInformationByHandle(held.value, &info) != 0 &&
                        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY),
                    "Local profile path component is not an accessible directory: " + text(part));
            require(!(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT),
                    "Local profile path component is a reparse point: " + text(part));
            const auto resolved = final_path(held.value);
            require(same(resolved, part.native()),
                    "Local profile path component resolves to a different directory: " + text(part) +
                        " -> " + text(fs::path(resolved)));
            ancestors.push_back(std::move(held));
        }
        require(!ancestors.empty());
        security.verify(ancestors.back().value);
        directory = fs::path(final_path(ancestors.back().value));
    }
    std::string context() const {
        BY_HANDLE_FILE_INFORMATION info{};
        require(GetFileInformationByHandle(ancestors.back().value, &info) != 0);
        return "xmind-local-profile:" + std::to_string(info.dwVolumeSerialNumber) + ":" +
               std::to_string(info.nFileIndexHigh) + ":" + std::to_string(info.nFileIndexLow);
    }
    Handle open(const wchar_t *name, DWORD access, DWORD creation, DWORD share = 0) {
        Handle file(CreateFileW(extended(directory / name).c_str(), access | READ_CONTROL, share,
                                &security.attributes, creation,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        require(file.value != INVALID_HANDLE_VALUE, "Cannot create protected local profile file");
        BY_HANDLE_FILE_INFORMATION info{};
        require(GetFileInformationByHandle(file.value, &info) != 0 &&
                !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
                info.nNumberOfLinks == 1 && same(final_path(file.value), (directory / name).native()));
        security.verify(file.value);
        return file;
    }
    std::string read(const wchar_t *name) {
        auto file = open(name, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ | FILE_SHARE_DELETE);
        LARGE_INTEGER size{};
        require(GetFileSizeEx(file.value, &size) != 0 && size.QuadPart > 0 && size.QuadPart <= 1048576);
        std::string out(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD count = 0;
        require(ReadFile(file.value, out.data(), static_cast<DWORD>(out.size()), &count, nullptr) != 0 &&
                count == out.size());
        return out;
    }
    Json state() {
        const auto protected_record = parse(read(L"profile.state"));
        require(protected_record.is_object() && protected_record.size() == 2 &&
                protected_record.value("protection", std::string{}) == "windows-dpapi-user-v1" &&
                protected_record.contains("ciphertext") && protected_record.at("ciphertext").is_string());
        auto secret = reveal_secret(
            {"windows-dpapi-user-v1", decode(protected_record.at("ciphertext").get<std::string>())},
            context());
        return parse(std::string(reinterpret_cast<const char *>(secret.view().data()), secret.view().size()));
    }
    void write(const Json &value) {
        auto source = value.dump();
        SecretBytes secret({reinterpret_cast<const std::uint8_t *>(source.data()), source.size()});
        const auto encrypted = protect_secret(secret, context());
        SecureZeroMemory(source.data(), source.size());
        const auto raw =
            Json{{"protection", encrypted.protection}, {"ciphertext", encode(encrypted.ciphertext)}}.dump();
        const auto name = wide("state-" + random_id() + ".tmp");
        auto file = open(name.c_str(), GENERIC_WRITE, CREATE_NEW);
        DWORD written = 0;
        require(WriteFile(file.value, raw.data(), static_cast<DWORD>(raw.size()), &written, nullptr) != 0 &&
                written == raw.size() && FlushFileBuffers(file.value) != 0);
        CloseHandle(file.value);
        file.value = INVALID_HANDLE_VALUE;
        if (fs::exists(directory / L"profile.state")) {
            auto old = open(L"profile.state", FILE_READ_ATTRIBUTES, OPEN_EXISTING,
                            FILE_SHARE_READ | FILE_SHARE_DELETE);
        }
        detail::publish_profile_record(extended(directory / name),extended(directory / L"profile.state"));
    }
    Handle lock() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
        for (;;) {
            Handle value(CreateFileW(extended(directory / L"startup.lock").c_str(),
                                     GENERIC_READ | GENERIC_WRITE | READ_CONTROL, 0, &security.attributes,
                                     OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                                     nullptr));
            if (value.value != INVALID_HANDLE_VALUE) {
                security.verify(value.value);
                BY_HANDLE_FILE_INFORMATION info{};
                require(GetFileInformationByHandle(value.value, &info) != 0 &&
                        !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && info.nNumberOfLinks == 1 &&
                        same(final_path(value.value), (directory / L"startup.lock").native()));
                return value;
            }
            require(GetLastError() == ERROR_SHARING_VIOLATION, "Cannot lock local profile startup");
            require(std::chrono::steady_clock::now() < deadline,
                    "Another local profile startup remains in progress");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
};
std::string loaded_root() {
    std::wstring image(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, image.data(), static_cast<DWORD>(image.size()));
    require(size > 0 && size < image.size());
    image.resize(size);
    // Normalize only the OS-reported local drive image for comparison with
    // the protected canonical runtime record; user path admission is unchanged.
    if (image.starts_with(L"\\\\?\\") && image.size() > 6 && image[5] == L':')
        image.erase(0, 4);
    require(same(fs::path(image).filename().native(), L"xmind.exe"),
            "Managed local profiles require the unified xmind.exe package");
    return text(fs::path(image).parent_path());
}
std::string file_bytes(const fs::path &path, std::size_t bound) {
    std::ifstream file(path, std::ios::binary);
    require(static_cast<bool>(file));
    std::string out;
    std::array<char, 65536> chunk{};
    while (file) {
        file.read(chunk.data(), chunk.size());
        out.append(chunk.data(), static_cast<std::size_t>(file.gcount()));
        require(out.size() <= bound);
    }
    require(file.eof());
    return out;
}
std::string default_root() {
    std::array<wchar_t, 32768> value{};
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", value.data(), static_cast<DWORD>(value.size()));
    require(length > 0 && length < value.size(), "Local xMind profile storage requires USERPROFILE");
    const fs::path requested(value.data());
    Handle profile(CreateFileW(extended(requested).c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    require(profile.value != INVALID_HANDLE_VALUE, "Cannot open the Windows user profile directory");
    BY_HANDLE_FILE_INFORMATION info{};
    require(GetFileInformationByHandle(profile.value, &info) != 0 &&
                (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY),
            "Windows user profile path is not a directory");
    const fs::path user_root(final_path(profile.value));
    const auto primary = user_root / L".xMind" / L"p";
    if (primary.native().size() <= 84) return text(primary);
    const auto fallback = user_root / L"p";
    require(fallback.native().size() <= 84,
            "The local Windows profile location is too long for native runtime preparation");
    return text(fallback);
}
void shape(const Json &value, const Store &store, const WorkspaceTools &workspace) {
    require(value.is_object() && value.contains("schema") && value.at("schema").is_number_integer() &&
            value.at("schema") == 1 && value.value("workspace", std::string{}) == workspace.root_path() &&
            value.value("workspace_id", std::string{}) == workspace.identity() &&
            value.value("directory", std::string{}) == text(store.directory) &&
            hex(value.value("auth", std::string{}), 64) && hex(value.value("manifest", std::string{}), 64) &&
            value.contains("pid") && value.at("pid").is_number_unsigned() &&
            value.at("pid").get<std::uint64_t>() <= MAXDWORD && value.contains("port") &&
            value.at("port").is_number_integer() && value.at("port") >= 0 && value.at("port") <= 65535 &&
            value.contains("approved_edits") && value.at("approved_edits").is_boolean());
    const auto execution=value.value("agent_execution",std::string{"thread"});require(execution=="thread"||execution=="process-read"||execution=="process-code","Invalid protected agent execution mode");
    for(const auto* field:{"provider_config","graphs_config","python_tools_config","language_server_config","process_config","mcp_config","web_assets","web_packages"})
        if(value.contains(field)&&!value.at(field).get<std::string>().empty())require(absolute(value.at(field).get<std::string>()).is_absolute());
    require((!value.contains("process_config")||value.at("process_config").get<std::string>().empty())
                ?(!value.contains("process_config_digest")||value.at("process_config_digest").get<std::string>().empty())
                :hex(value.value("process_config_digest",std::string{}),64),
            "Saved process configuration fingerprint is invalid");
    if(value.contains("process_config_pending"))require(value.at("process_config_pending").is_boolean()&&
        (!value.at("process_config_pending").get<bool>()||(value.contains("process_config")&&!value.at("process_config").get<std::string>().empty())),
        "Saved process configuration update is invalid");
    require(value.value("mcp_config",std::string{}).empty()
                ?value.value("mcp_config_digest",std::string{}).empty()
                :hex(value.value("mcp_config_digest",std::string{}),64),
            "Saved MCP configuration fingerprint is invalid");
    if(value.contains("mcp_config_pending"))require(value.at("mcp_config_pending").is_boolean()&&
        (!value.at("mcp_config_pending").get<bool>()||!value.value("mcp_config",std::string{}).empty()),
        "Saved MCP configuration update is invalid");
    if(value.contains("language_server_config_pending"))require(value.at("language_server_config_pending").is_boolean()&&
        (!value.at("language_server_config_pending").get<bool>()||(value.contains("language_server_config")&&!value.at("language_server_config").get<std::string>().empty())),
        "Saved language-server configuration update is invalid");
    require(value.value("phase", std::string{}) == "preparing" ||
            value.value("phase", std::string{}) == "starting" ||
            value.value("phase", std::string{}) == "ready");
    if(value.contains("owner_receipt")){
        const auto receipt=value.at("owner_receipt");
        require(receipt.is_object()&&receipt.size()==3&&hex(receipt.value("generation",std::string{}),32)&&
                hex(receipt.value("receipt_id",std::string{}),32)&&receipt.contains("revision")&&
                receipt.at("revision").is_number_integer()&&receipt.at("revision").get<std::int64_t>()>0);
        if(value.contains("replacement_port"))
            require(value.at("replacement_port").is_number_integer()&&value.at("replacement_port").get<int>()>0&&
                    value.at("replacement_port").get<int>()<=65535);
    }
    if(value.contains("upgrade_intent"))require(value.at("upgrade_intent").is_boolean()&&value.at("upgrade_intent").get<bool>());
    if(value.contains("replacement_manifest"))require((value.contains("owner_receipt")||value.contains("upgrade_intent"))&&hex(value.at("replacement_manifest").get<std::string>(),64));
    require(!value.contains("replacement_port")||value.contains("owner_receipt")||value.contains("upgrade_intent"));
    require(contains(store.directory, absolute(value.at("runtime").get<std::string>())),
            "Saved runtime escaped its private profile directory");
    if(value.contains("replacement_runtime")){
        require((value.contains("owner_receipt")||value.contains("upgrade_intent"))&&
                    value.contains("replacement_manifest")&&
                    contains(store.directory,absolute(value.at("replacement_runtime").get<std::string>()))&&
                    (absolute(value.at("replacement_runtime").get<std::string>())!=absolute(value.at("runtime").get<std::string>())||
                     (value.contains("owner_receipt")&&value.at("manifest")==value.at("replacement_manifest"))),
                "Replacement runtime escaped its private profile directory");
    }
    if (value.at("phase") != "preparing") {
        const auto birth = value.value("birth", std::string{});
        require(value.at("pid") > 0 && !birth.empty() && birth.size() <= 20 && birth.front() != '0' &&
                birth.find_first_not_of("0123456789") == std::string::npos);
    }
    if (value.at("phase") == "ready")
        require(value.at("port") > 0 && hex(value.value("authority", std::string{}), 32));
}
std::wstring quote(const std::wstring &value) {
    std::wstring out = L"\"";
    std::size_t slashes = 0;
    for (const auto c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'\"') {
            out.append(slashes * 2 + 1, L'\\');
            out += c;
        } else {
            out.append(slashes, L'\\');
            out += c;
        }
        slashes = 0;
    }
    out.append(slashes * 2, L'\\');
    out += L'\"';
    return out;
}
std::vector<wchar_t> environment(const std::string &auth) {
    LPWCH raw = GetEnvironmentStringsW();
    require(raw != nullptr);
    struct Free {
        LPWCH value;
        ~Free() { FreeEnvironmentStringsW(value); }
    } free{raw};
    struct Less {
        bool operator()(const std::wstring &a, const std::wstring &b) const {
            return _wcsicmp(a.c_str(), b.c_str()) < 0;
        }
    };
    std::map<std::wstring, std::wstring, Less> values;
    for (auto cursor = raw; *cursor; cursor += wcslen(cursor) + 1) {
        std::wstring item = cursor;
        const auto equal = item.find(L'=', item.starts_with(L"=") ? 1 : 0);
        require(equal != std::wstring::npos);
        const auto name = item.substr(0, equal);
        if (_wcsicmp(name.c_str(), L"XMIND_AUTH_TOKEN") == 0 ||
            _wcsicmp(name.c_str(), L"XMIND_API_KEY") == 0 || _wcsnicmp(name.c_str(), L"XMIND_UI_", 9) == 0)
            continue;
        values[name] = item.substr(equal + 1);
    }
    values[L"XMIND_AUTH_TOKEN"] = wide(auth);
    std::vector<wchar_t> out;
    for (const auto &[key, value] : values) {
        const auto item = key + L"=" + value;
        out.insert(out.end(), item.begin(), item.end());
        out.push_back(0);
    }
    out.push_back(0);
    return out;
}
struct Child {
    Handle process, thread, job;
    bool detached = false;
    void release() {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        require(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits,
                                        sizeof(limits)) != 0,
                "Cannot detach the persistent local backend");
        detached = true;
    }
};
Child launch(Store &store, Json &state) {
    Child child;
    child.job.value = CreateJobObjectW(nullptr, nullptr);
    require(child.job.value != nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    require(SetInformationJobObject(child.job.value, JobObjectExtendedLimitInformation, &limits,
                                    sizeof(limits)) != 0);
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 2, 0, &size);
    std::vector<std::uint8_t> storage(size);
    auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    require(InitializeProcThreadAttributeList(attributes, 2, 0, &size) != 0);
    struct Delete {
        LPPROC_THREAD_ATTRIBUTE_LIST value;
        ~Delete() { DeleteProcThreadAttributeList(value); }
    } cleanup{attributes};
    HANDLE job = child.job.value;
    require(UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job, sizeof(job),
                                      nullptr, nullptr) != 0,
            "Atomic managed process ownership is unavailable");
    auto log = store.open(L"native.log", FILE_APPEND_DATA, OPEN_ALWAYS, FILE_SHARE_READ | FILE_SHARE_WRITE);
    SECURITY_ATTRIBUTES inherited{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherited,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    require(input.value != INVALID_HANDLE_VALUE &&
            SetHandleInformation(log.value, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) != 0);
    std::array<HANDLE, 2> inherited_handles{input.value, log.value};
    require(UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                      inherited_handles.data(), sizeof(inherited_handles), nullptr,
                                      nullptr) != 0);
    const auto runtime = absolute(state.at("runtime").get<std::string>()), program = runtime / L"xmind.exe";
    // The loader can reject a near-MAX_PATH image before main while resolving
    // its adjacent runtime DLL. Keep the verified identity canonical, but use
    // the extended Windows path for both the image and argv[0] at launch.
    const auto native_program = extended(program);
    std::vector<std::wstring> args{native_program,
                                   L"serve",
                                   L"--db",
                                   (store.directory / L"state.sqlite").native(),
                                   L"--modules",
                                   (runtime / L"modules").native(),
                                   L"--stdlib",
                                   (runtime / L"stdlib").native(),
                                   L"--workspace",
                                   wide(state.at("workspace").get<std::string>()),
                                   L"--port",
                                   state.contains("replacement_port")?std::to_wstring(state.at("replacement_port").get<int>()):
                                       (state.at("port").get<int>()>0?std::to_wstring(state.at("port").get<int>()):L"0"),
                                   L"--runtime-manifest-sha256",
                                   wide(state.at("manifest").get<std::string>()),
                                   L"--profile-state",
                                   (store.directory / L"profile.state").native()};
    args.push_back(L"--workspace-registry");
    args.push_back((store.directory.parent_path()/L"workspaces.sqlite").native());
    for(const auto* field:{"web_assets","web_packages"}){
        const auto value=state.value(field,std::string{});if(!value.empty()){
            args.push_back(std::string(field)=="web_assets"?L"--web-assets":L"--web-packages");args.push_back(wide(value));
        }
    }
    if(state.value("web_port",0)>0){args.push_back(L"--web-port");args.push_back(std::to_wstring(state.at("web_port").get<int>()));}
    if(state.contains("owner_receipt")){
        const auto& receipt=state.at("owner_receipt");
        args.push_back(L"--owner-receipt");
        args.push_back(wide(receipt.at("generation").get<std::string>()+":"+
            std::to_string(receipt.at("revision").get<std::int64_t>())+":"+
            receipt.at("receipt_id").get<std::string>()));
    }
    args.push_back(L"--agent-execution");args.push_back(wide(state.value("agent_execution",std::string{"thread"})));
    if (state.at("approved_edits").get<bool>()) {
        args.push_back(L"--workspace-edits");
        args.push_back(L"approved");
    }
    for (const auto *field : {"provider_config", "graphs_config", "python_tools_config", "language_server_config", "process_config", "mcp_config"}) {
        if(state.contains("owner_receipt")&&std::string(field)!="process_config"&&std::string(field)!="mcp_config"&&
           !(std::string(field)=="language_server_config"&&state.value("language_server_config_pending",false)))continue;
        if(std::string(field)=="process_config"&&!state.value("process_config_pending",false))continue;
        if(std::string(field)=="mcp_config"&&!state.value("mcp_config_pending",false))continue;
        if(std::string(field)=="language_server_config"&&state.contains("owner_receipt")&&
           !state.value("language_server_config_pending",false))continue;
        const auto value = state.value(field, std::string{});
        if (!value.empty()) {
            const auto option=std::string(field)=="provider_config"?L"--provider-config":
                (std::string(field)=="graphs_config"?L"--graphs-config":
                 (std::string(field)=="python_tools_config"?L"--python-tools-config":
                  (std::string(field)=="process_config"?L"--process-config":
                   (std::string(field)=="mcp_config"?L"--mcp-config":L"--language-server-config"))));
            args.push_back(option);
            args.push_back(wide(value));
        }
    }
    std::wstring command;
    for (const auto &arg : args) {
        if (!command.empty())
            command += L' ';
        command += quote(arg);
    }
    require(command.size() < 32768);
    auto env = environment(state.at("auth").get<std::string>());
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = log.value;
    startup.StartupInfo.hStdError = log.value;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    require(CreateProcessW(native_program.c_str(), command.data(), nullptr, nullptr, TRUE,
                           CREATE_SUSPENDED | DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT |
                               EXTENDED_STARTUPINFO_PRESENT,
                           env.data(), wide(state.at("workspace").get<std::string>()).c_str(),
                           &startup.StartupInfo, &process) != 0,
            "Cannot launch the native local backend");
    SecureZeroMemory(env.data(), env.size() * sizeof(wchar_t));
    child.process.value = process.hProcess;
    child.thread.value = process.hThread;
    state["pid"] = static_cast<std::uint32_t>(process.dwProcessId);
    state["birth"] = inspect_owner_process_birth(process.dwProcessId);
    state["phase"] = "starting";
    state["port"] = 0;
    store.write(state);
    require(ResumeThread(child.thread.value) != static_cast<DWORD>(-1),
            "Cannot resume the owned local backend");
    return child;
}
void verify_process(const Json &state) {
    const auto pid = state.at("pid").get<std::uint32_t>();
    require(pid > 0 && !observe_owner_exit(pid, state.at("birth").get<std::string>(), 0).exited,
            "Local backend exited during connection");
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    require(process.value != nullptr && process.value != INVALID_HANDLE_VALUE);
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    require(QueryFullProcessImageNameW(process.value, 0, path.data(), &size) != 0);
    path.resize(size);
    Handle actual(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr));
    const auto expected = absolute(state.at("runtime").get<std::string>()) / L"xmind.exe";
    Handle wanted(CreateFileW(expected.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr));
    require(actual.value != INVALID_HANDLE_VALUE && wanted.value != INVALID_HANDLE_VALUE);
    BY_HANDLE_FILE_INFORMATION a{}, b{};
    require(GetFileInformationByHandle(actual.value, &a) != 0 &&
                GetFileInformationByHandle(wanted.value, &b) != 0 &&
                a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
                a.nFileIndexLow == b.nFileIndexLow &&
                same(final_path(actual.value), final_path(wanted.value)),
            "Saved profile process image differs; no backend was replaced");
}
std::shared_ptr<httplib::Response> profile_sdk_response(const Json& state,const std::string& path,const Json* payload=nullptr){
    const auto token=state.at("auth").get<std::string>();
    LocalProfileConnection connection{state.at("workspace").get<std::string>(),state.at("directory").get<std::string>(),state.at("port").get<std::uint16_t>(),state.at("pid").get<std::uint32_t>(),false,
        SecretBytes({reinterpret_cast<const std::uint8_t*>(token.data()),token.size()}),state.at("approved_edits").get<bool>(),state.value("agent_execution",std::string{"thread"}),state.at("ipc_endpoint").get<std::string>()};
    NativeRequestChannel channel(connection);const httplib::Headers headers{{"Authorization","Bearer "+token}};
    try{return payload?channel.Post(path,headers,payload->dump(),"application/json"):channel.Get(path,headers);}
    catch(const std::exception& error){throw std::runtime_error("Managed SDK route "+path+": "+error.what());}
}
bool authenticated(const Json &state) {
    try {
        verify_process(state);
        httplib::Client client("127.0.0.1", state.at("port").get<int>());
        client.set_connection_timeout(1, 0);
        client.set_read_timeout(2, 0);
        client.set_follow_location(false);
        const httplib::Headers headers{{"Authorization", "Bearer " + state.at("auth").get<std::string>()}};
        const auto workspace = state.contains("ipc_endpoint")?profile_sdk_response(state,"/v1/workspace"):std::shared_ptr<httplib::Response>{},
                   owner = state.contains("ipc_endpoint")?profile_sdk_response(state,"/v1/backend/owner"):std::shared_ptr<httplib::Response>{};
        if(!state.contains("ipc_endpoint")){
            const auto legacy_workspace=client.Get("/v1/workspace",headers),legacy_owner=client.Get("/v1/backend/owner",headers);
            if(!legacy_workspace||!legacy_owner||legacy_workspace->status!=200||legacy_owner->status!=200)return false;
            const auto w=parse(legacy_workspace->body),o=parse(legacy_owner->body);
            return w.value("configured",false)&&w.value("root",std::string{})==state.at("workspace").get<std::string>()&&w.value("workspace_id",std::string{})==state.at("workspace_id").get<std::string>()&&w.value("authority_id",std::string{})==state.at("authority").get<std::string>()&&o.at("process_id")==state.at("pid")&&o.at("process_birth")==state.at("birth");
        }
        if (!workspace || !owner || workspace->status != 200 || owner->status != 200)
            return false;
        const auto w = parse(workspace->body), o = parse(owner->body);
        return w.value("configured", false) &&
               w.value("root", std::string{}) == state.at("workspace").get<std::string>() &&
               w.value("workspace_id", std::string{}) == state.at("workspace_id").get<std::string>() &&
               w.value("authority_id", std::string{}) == state.at("authority").get<std::string>() &&
               o.at("process_id") == state.at("pid") && o.at("process_birth") == state.at("birth");
    } catch (...) {
        return false;
    }
}
Json profile_request(int port,const std::string& token,const std::string& path,const Json* payload=nullptr){
    httplib::Client client("127.0.0.1",port);client.set_connection_timeout(1,0);client.set_read_timeout(60,0);client.set_write_timeout(15,0);client.set_follow_location(false);
    const httplib::Headers headers{{"Authorization","Bearer "+token}};
    const auto response=payload?client.Post(path,headers,payload->dump(),"application/json"):client.Get(path,headers);
    if(!response)throw std::runtime_error(std::string("Managed backend owner request failed: ")+(payload?"POST ":"GET ")+path+" (transport "+std::to_string(static_cast<int>(response.error()))+")");
    require(response->body.size()<=128*1024,"Managed backend owner response exceeds limits");
    if(response->status<200||response->status>=300)throw ProfileRequestError(response->status,path,response->body);
    return parse(response->body);
}
Json profile_request(const Json& state,const std::string& path,const Json* payload=nullptr){
    // The compatibility path can attach preserved pre-IPC owners for their
    // existing controlled upgrade. New owners always advertise the SDK endpoint.
    if(!state.contains("ipc_endpoint"))return profile_request(state.at("port").get<int>(),state.at("auth").get<std::string>(),path,payload);
    const auto response=profile_sdk_response(state,path,payload);
    require(response&&response->body.size()<=128*1024,"Managed backend SDK response exceeds limits");
    if(response->status<200||response->status>=300)throw ProfileRequestError(response->status,path,response->body);
    return parse(response->body);
}
std::pair<fs::path,std::string> stage_current_runtime(Store& store,const WorkspaceTools& workspace){
    const auto source=absolute(loaded_root());const auto raw=file_bytes(source/L"native-runtime-manifest.json",4*1024*1024),digest=context_digest(raw);
    VerifiedRuntimeGeneration source_generation(text(source),digest);source_generation.require_loaded_server_image();
    const auto target=store.directory/(L"runtime-"+wide(random_id()));Store destination(target,true);const auto manifest=parse(raw);
    std::map<fs::path,std::unique_ptr<Store>> prepared_directories;
    for(const auto& [name,file_digest]:manifest.at("files").items()){
        (void)file_digest;const auto relative=fs::u8path(name),parent=relative.parent_path();
        if(!parent.empty()&&!prepared_directories.contains(parent))prepared_directories.emplace(parent,std::make_unique<Store>(destination.directory/parent,true));
        require(CopyFileW(extended(source/relative).c_str(),extended(destination.directory/relative).c_str(),TRUE)!=0,"Cannot stage the verified replacement runtime");
    }
    auto output=destination.open(L"native-runtime-manifest.json",GENERIC_WRITE,CREATE_NEW);DWORD written=0;
    require(WriteFile(output.value,raw.data(),static_cast<DWORD>(raw.size()),&written,nullptr)!=0&&written==raw.size()&&FlushFileBuffers(output.value)!=0,"Cannot publish the replacement runtime manifest");
    require(CloseHandle(output.value)!=0,"Cannot close replacement runtime manifest");output.value=INVALID_HANDLE_VALUE;
    VerifiedRuntimeGeneration verified(VerifiedRuntimeGeneration::managed_profile_copy(text(destination.directory),digest,workspace.root_path()));verified.revalidate();
    return {destination.directory,digest};
}
Json authority_body(const Json& workspace){
    require(workspace.value("configured",false)&&workspace.contains("workspace_id")&&workspace.contains("authority_id"));
    return {{"expected_workspace_id",workspace.at("workspace_id")},{"expected_workspace_authority_id",workspace.at("authority_id")}};
}
void retire_for_upgrade(Store& store,Json& state,const WorkspaceTools& workspace,bool allow_retarget){
    require(state.contains("owner_receipt")&&state.contains("replacement_port")&&state.contains("replacement_manifest")&&state.contains("replacement_runtime"),"Incomplete persisted backend handoff intent; profile was preserved");
    const auto pid=state.at("pid").get<std::uint32_t>();const auto birth=state.value("birth",std::string{});
    std::string retirement_error;
    if(pid&&!observe_owner_exit(pid,birth,0).exited){
        try{
            Json ws=profile_request(state,"/v1/workspace");
            Json body=authority_body(ws);const auto& receipt=state.at("owner_receipt");
            body["expected_generation"]=receipt.at("generation");body["expected_revision"]=receipt.at("revision");body["receipt_id"]=receipt.at("receipt_id");
            body["target_runtime_root"]=state.at("replacement_runtime");body["target_manifest_sha256"]=state.at("replacement_manifest");body["approved_edits"]=state.at("approved_edits");
            profile_request(state,"/v1/backend/owner/retire",&body);
        }catch(const std::exception& error){
            // A lost reply can follow a committed retirement. Observe the exact
            // process before deciding; retain a rejection for an unresolved exit.
            retirement_error=error.what();
        }catch(...){retirement_error="Unknown managed retirement request failure";}
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(pid&&!observe_owner_exit(pid,birth,0).exited&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(!pid||observe_owner_exit(pid,birth,0).exited,
        "Backend retirement is unresolved; the saved handoff was preserved"+
        (retirement_error.empty()?std::string{}:std::string(". ")+retirement_error));
    {
        // A crash can follow quiescence before the live owner commits retirement.
        // Its protected receipt and an exclusive database lease allow finishing
        // that transition; normal startup must continue to refuse the database.
        BackendLease lease(text(store.directory/L"state.sqlite"));
        const auto source=absolute(loaded_root());
        const auto digest=context_digest(file_bytes(source/L"native-runtime-manifest.json",4*1024*1024));
        VerifiedRuntimeGeneration source_generation(text(source),digest);source_generation.require_loaded_server_image();
        XlangSqlite db(lease.canonical_database_path(),{text(source/L"modules"),text(source/L"stdlib")});
        const auto rows=db.execute("SELECT payload FROM information WHERE category='native-backend-owner' AND id='owner'").rows;
        if(rows.size()==1){
            const auto owner=Json::parse(std::get<std::string>(rows[0][0]));
            if(owner.value("quiesced",false)&&!owner.value("retirement_requested",false)&&!owner.value("replacement_prepared",false)&&!owner.contains("replacement_target")){
                const auto& saved=state.at("owner_receipt");
                BackendOwnerReceipt receipt{saved.at("generation").get<std::string>(),saved.at("receipt_id").get<std::string>(),saved.at("revision").get<std::int64_t>()};
                require(owner.at("generation")==receipt.generation&&owner.at("revision")==receipt.revision&&owner.at("receipt_id")==receipt.receipt_id,"Interrupted backend receipt differs from protected profile");
                if(allow_retarget&&state.at("replacement_manifest")!=digest){
                    auto [replacement,manifest]=stage_current_runtime(store,workspace);
                    state["replacement_runtime"]=text(replacement);state["replacement_manifest"]=manifest;
                    // Persist the exact target before the transaction so a crash
                    // after commit resumes this intent rather than staging again.
                    store.write(state);
                }
                VerifiedRuntimeGeneration replacement(VerifiedRuntimeGeneration::managed_profile_copy(state.at("replacement_runtime").get<std::string>(),state.at("replacement_manifest").get<std::string>(),workspace.root_path()));
                const auto target=qualify_backend_target(replacement,workspace,state.at("auth").get<std::string>(),state.at("approved_edits").get<bool>(),false);
                (void)finish_interrupted_backend_retirement(db,lease,receipt,target,pid,birth);
            }
        }
    }
    VerifiedRuntimeGeneration target(VerifiedRuntimeGeneration::managed_profile_copy(state.at("replacement_runtime").get<std::string>(),state.at("replacement_manifest").get<std::string>(),workspace.root_path()));target.revalidate();
    state["runtime"]=state.at("replacement_runtime");state["manifest"]=state.at("replacement_manifest");state["pid"]=static_cast<std::uint32_t>(0);state["birth"]="";state["port"]=0;state["phase"]="preparing";store.write(state);
}
} // namespace
std::vector<LocalWorkspaceProfile> list_local_workspace_profiles(const std::string &profile_root) {
    const auto configured = absolute(profile_root.empty() ? default_root() : profile_root);
    if (!fs::exists(configured))
        return {};
    Store root(configured, false);
    std::vector<LocalWorkspaceProfile> result;
    std::size_t examined = 0;
    bool exceeded_result_limit = false;
    for (const auto &entry : fs::directory_iterator(root.directory)) {
        require(++examined <= 4096, "Local workspace catalogue exceeds its entry limit");
        const auto name = entry.path().filename().native();
        std::string encoded_name;
        try {
            encoded_name = utf8(name);
        } catch (...) {
            continue;
        }
        if (!hex(encoded_name, 64))
            continue;
        try {
            Store store(root.directory / name, false);
            auto state = store.state();
            struct WipeAuth {
                Json &state;
                ~WipeAuth() {
                    if (state.is_object() && state.contains("auth") && state["auth"].is_string()) {
                        auto &value = state["auth"].get_ref<std::string &>();
                        if (!value.empty())
                            SecureZeroMemory(value.data(), value.size());
                    }
                }
            } wipe{state};
            WorkspaceTools workspace(state.at("workspace").get<std::string>());
            shape(state, store, workspace);
            if (encoded_name != context_digest(workspace.identity()))
                continue;
            if (result.size() == 256) {
                exceeded_result_limit = true;
                break;
            }
            result.push_back({workspace.identity(), workspace.root_path(),
                              utf8(fs::u8path(workspace.root_path()).filename().native())});
        } catch (...) {
            // A damaged, stale, or inaccessible sibling profile must not make
            // a different valid workspace disappear from the catalogue.
        }
    }
    require(!exceeded_result_limit, "Local workspace catalogue exceeds its result limit");
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.workspace_id < right.workspace_id;
    });
    return result;
}
LocalProfileConnection connect_local_profile(const LocalProfileOptions &options) {
    if(options.agent_execution&&*options.agent_execution!="thread"&&*options.agent_execution!="process-read"&&*options.agent_execution!="process-code")throw std::invalid_argument("Agent execution must be thread, process-read or process-code");
    const auto requested_mcp_path=options.mcp_config.empty()?std::string{}:text(absolute(options.mcp_config));
    std::string requested_mcp_digest,requested_mcp_document;
    if(!options.mcp_config.empty()){
        const auto document=file_bytes(absolute(options.mcp_config),256*1024);
        validate_mcp_configuration_document(document);
        requested_mcp_digest=context_digest(document);
        requested_mcp_document=document;
    }
    WorkspaceTools workspace(options.workspace.empty() ? text(fs::current_path()) : options.workspace);
    const auto base = absolute(options.profile_root.empty() ? default_root() : options.profile_root),
               selected = absolute(workspace.root_path());
    if (contains(selected, base) || contains(base, selected))
        throw std::invalid_argument("Profile storage overlaps the selected workspace");
    Store root(base, !options.require_existing_profile);
    const auto profile_directory = root.directory / wide(context_digest(workspace.identity()));
    if (options.require_existing_profile)
        require(fs::exists(profile_directory / L"profile.state"),
                "Workspace is not present in the existing local profile catalogue");
    Store store(profile_directory, !options.require_existing_profile);
    auto lock = store.lock();
    Json state;
    bool started = false;
    bool recovered_stopped_owner = false;
    std::unique_ptr<VerifiedRuntimeGeneration> source_generation, runtime;
    if (fs::exists(store.directory / L"profile.state")) {
        state = store.state();
        shape(state, store, workspace);
        std::string loaded_manifest;
        if(options.upgrade_backend&&!state.contains("owner_receipt"))
            loaded_manifest=context_digest(file_bytes(absolute(loaded_root())/L"native-runtime-manifest.json",4*1024*1024));
        if(options.upgrade_backend&&!state.contains("owner_receipt")){
            const auto phase=state.value("phase",std::string{});
            const bool recoverable_phase=phase=="ready"||phase=="starting"||phase=="preparing";
            const auto previous_pid=state.at("pid").get<std::uint32_t>();
            const bool previous_owner_exited=!previous_pid||observe_owner_exit(previous_pid,state.value("birth",std::string{}),0).exited;
            if(recoverable_phase&&previous_owner_exited){
                // The protected profile record and process birth prove the old
                // owner is gone. Replace its generation directly: older saved
                // runtimes may no longer understand the current managed launch
                // arguments, so attempting to restart them can strand a healthy
                // SQLite profile in "starting" forever.
                const int saved_port=state.value("port",0);
                auto [replacement,digest]=stage_current_runtime(store,workspace);
                state["runtime"]=text(replacement);state["manifest"]=digest;
                state["phase"]="preparing";state["pid"]=0;state["birth"]="";state["port"]=saved_port;
                // An interrupted upgrade may have persisted its intent before
                // the old owner issued a quiescence receipt. If the owner is
                // gone and no receipt was persisted, retry as stopped-owner
                // recovery. A database that did commit quiescence still rejects
                // ordinary startup, preserving its admission fence.
                state.erase("upgrade_intent");state.erase("replacement_runtime");
                state.erase("replacement_manifest");state.erase("replacement_port");
                store.write(state);recovered_stopped_owner=true;
            }
        }
        for (const auto &[field, value] : std::array<std::pair<const char *, std::string>, 2>{
                 {{"graphs_config", options.graphs_config},
                  {"python_tools_config", options.python_tools_config}}})
            if (!value.empty())
                require(state.value(field, std::string{}) == text(absolute(value)),
                        "Profile launch configuration changed; update the existing backend explicitly");
        const auto requested_provider_path=options.provider_config.empty()?std::string{}:text(absolute(options.provider_config));
        const bool provider_configuration_changed=!options.provider_config.empty()&&
            state.value("provider_config",std::string{})!=requested_provider_path;
        if(state.contains("owner_receipt")&&provider_configuration_changed)
            throw std::invalid_argument("A backend handoff is already in progress; finish reconnecting before changing provider configuration");
        if(provider_configuration_changed&&!options.upgrade_backend)
            throw std::invalid_argument("Provider configuration changed; update the existing backend explicitly");
        if(provider_configuration_changed){
            // Validate before quiescence. Custom routes are admitted only by the
            // trusted document; existing routes come from the authenticated owner.
            std::vector<std::string> routes;
            if(!recovered_stopped_owner){
                require(state.at("phase")=="ready"&&authenticated(state),"Only an authenticated ready owner can validate a provider update");
                const auto providers=profile_request(state,"/v1/provider/profiles");
                for(const auto& route:providers.at("routes"))routes.push_back(route.at("id").get<std::string>());
            }else routes={"openai.chat","openai.responses","anthropic.messages","gemini.generate-content","deepseek.chat","xai.responses","mistral.chat","openrouter.chat"};
            (void)read_provider_yaml_config(absolute(options.provider_config),routes);
        }
        if((recovered_stopped_owner||state.contains("upgrade_intent"))&&provider_configuration_changed){
            state["provider_config"]=requested_provider_path;store.write(state);
        }
        const auto requested_language_server_path=options.language_server_config.empty()?std::string{}:text(absolute(options.language_server_config));
        const bool language_server_configuration_changed=!options.language_server_config.empty()&&
            state.value("language_server_config",std::string{})!=requested_language_server_path;
        if(state.contains("owner_receipt")&&language_server_configuration_changed)
            throw std::invalid_argument("A backend handoff is already in progress; finish reconnecting before changing language-server configuration");
        if(language_server_configuration_changed&&!options.upgrade_backend)
            throw std::invalid_argument("Profile launch configuration changed; update the existing backend explicitly");
        if(recovered_stopped_owner&&language_server_configuration_changed){
            state["language_server_config"]=requested_language_server_path;state["language_server_config_pending"]=true;store.write(state);
        }
        if(state.contains("upgrade_intent")&&language_server_configuration_changed){
            state["language_server_config"]=requested_language_server_path;state["language_server_config_pending"]=true;store.write(state);
        }
        const auto requested_process_path=options.process_config.empty()?std::string{}:text(absolute(options.process_config));
        const auto requested_process_digest=options.process_config.empty()?std::string{}:context_digest(file_bytes(absolute(options.process_config),256*1024));
        const bool process_configuration_changed=!options.process_config.empty()&&
            (state.value("process_config",std::string{})!=requested_process_path||
             state.value("process_config_digest",std::string{})!=requested_process_digest);
        if(state.contains("owner_receipt")&&process_configuration_changed)
            throw std::invalid_argument("A backend handoff is already in progress; finish reconnecting before changing process configuration");
        if(state.contains("owner_receipt")&&options.upgrade_backend&&!options.process_config.empty()&&!process_configuration_changed){
            // An explicit reimport of the same trusted document must reach the
            // replacement even when only the executable's file identity changed.
            state["process_config_pending"]=true;store.write(state);
        }
        if(process_configuration_changed&&!options.upgrade_backend)
            throw std::invalid_argument("Process configuration changed; update the existing backend explicitly");
        if(recovered_stopped_owner&&process_configuration_changed){
            state["process_config"]=requested_process_path;state["process_config_digest"]=requested_process_digest;state["process_config_pending"]=true;store.write(state);
        }
        if(state.contains("upgrade_intent")&&process_configuration_changed){
            state["process_config"]=requested_process_path;state["process_config_digest"]=requested_process_digest;state["process_config_pending"]=true;store.write(state);
        }
        const bool mcp_configuration_changed=!options.mcp_config.empty()&&
            (state.value("mcp_config",std::string{})!=requested_mcp_path||
             state.value("mcp_config_digest",std::string{})!=requested_mcp_digest);
        const bool web_configuration_changed=!options.web_assets.empty()&&
            (state.value("web_assets",std::string{})!=text(absolute(options.web_assets))||
             state.value("web_packages",std::string{})!=(options.web_packages.empty()?std::string{}:text(absolute(options.web_packages))));
        if(web_configuration_changed&&!options.upgrade_backend)
            throw std::invalid_argument("This backend does not host the requested web UI; use an explicit qualified backend upgrade");
        if(state.contains("owner_receipt")&&mcp_configuration_changed)
            throw std::invalid_argument("A backend handoff is already in progress; finish reconnecting before changing MCP configuration");
        if(mcp_configuration_changed&&!options.upgrade_backend)
            throw std::invalid_argument("MCP configuration changed; update the existing backend explicitly");
        if(mcp_configuration_changed&&!recovered_stopped_owner&&!state.contains("owner_receipt")){
            require(state.at("phase")=="ready"&&authenticated(state),"Only an authenticated ready owner can validate an MCP update");
            const auto ws=profile_request(state,"/v1/workspace");
            auto request=authority_body(ws);request["document"]=requested_mcp_document;
            try{
                const auto validated=profile_request(state,"/v1/backend/owner/mcp/validate",&request);
                require(validated.value("valid",false)&&validated.value("document_sha256",std::string{})==requested_mcp_digest,
                        "Managed owner did not validate this exact MCP update");
            }catch(const ProfileRequestError& error){
                if(error.status==404)throw std::invalid_argument("Upgrade this managed backend without --mcp-config first, then apply the trusted MCP configuration");
                throw;
            }
        }
        if((recovered_stopped_owner||state.contains("upgrade_intent"))&&mcp_configuration_changed){
            state["mcp_config"]=requested_mcp_path;state["mcp_config_digest"]=requested_mcp_digest;state["mcp_config_pending"]=true;store.write(state);
        }
        const bool execution_changed=options.agent_execution&&state.value("agent_execution",std::string{"thread"})!=*options.agent_execution;
        const bool policy_requested=bool(options.approved_edits)||bool(options.agent_execution);const auto requested_execution=options.agent_execution.value_or(state.value("agent_execution",std::string{"thread"}));const bool requested_edits=options.approved_edits.value_or(state.at("approved_edits").get<bool>());
        const bool edit_policy_changed=options.approved_edits&&
            state.at("approved_edits").get<bool>()!=*options.approved_edits;
        if(edit_policy_changed&&!options.upgrade_backend)
            throw std::invalid_argument("Profile edit policy changed; update the existing backend explicitly");
        if(state.contains("owner_receipt")&&edit_policy_changed)
            throw std::invalid_argument("A backend handoff is already in progress; finish reconnecting before changing workspace edit policy");
        if(execution_changed&&!options.upgrade_backend)throw std::invalid_argument("Profile execution mode changed; update the existing backend explicitly");
        if(state.contains("owner_receipt")&&execution_changed)throw std::invalid_argument("A backend handoff is already in progress; finish reconnecting before changing execution mode");
        if((requested_execution=="process-code"&&!requested_edits)||(requested_execution=="process-read"&&requested_edits))throw std::invalid_argument("Process execution mode differs from workspace edit policy");
        if(recovered_stopped_owner&&policy_requested){
            state["approved_edits"]=requested_edits;state["agent_execution"]=requested_execution;
            store.write(state);
        }
        if(state.contains("upgrade_intent")&&policy_requested){
            state["approved_edits"]=requested_edits;state["agent_execution"]=requested_execution;
            store.write(state);
        }
        if(state.contains("owner_receipt")||(options.upgrade_backend&&!recovered_stopped_owner&&(web_configuration_changed||provider_configuration_changed||process_configuration_changed||mcp_configuration_changed||language_server_configuration_changed||edit_policy_changed||execution_changed||state.contains("upgrade_intent")||state.at("manifest").get<std::string>()!=loaded_manifest))){
            bool deferred_busy_upgrade=false;
            if(!state.contains("owner_receipt")){
                if(state.contains("upgrade_intent")&&state.value("replacement_manifest",std::string{})!=loaded_manifest){
                    // A rejected preflight has not committed an owner handoff.
                    // An explicit upgrade may select another verified target
                    // only while the exact protected owner remains ready.
                    require(options.upgrade_backend&&state.at("phase")=="ready"&&authenticated(state),"Retargeting requires the authenticated ready owner");
                    const auto current_owner=profile_request(state,"/v1/backend/owner");
                    require(!current_owner.value("quiesced",true)&&!current_owner.value("retirement_requested",true)&&!current_owner.value("replacement_prepared",true),"A committed owner handoff cannot be retargeted");
                    auto [target,digest]=stage_current_runtime(store,workspace);
                    state["replacement_runtime"]=text(target);state["replacement_manifest"]=digest;state["replacement_port"]=state.at("port");
                    if(web_configuration_changed){state["web_assets"]=text(absolute(options.web_assets));state["web_packages"]=options.web_packages.empty()?std::string{}:text(absolute(options.web_packages));}
                    store.write(state);
                }
                if(!state.contains("upgrade_intent")){
                    require(state.at("phase")=="ready"&&authenticated(state),"Only an authenticated ready owner can be upgraded");
                    const auto current_owner=profile_request(state,"/v1/backend/owner");
                    require(current_owner.value("retirement_supported",false),"Current managed runtime does not support profile-preserving upgrade");
                    auto [target,digest]=stage_current_runtime(store,workspace);
                    state["replacement_runtime"]=text(target);state["replacement_manifest"]=digest;state["replacement_port"]=state.at("port");
                    if(provider_configuration_changed)state["provider_config"]=requested_provider_path;
                    if(process_configuration_changed){state["process_config"]=requested_process_path;state["process_config_digest"]=requested_process_digest;state["process_config_pending"]=true;}
                    if(mcp_configuration_changed){state["mcp_config"]=requested_mcp_path;state["mcp_config_digest"]=requested_mcp_digest;state["mcp_config_pending"]=true;}
                    if(language_server_configuration_changed){state["language_server_config"]=requested_language_server_path;state["language_server_config_pending"]=true;}
                    if(policy_requested){state["approved_edits"]=requested_edits;state["agent_execution"]=requested_execution;}
                    if(web_configuration_changed){state["web_assets"]=text(absolute(options.web_assets));state["web_packages"]=options.web_packages.empty()?std::string{}:text(absolute(options.web_packages));}
                    if(!state.value("web_assets",std::string{}).empty()&&!state.contains("web_port")){
                        const auto prior=store.directory/("web-"+std::to_string(state.at("pid").get<std::uint32_t>())+"-"+state.at("birth").get<std::string>()+".json");
                        if(fs::exists(prior)){
                            const auto metadata=parse(file_bytes(prior,16384));
                            require(metadata.at("process_id")==state.at("pid")&&metadata.at("process_birth")==state.at("birth")&&metadata.at("backend_process_id")==state.at("pid"),"Prior browser owner identity differs");
                            const auto origin=metadata.at("origin").get<std::string>();constexpr std::string_view prefix="http://127.0.0.1:";require(origin.starts_with(prefix),"Invalid prior browser address");
                            int port=0;const auto parsed=std::from_chars(origin.data()+prefix.size(),origin.data()+origin.size(),port);require(parsed.ec==std::errc{}&&parsed.ptr==origin.data()+origin.size()&&port>0&&port<=65535,"Invalid prior browser port");state["web_port"]=port;
                        }
                    }
                    state["upgrade_intent"]=true;store.write(state);
                }
                const auto ws=profile_request(state,"/v1/workspace");
                const auto owner=profile_request(state,"/v1/backend/owner");
                require(owner.value("retirement_supported",false),"Current managed runtime does not support profile-preserving upgrade");
                Json quiesced=owner;
                if(!owner.value("quiesced",false)){
                    Json request=authority_body(ws);request["expected_generation"]=owner.at("generation");request["expected_revision"]=owner.at("revision");
                    request["target_runtime_root"]=state.at("replacement_runtime");request["target_manifest_sha256"]=state.at("replacement_manifest");
                    try {
                        quiesced=profile_request(state,"/v1/backend/owner/quiesce",&request);
                    } catch(const ProfileRequestError& error) {
                        bool busy=false;
                        if(error.status==409)try{const auto detail=parse(error.body).value("detail",std::string{});busy=detail=="Native admission is in flight"||detail=="Native execution must be healthy and idle before owner control";}catch(...){}
                        if(options.defer_busy_upgrade&&busy)deferred_busy_upgrade=true;else throw;
                    }
                }
                if(!deferred_busy_upgrade){
                    require(quiesced.value("quiesced",false)&&quiesced.contains("receipt_id")&&quiesced.contains("revision"),"Backend did not issue a quiescence receipt");
                    state["owner_receipt"]={{"generation",quiesced.at("generation")},{"revision",quiesced.at("revision")},{"receipt_id",quiesced.at("receipt_id")}};
                    state.erase("upgrade_intent");store.write(state);
                }
            }
            if(!deferred_busy_upgrade){
                bool replacement_already_running=false;
                if(state.contains("owner_receipt")&&state.at("phase")=="ready"&&state.at("pid").get<std::uint32_t>()){
                    try{auto replacement=state;replacement["runtime"]=state.at("replacement_runtime");replacement["manifest"]=state.at("replacement_manifest");verify_process(replacement);replacement_already_running=true;}catch(...){}
                }
                if(!replacement_already_running)retire_for_upgrade(store,state,workspace,options.upgrade_backend);
            }
        }
    } else {
        for (const auto &item : fs::directory_iterator(store.directory))
            require(item.path().filename() == L"startup.lock",
                    "Profile record is missing but storage is not empty; operator recovery is required");
        if((options.agent_execution==std::optional<std::string>{"process-code"}&&!options.approved_edits.value_or(true))||(options.agent_execution==std::optional<std::string>{"process-read"}&&options.approved_edits.value_or(true)))throw std::invalid_argument("Process execution mode differs from workspace edit policy");
        const auto source = absolute(loaded_root());
        const auto raw = file_bytes(source / L"native-runtime-manifest.json", 4 * 1024 * 1024),
                   digest = context_digest(raw);
        source_generation = std::make_unique<VerifiedRuntimeGeneration>(text(source), digest);
        source_generation->require_loaded_server_image();
        const auto target = store.directory / (L"runtime-" + wide(random_id()));
        state = {
            {"schema", 1},
            {"phase", "preparing"},
            {"directory", text(store.directory)},
            {"workspace", workspace.root_path()},
            {"workspace_id", workspace.identity()},
            {"runtime", text(target)},
            {"manifest", digest},
            {"auth", random_id()},
            {"pid", static_cast<std::uint32_t>(0)},
            {"birth", ""},
            {"port", 0},
            {"approved_edits", options.approved_edits.value_or(true)},
            {"agent_execution",options.agent_execution.value_or("thread")},
            {"provider_config",
             options.provider_config.empty() ? "" : text(absolute(options.provider_config))},
            {"graphs_config", options.graphs_config.empty() ? "" : text(absolute(options.graphs_config))}};
        state["python_tools_config"]=options.python_tools_config.empty()?"":text(absolute(options.python_tools_config));
        state["web_assets"]=options.web_assets.empty()?"":text(absolute(options.web_assets));
        state["web_packages"]=options.web_packages.empty()?"":text(absolute(options.web_packages));
        state["language_server_config"]=options.language_server_config.empty()?"":text(absolute(options.language_server_config));
        state["process_config"]=options.process_config.empty()?"":text(absolute(options.process_config));
        state["process_config_digest"]=options.process_config.empty()?"":context_digest(file_bytes(absolute(options.process_config),256*1024));
        state["process_config_pending"]=!options.process_config.empty();
        state["mcp_config"]=requested_mcp_path;state["mcp_config_digest"]=requested_mcp_digest;
        state["mcp_config_pending"]=!options.mcp_config.empty();
        store.write(state);
    }
    if (state.at("phase") == "preparing") {
        if(state.contains("owner_receipt")){
            // A receipt binds recovery to the fully staged target generation.
            // The app package may have changed since staging; finish this
            // handoff from its verified private copy, then a later launch can
            // upgrade again to the newer package.
            runtime=std::make_unique<VerifiedRuntimeGeneration>(VerifiedRuntimeGeneration::managed_profile_copy(
                state.at("runtime").get<std::string>(),state.at("manifest").get<std::string>(),workspace.root_path()));
        } else {
            const auto source = absolute(loaded_root());
            const auto raw = file_bytes(source / L"native-runtime-manifest.json", 4 * 1024 * 1024);
            require(context_digest(raw) == state.at("manifest").get<std::string>(),
                    "Prepared profile belongs to a different accepted package; operator recovery is required");
            if (!source_generation) {
                source_generation = std::make_unique<VerifiedRuntimeGeneration>(
                    text(source), state.at("manifest").get<std::string>());
                source_generation->require_loaded_server_image();
            }
            const auto target = store.directory / (L"runtime-" + wide(random_id()));
            Store destination(target, true);
            state["runtime"] = text(destination.directory);
            store.write(state);
            const auto manifest = parse(raw);
            // Prepare and pin each destination directory once. Repeating recursive
            // directory creation for every library file makes a cold profile pay
            // the same path traversal thousands of times. Store applies the private
            // ACL and rejects aliases; its handles keep these paths pinned through
            // copying and the complete destination generation verification below.
            std::map<fs::path, std::unique_ptr<Store>> prepared_directories;
            for (const auto &[name, digest] : manifest.at("files").items()) {
                const auto path = fs::u8path(name);
                const auto parent = path.parent_path();
                if (!parent.empty() && !prepared_directories.contains(parent))
                    prepared_directories.emplace(parent, std::make_unique<Store>(destination.directory / parent, true));
                require(CopyFileW(extended(source / path).c_str(), extended(destination.directory / path).c_str(),
                                  TRUE) != 0,
                        "Cannot copy the verified profile runtime inventory");
            }
            auto output = destination.open(L"native-runtime-manifest.json", GENERIC_WRITE, CREATE_NEW);
            DWORD written = 0;
            require(WriteFile(output.value, raw.data(), static_cast<DWORD>(raw.size()), &written, nullptr) != 0 &&
                        written == raw.size() && FlushFileBuffers(output.value) != 0,
                    "Cannot publish the verified profile runtime manifest");
            CloseHandle(output.value);
            output.value = INVALID_HANDLE_VALUE;
            runtime = std::make_unique<VerifiedRuntimeGeneration>(
                VerifiedRuntimeGeneration::managed_profile_copy(text(destination.directory),
                                                                state.at("manifest").get<std::string>(),
                                                                workspace.root_path()));
        }
    }
    if (!runtime)
        runtime = std::make_unique<VerifiedRuntimeGeneration>(VerifiedRuntimeGeneration::managed_profile_copy(
            state.at("runtime").get<std::string>(), state.at("manifest").get<std::string>(),
            workspace.root_path()));
    std::unique_ptr<Child> child;
    const auto pid = state.at("pid").get<std::uint32_t>();
    bool exited = pid == 0;
    if (pid)
        exited = observe_owner_exit(pid, state.at("birth").get<std::string>(), 0).exited;
    if (exited) {
        state["phase"] = "preparing";
        store.write(state);
        child = std::make_unique<Child>(launch(store, state));
        started = true;
    } else
        verify_process(state);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (;;) {
        state = store.state();
        shape(state, store, workspace);
        if (state.at("phase") == "ready") {
            if (child && !child->detached)
                child->release();
            if (authenticated(state)){
                if(state.contains("owner_receipt")){
                    const auto ws=profile_request(state,"/v1/workspace");
                    auto owner=profile_request(state,"/v1/backend/owner");
                    if(owner.value("quiesced",false)){
                        Json request=authority_body(ws);request["expected_generation"]=owner.at("generation");request["expected_revision"]=owner.at("revision");request["receipt_id"]=owner.at("receipt_id");
                        profile_request(state,"/v1/backend/owner/activate",&request);
                        owner=profile_request(state,"/v1/backend/owner");
                    }
                    require(!owner.value("quiesced",true),"Replacement owner remains quiesced; reconnect to resume activation");
                    state.erase("owner_receipt");state.erase("replacement_port");state.erase("replacement_manifest");state.erase("replacement_runtime");store.write(state);
                }
                if(state.value("process_config_pending",false)){state.erase("process_config_pending");store.write(state);}
                if(state.value("mcp_config_pending",false)){state.erase("mcp_config_pending");store.write(state);}
                if(state.value("language_server_config_pending",false)){state.erase("language_server_config_pending");store.write(state);}
                break;
            }
        }
        if (observe_owner_exit(state.at("pid").get<std::uint32_t>(), state.at("birth").get<std::string>(), 0)
                .exited)
            throw std::runtime_error(
                "The owned local backend exited before readiness; its profile and database were preserved");
        require(std::chrono::steady_clock::now() < deadline,
                "Local backend readiness is unresolved; reconnect to the same profile. No competing owner "
                "was started");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const auto auth = state.at("auth").get<std::string>();
    return {workspace.root_path(),
            text(store.directory),
            state.at("port").get<std::uint16_t>(),
            state.at("pid").get<std::uint32_t>(),
            started,
            SecretBytes({reinterpret_cast<const std::uint8_t *>(auth.data()), auth.size()}),
            state.at("approved_edits").get<bool>(),state.value("agent_execution",std::string{"thread"}),state.value("ipc_endpoint",std::string{})};
}
void validate_local_profile_launch(const std::string &state_file,const std::string &workspace_root,
                                   const std::string &runtime_root,const std::string &manifest,
                                   const std::string &auth_token,const std::string &owner_receipt,
                                   const std::string &language_server_config,const std::string &mcp_config,
                                   std::string *mcp_document,const std::string &agent_execution,
                                   std::string *provider_route_config) {
    const auto path=absolute(state_file);
    require(path.filename()==L"profile.state"&&hex(manifest,64)&&hex(auth_token,64));
    Store store(path.parent_path());
    WorkspaceTools workspace(workspace_root);
    const auto state=store.state();
    shape(state,store,workspace);
    const auto pid=GetCurrentProcessId();
    require(state.at("phase")=="starting"&&state.at("pid")==pid&&
                state.at("birth")==inspect_owner_process_birth(pid)&&
                state.at("runtime")==text(absolute(runtime_root))&&
                state.at("manifest")==manifest&&state.at("auth")==auth_token&&state.value("agent_execution",std::string{"thread"})==agent_execution,
            "Managed profile runtime does not match its protected startup record");
    const auto requested_mcp=mcp_config.empty()?std::string{}:text(absolute(mcp_config));
    require(state.value("mcp_config_pending",false)
                ?(!requested_mcp.empty()&&state.value("mcp_config",std::string{})==requested_mcp)
                :requested_mcp.empty(),
            "Managed profile MCP override differs from its protected startup intent");
    if(!requested_mcp.empty()){
        const auto document=file_bytes(absolute(requested_mcp),256*1024);
        require(context_digest(document)==state.value("mcp_config_digest",std::string{}),
                "Managed profile MCP configuration changed after startup was prepared");
        validate_mcp_configuration_document(document);
        if(mcp_document)*mcp_document=document;
    }
    if(owner_receipt.empty())require(!state.contains("owner_receipt"),
            "Managed profile replacement receipt was not supplied to the server");
    else{
        const auto first=owner_receipt.find(':'),last=owner_receipt.rfind(':');
        require(first==32&&last>first&&last+33==owner_receipt.size()&&
                state.contains("owner_receipt")&&
                state.at("owner_receipt").at("generation")==owner_receipt.substr(0,first)&&
                state.at("owner_receipt").at("revision")==std::stoll(owner_receipt.substr(first+1,last-first-1))&&
                state.at("owner_receipt").at("receipt_id")==owner_receipt.substr(last+1),
                "Managed profile replacement receipt differs from its protected startup record");
        const auto requested_language_server=language_server_config.empty()?std::string{}:text(absolute(language_server_config));
        const bool pending_language_server=state.value("language_server_config_pending",false);
        require(pending_language_server?(!requested_language_server.empty()&&
                    state.value("language_server_config",std::string{})==requested_language_server):requested_language_server.empty(),
                "Managed profile language-server override differs from its protected upgrade intent");
    }
    // Expose only the path already owned by this authenticated startup record.
    // It defines routes; replacement startup must not reimport profile secrets.
    if(provider_route_config)*provider_route_config=owner_receipt.empty()?std::string{}:state.value("provider_config",std::string{});
}
void publish_local_profile_ready(const std::string &state_file, int port, const std::string &token,
                                 const std::string &workspace_root, const std::string &workspace_id,
                                 const std::string &authority,const std::string &ipc_endpoint) {
    const auto path = absolute(state_file);
    require(path.filename() == L"profile.state" && port > 0 && port <= 65535);
    Store store(path.parent_path());
    WorkspaceTools workspace(workspace_root);
    auto state = store.state();
    shape(state, store, workspace);
    require(state.at("phase") == "starting" && state.at("pid") == GetCurrentProcessId() &&
                state.at("birth") == inspect_owner_process_birth(GetCurrentProcessId()) &&
                state.at("auth") == token && state.at("workspace_id") == workspace_id &&
                state.at("runtime") == loaded_root(),
            "Native profile publication does not match its prepared process");
    require(hex(authority, 32));
    state["authority"] = authority;
    state["port"] = port;
    if(!ipc_endpoint.empty()){
        require(ipc_endpoint.starts_with("lrpc:")&&ipc_endpoint.size()<=16,"Invalid profile IPC endpoint");
        state["ipc_endpoint"]=ipc_endpoint;
    }
    state["phase"] = "ready";
    store.write(state);
}
LocalProfileOptions managed_web_view_options(const std::string& directory){
    Store store(absolute(directory));const auto state=store.state();
    WorkspaceTools workspace(state.at("workspace").get<std::string>());shape(state,store,workspace);
    require(state.at("phase")=="ready"&&state.at("pid")==GetCurrentProcessId()&&state.at("birth")==inspect_owner_process_birth(GetCurrentProcessId())&&state.at("runtime")==loaded_root(),"Browser configuration requires its exact managed owner");
    LocalProfileOptions options;
    options.provider_config=state.value("provider_config",std::string{});options.graphs_config=state.value("graphs_config",std::string{});
    options.python_tools_config=state.value("python_tools_config",std::string{});options.language_server_config=state.value("language_server_config",std::string{});
    options.process_config=state.value("process_config",std::string{});options.mcp_config=state.value("mcp_config",std::string{});
    options.approved_edits=state.at("approved_edits").get<bool>();options.agent_execution=state.value("agent_execution",std::string{"thread"});return options;
}
std::string inspect_managed_web_owner(const std::string& directory){
    Store store(absolute(directory));const auto state=store.state();WorkspaceTools workspace(state.at("workspace").get<std::string>());shape(state,store,workspace);verify_process(state);
    return profile_request(state,"/v1/backend/owner").dump();
}
std::string finish_retired_managed_web_owner(const std::string& directory){
    Store store(absolute(directory));auto startup=store.lock();const auto state=store.state();
    WorkspaceTools workspace(state.at("workspace").get<std::string>());shape(state,store,workspace);
    require(!state.value("web_assets",std::string{}).empty()&&state.contains("owner_receipt")&&state.contains("replacement_runtime")&&state.contains("replacement_manifest"),"Only an acknowledged managed web handoff can finish retirement");
    verify_process(state);const auto owner=profile_request(state,"/v1/backend/owner");const auto& receipt=state.at("owner_receipt");
    require(owner.value("quiesced",false)&&owner.value("retirement_requested",false)&&!owner.value("replacement_prepared",false)&&owner.contains("bootstrap_receipt")&&owner.at("bootstrap_receipt")==receipt&&owner.at("generation")==receipt.at("generation")&&owner.at("receipt_id")==receipt.at("receipt_id")&&owner.at("revision")>=receipt.at("revision")&&owner.at("process_id")==state.at("pid")&&owner.at("process_birth")==state.at("birth"),"Native retirement has not been acknowledged; process was preserved");
    const auto pid=state.at("pid").get<std::uint32_t>();Handle process(OpenProcess(PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));require(process.value!=nullptr,"Cannot open acknowledged retired process");
    FILETIME created{},exit{},kernel{},user{};require(GetProcessTimes(process.value,&created,&exit,&kernel,&user)!=0,"Cannot verify retired process birth");
    const auto birth=std::to_string((static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime);require(birth==state.at("birth").get<std::string>(),"Retired process identity changed");
    std::wstring image(32768,L'\0');DWORD size=static_cast<DWORD>(image.size());require(QueryFullProcessImageNameW(process.value,0,image.data(),&size)!=0,"Cannot verify retired image");image.resize(size);if(image.starts_with(L"\\\\?\\"))image.erase(0,4);
    require(same(fs::path(image).lexically_normal().native(),(absolute(state.at("runtime").get<std::string>())/L"xmind.exe").native()),"Retired process image differs");
    require(TerminateProcess(process.value,ERROR_PROCESS_ABORTED)!=0&&WaitForSingleObject(process.value,10000)==WAIT_OBJECT_0,"Acknowledged retired process exit remains unresolved");
    return Json{{"process_id",pid},{"process_birth",birth},{"native_retirement_acknowledged",true},{"physical_exit_confirmed",true},{"forced_after_retirement",true},{"profile_preserved",true}}.dump();
}
void publish_managed_web_port(const std::string& directory,int port){
    require(port>0&&port<=65535,"Invalid managed browser port");Store store(absolute(directory));auto startup=store.lock();auto state=store.state();
    require(state.at("phase")=="ready"&&state.at("pid")==GetCurrentProcessId()&&state.at("birth")==inspect_owner_process_birth(GetCurrentProcessId())&&state.at("runtime")==loaded_root(),"Browser port requires its exact managed owner");
    state["web_port"]=port;store.write(state);
}
void validate_local_view_ready(const std::string &file, const std::string &workspace_root) {
    const auto target = absolute(file);
    WorkspaceTools workspace(workspace_root);
    require(!contains(absolute(workspace.root_path()), target.parent_path()) &&
                !contains(target.parent_path(), absolute(workspace.root_path())),
            "View rendezvous overlaps the selected workspace");
    Store store(target.parent_path(), true);
    require(!fs::exists(target), "View rendezvous already exists; its data was preserved");
}
void publish_local_view_ready(const std::string &file, const std::string &metadata,
                              const std::string &workspace_root) {
    validate_local_view_ready(file, workspace_root);
    const auto target = absolute(file);
    Store store(target.parent_path());
    const auto value = parse(metadata);
    require(value.is_object() && !value.contains("auth") && !value.contains("token") &&
            metadata.size() <= 16384);
    const auto pending = wide("view-" + random_id() + ".tmp");
    auto output = store.open(pending.c_str(), GENERIC_WRITE, CREATE_NEW);
    DWORD written = 0;
    require(
        WriteFile(output.value, metadata.data(), static_cast<DWORD>(metadata.size()), &written, nullptr) &&
        written == metadata.size() && FlushFileBuffers(output.value));
    CloseHandle(output.value);
    output.value = INVALID_HANDLE_VALUE;
    // Existence is the readiness signal. Publish only the complete, closed
    // record, and never replace an existing rendezvous supplied by a caller.
    require(MoveFileExW(extended(store.directory / pending).c_str(),extended(target).c_str(),
                       MOVEFILE_WRITE_THROUGH) != 0,
            "Cannot publish complete view rendezvous; existing file was preserved");
}
namespace {
void client_auth_origin(const std::string& origin) {
    constexpr std::string_view base="http://127.0.0.1";
    if(origin==base)return;
    require(origin.starts_with(std::string(base)+":"),"Client authentication requires a canonical loopback origin");
    const auto port=std::string_view(origin).substr(base.size()+1);int number=0;
    const auto parsed=std::from_chars(port.data(),port.data()+port.size(),number);
    require(parsed.ec==std::errc{}&&parsed.ptr==port.data()+port.size()&&number>0&&number<=65535&&
            std::to_string(number)==port&&number!=80,"Client authentication requires a canonical loopback origin");
}
Handle client_auth_file(Store& store) {
    return store.open(L"client-auth.sqlite",GENERIC_READ|GENERIC_WRITE,OPEN_ALWAYS,FILE_SHARE_READ|FILE_SHARE_WRITE);
}
void client_auth_schema(XlangSqlite& db) {
    db.execute("CREATE TABLE IF NOT EXISTS client_auth(origin TEXT PRIMARY KEY NOT NULL,protection TEXT NOT NULL,ciphertext BLOB NOT NULL)");
}
std::string client_auth_context(const Store& store,const std::string& origin) {
    return store.context()+":vscode-backend-auth:"+origin;
}
}
void save_client_auth(const std::string& directory,const std::vector<std::string>& roots,
                      const std::string& origin,const SecretBytes& token,bool replace) {
    client_auth_origin(origin);
    validate_local_auth_token({reinterpret_cast<const char*>(token.view().data()),token.view().size()});
    Store store(absolute(directory),true);auto lock=store.lock();auto file=client_auth_file(store);
    const auto protected_token=protect_secret(token,client_auth_context(store,origin));
    XlangSqlite db(text(store.directory/L"client-auth.sqlite"),roots);client_auth_schema(db);
    db.execute(replace?
        "INSERT INTO client_auth VALUES(?,?,?) ON CONFLICT(origin) DO UPDATE SET protection=excluded.protection,ciphertext=excluded.ciphertext":
        "INSERT INTO client_auth VALUES(?,?,?) ON CONFLICT(origin) DO NOTHING",
        {origin,protected_token.protection,protected_token.ciphertext});
}
std::optional<SecretBytes> load_client_auth(const std::string& directory,
    const std::vector<std::string>& roots,const std::string& origin) {
    client_auth_origin(origin);const auto target=absolute(directory);
    if(!fs::exists(target))return {};
    Store store(target);auto lock=store.lock();
    if(!fs::exists(store.directory/L"client-auth.sqlite"))return {};
    auto file=client_auth_file(store);
    XlangSqlite db(text(store.directory/L"client-auth.sqlite"),roots);client_auth_schema(db);
    const auto rows=db.execute("SELECT protection,ciphertext FROM client_auth WHERE origin=?",{origin}).rows;
    if(rows.empty())return {};
    require(rows.size()==1&&rows[0].size()==2&&std::holds_alternative<std::string>(rows[0][0])&&
            std::holds_alternative<SqlBytes>(rows[0][1]),"Invalid protected client authentication record");
    auto token=reveal_secret({std::get<std::string>(rows[0][0]),std::get<SqlBytes>(rows[0][1])},client_auth_context(store,origin));
    validate_local_auth_token({reinterpret_cast<const char*>(token.view().data()),token.view().size()});
    return token;
}
void delete_client_auth(const std::string& directory,const std::vector<std::string>& roots,
                        const std::string& origin) {
    client_auth_origin(origin);const auto target=absolute(directory);if(!fs::exists(target))return;
    Store store(target);auto lock=store.lock();if(!fs::exists(store.directory/L"client-auth.sqlite"))return;
    auto file=client_auth_file(store);XlangSqlite db(text(store.directory/L"client-auth.sqlite"),roots);client_auth_schema(db);
    db.execute("DELETE FROM client_auth WHERE origin=?",{origin});
}
int run_client_auth(int argc,char** argv) {
    require(argc==4,"Private client-auth requires ACTION DIRECTORY ORIGIN");
    const std::string action=argv[1],directory=argv[2],origin=argv[3];
    require(action=="get"||action=="put"||action=="seed"||action=="delete","Invalid private client-auth action");
    const auto runtime=absolute(loaded_root());const std::vector<std::string> roots{text(runtime/L"modules"),text(runtime/L"stdlib")};
    if(action=="put"||action=="seed") {
        std::string source;char byte;
        while(std::cin.get(byte)){require(source.size()<1024,"Client authentication input exceeds limits");source+=byte;}
        auto request=parse(source);SecureZeroMemory(source.data(),source.size());
        require(request.is_object()&&request.size()==1&&request.contains("token")&&request["token"].is_string(),"Invalid private client authentication input");
        auto& plain=request["token"].get_ref<std::string&>();
        SecretBytes token({reinterpret_cast<const std::uint8_t*>(plain.data()),plain.size()});SecureZeroMemory(plain.data(),plain.size());
        save_client_auth(directory,roots,origin,token,action=="put");std::cout<<"{\"saved\":true}\n";
    }else if(action=="delete") {
        delete_client_auth(directory,roots,origin);std::cout<<"{\"deleted\":true}\n";
    }else {
        auto token=load_client_auth(directory,roots,origin);
        if(!token)std::cout<<"{\"token\":null}\n";
        else {
            std::cout<<"{\"token\":\"";
            for(const auto byte:token->view()){if(byte=='\\'||byte=='"')std::cout.put('\\');std::cout.put(static_cast<char>(byte));}
            std::cout<<"\"}\n";
        }
    }
    return 0;
}
} // namespace agentflow
