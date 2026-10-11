#include "agentflow/local_profile.hpp"
#include "agentflow/context_records.hpp"
#include "agentflow/owner_process.hpp"
#include "agentflow/runtime_generation.hpp"
#include "agentflow/workspace_tools.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>

#include <aclapi.h>
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sddl.h>
#include <set>
#include <shlobj.h>
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
                        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                        !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                        same(final_path(held.value), part.native()),
                    "Local profile path contains an alias or changed directory");
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
        require(MoveFileExW(extended(directory / name).c_str(), extended(directory / L"profile.state").c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
                "Cannot publish local profile record; stored database was preserved");
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
    PWSTR value = nullptr;
    require(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &value)),
            "Cannot locate local xMind profile storage");
    const auto path = fs::path(value) / L"xMind" / L"LocalProfiles";
    CoTaskMemFree(value);
    return text(path);
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
    for(const auto* field:{"provider_config","graphs_config","python_tools_config","language_server_config","process_config"})
        if(value.contains(field)&&!value.at(field).get<std::string>().empty())require(absolute(value.at(field).get<std::string>()).is_absolute());
    require((!value.contains("process_config")||value.at("process_config").get<std::string>().empty())
                ?(!value.contains("process_config_digest")||value.at("process_config_digest").get<std::string>().empty())
                :hex(value.value("process_config_digest",std::string{}),64),
            "Saved process configuration fingerprint is invalid");
    if(value.contains("process_config_pending"))require(value.at("process_config_pending").is_boolean()&&
        (!value.at("process_config_pending").get<bool>()||(value.contains("process_config")&&!value.at("process_config").get<std::string>().empty())),
        "Saved process configuration update is invalid");
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
    std::vector<std::wstring> args{program.native(),
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
                                   state.contains("replacement_port")?std::to_wstring(state.at("replacement_port").get<int>()):L"0",
                                   L"--runtime-manifest-sha256",
                                   wide(state.at("manifest").get<std::string>()),
                                   L"--profile-state",
                                   (store.directory / L"profile.state").native()};
    args.push_back(L"--workspace-registry");
    args.push_back((store.directory.parent_path()/L"workspaces.sqlite").native());
    if(state.contains("owner_receipt")){
        const auto& receipt=state.at("owner_receipt");
        args.push_back(L"--owner-receipt");
        args.push_back(wide(receipt.at("generation").get<std::string>()+":"+
            std::to_string(receipt.at("revision").get<std::int64_t>())+":"+
            receipt.at("receipt_id").get<std::string>()));
    }
    if (state.at("approved_edits").get<bool>()) {
        args.push_back(L"--workspace-edits");
        args.push_back(L"approved");
    }
    for (const auto *field : {"provider_config", "graphs_config", "python_tools_config", "language_server_config", "process_config"}) {
        if(state.contains("owner_receipt")&&std::string(field)!="process_config")continue;
        if(std::string(field)=="process_config"&&!state.value("process_config_pending",false))continue;
        const auto value = state.value(field, std::string{});
        if (!value.empty()) {
            const auto option=std::string(field)=="provider_config"?L"--provider-config":
                (std::string(field)=="graphs_config"?L"--graphs-config":
                 (std::string(field)=="python_tools_config"?L"--python-tools-config":
                  (std::string(field)=="process_config"?L"--process-config":L"--language-server-config")));
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
    require(CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, TRUE,
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
bool authenticated(const Json &state) {
    try {
        verify_process(state);
        httplib::Client client("127.0.0.1", state.at("port").get<int>());
        client.set_connection_timeout(1, 0);
        client.set_read_timeout(2, 0);
        client.set_follow_location(false);
        const httplib::Headers headers{{"Authorization", "Bearer " + state.at("auth").get<std::string>()}};
        const auto workspace = client.Get("/v1/workspace", headers),
                   owner = client.Get("/v1/backend/owner", headers);
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
void retire_for_upgrade(Store& store,Json& state,const WorkspaceTools& workspace){
    require(state.contains("owner_receipt")&&state.contains("replacement_port")&&state.contains("replacement_manifest")&&state.contains("replacement_runtime"),"Incomplete persisted backend handoff intent; profile was preserved");
    const auto pid=state.at("pid").get<std::uint32_t>();const auto birth=state.value("birth",std::string{});
    if(pid&&!observe_owner_exit(pid,birth,0).exited){
        try{
            Json ws=profile_request(state.at("port").get<int>(),state.at("auth").get<std::string>(),"/v1/workspace");
            Json body=authority_body(ws);const auto& receipt=state.at("owner_receipt");
            body["expected_generation"]=receipt.at("generation");body["expected_revision"]=receipt.at("revision");body["receipt_id"]=receipt.at("receipt_id");
            body["target_runtime_root"]=state.at("replacement_runtime");body["target_manifest_sha256"]=state.at("replacement_manifest");body["approved_edits"]=state.at("approved_edits");
            profile_request(state.at("port").get<int>(),state.at("auth").get<std::string>(),"/v1/backend/owner/retire",&body);
        }catch(...){/* retirement may commit before its response or listener remains available */}
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(pid&&!observe_owner_exit(pid,birth,0).exited&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(!pid||observe_owner_exit(pid,birth,0).exited,"Backend retirement is unresolved; reconnect to this profile to resume the same handoff");
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
    std::unique_ptr<VerifiedRuntimeGeneration> source_generation, runtime;
    if (fs::exists(store.directory / L"profile.state")) {
        state = store.state();
        shape(state, store, workspace);
        std::string loaded_manifest;
        if(options.upgrade_backend&&!state.contains("owner_receipt")&&!state.contains("upgrade_intent"))
            loaded_manifest=context_digest(file_bytes(absolute(loaded_root())/L"native-runtime-manifest.json",4*1024*1024));
        for (const auto &[field, value] : std::array<std::pair<const char *, std::string>, 4>{
                 {{"provider_config", options.provider_config}, {"graphs_config", options.graphs_config},
                  {"python_tools_config", options.python_tools_config}, {"language_server_config", options.language_server_config}}})
            if (!value.empty())
                require(state.value(field, std::string{}) == text(absolute(value)),
                        "Profile launch configuration changed; update the existing backend explicitly");
        const auto requested_process_path=options.process_config.empty()?std::string{}:text(absolute(options.process_config));
        const auto requested_process_digest=options.process_config.empty()?std::string{}:context_digest(file_bytes(absolute(options.process_config),256*1024));
        const bool process_configuration_changed=!options.process_config.empty()&&
            (state.value("process_config",std::string{})!=requested_process_path||
             state.value("process_config_digest",std::string{})!=requested_process_digest);
        if(state.contains("owner_receipt")&&process_configuration_changed)
            throw std::invalid_argument("A backend handoff is already in progress; finish reconnecting before changing process configuration");
        if(process_configuration_changed&&!options.upgrade_backend)
            throw std::invalid_argument("Process configuration changed; update the existing backend explicitly");
        if(state.contains("upgrade_intent")&&process_configuration_changed){
            state["process_config"]=requested_process_path;state["process_config_digest"]=requested_process_digest;state["process_config_pending"]=true;store.write(state);
        }
        if (options.approved_edits)
            require(state.at("approved_edits") == *options.approved_edits,
                    "Profile edit policy changed; update the existing backend explicitly");
        if(state.contains("owner_receipt")||(options.upgrade_backend&&(process_configuration_changed||state.contains("upgrade_intent")||state.at("manifest").get<std::string>()!=loaded_manifest))){
            bool deferred_busy_upgrade=false;
            if(!state.contains("owner_receipt")){
                if(!state.contains("upgrade_intent")){
                    require(state.at("phase")=="ready"&&authenticated(state),"Only an authenticated ready owner can be upgraded");
                    const auto current_owner=profile_request(state.at("port").get<int>(),state.at("auth").get<std::string>(),"/v1/backend/owner");
                    require(current_owner.value("retirement_supported",false),"Current managed runtime does not support profile-preserving upgrade");
                    auto [target,digest]=stage_current_runtime(store,workspace);
                    state["replacement_runtime"]=text(target);state["replacement_manifest"]=digest;state["replacement_port"]=state.at("port");
                    if(process_configuration_changed){state["process_config"]=requested_process_path;state["process_config_digest"]=requested_process_digest;state["process_config_pending"]=true;}
                    state["upgrade_intent"]=true;store.write(state);
                }
                const auto ws=profile_request(state.at("port").get<int>(),state.at("auth").get<std::string>(),"/v1/workspace");
                const auto owner=profile_request(state.at("port").get<int>(),state.at("auth").get<std::string>(),"/v1/backend/owner");
                require(owner.value("retirement_supported",false),"Current managed runtime does not support profile-preserving upgrade");
                Json quiesced=owner;
                if(!owner.value("quiesced",false)){
                    Json request=authority_body(ws);request["expected_generation"]=owner.at("generation");request["expected_revision"]=owner.at("revision");
                    try {
                        quiesced=profile_request(state.at("port").get<int>(),state.at("auth").get<std::string>(),"/v1/backend/owner/quiesce",&request);
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
                if(!replacement_already_running)retire_for_upgrade(store,state,workspace);
            }
        }
    } else {
        for (const auto &item : fs::directory_iterator(store.directory))
            require(item.path().filename() == L"startup.lock",
                    "Profile record is missing but storage is not empty; operator recovery is required");
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
            {"provider_config",
             options.provider_config.empty() ? "" : text(absolute(options.provider_config))},
            {"graphs_config", options.graphs_config.empty() ? "" : text(absolute(options.graphs_config))}};
        state["python_tools_config"]=options.python_tools_config.empty()?"":text(absolute(options.python_tools_config));
        state["language_server_config"]=options.language_server_config.empty()?"":text(absolute(options.language_server_config));
        state["process_config"]=options.process_config.empty()?"":text(absolute(options.process_config));
        state["process_config_digest"]=options.process_config.empty()?"":context_digest(file_bytes(absolute(options.process_config),256*1024));
        state["process_config_pending"]=!options.process_config.empty();
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
                    const auto ws=profile_request(state.at("port").get<int>(),state.at("auth").get<std::string>(),"/v1/workspace");
                    auto owner=profile_request(state.at("port").get<int>(),state.at("auth").get<std::string>(),"/v1/backend/owner");
                    if(owner.value("quiesced",false)){
                        Json request=authority_body(ws);request["expected_generation"]=owner.at("generation");request["expected_revision"]=owner.at("revision");request["receipt_id"]=owner.at("receipt_id");
                        profile_request(state.at("port").get<int>(),state.at("auth").get<std::string>(),"/v1/backend/owner/activate",&request);
                        owner=profile_request(state.at("port").get<int>(),state.at("auth").get<std::string>(),"/v1/backend/owner");
                    }
                    require(!owner.value("quiesced",true),"Replacement owner remains quiesced; reconnect to resume activation");
                    state.erase("owner_receipt");state.erase("replacement_port");state.erase("replacement_manifest");state.erase("replacement_runtime");store.write(state);
                }
                if(state.value("process_config_pending",false)){state.erase("process_config_pending");store.write(state);}
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
            SecretBytes({reinterpret_cast<const std::uint8_t *>(auth.data()), auth.size()})};
}
void validate_local_profile_launch(const std::string &state_file,const std::string &workspace_root,
                                   const std::string &runtime_root,const std::string &manifest,
                                   const std::string &auth_token,const std::string &owner_receipt) {
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
                state.at("manifest")==manifest&&state.at("auth")==auth_token,
            "Managed profile runtime does not match its protected startup record");
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
    }
}
void publish_local_profile_ready(const std::string &state_file, int port, const std::string &token,
                                 const std::string &workspace_root, const std::string &workspace_id,
                                 const std::string &authority) {
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
    state["phase"] = "ready";
    store.write(state);
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
    auto output = store.open(target.filename().c_str(), GENERIC_WRITE, CREATE_NEW);
    DWORD written = 0;
    require(
        WriteFile(output.value, metadata.data(), static_cast<DWORD>(metadata.size()), &written, nullptr) &&
        written == metadata.size() && FlushFileBuffers(output.value));
}
} // namespace agentflow
