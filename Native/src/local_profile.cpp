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
void require(bool condition,
             const char *message = "Local profile validation failed; stored state was preserved") {
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
        const auto sddl = L"D:P(A;;FA;;;SY)(A;;FA;;;" + std::wstring(sid) + L")";
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
            if (create && !fs::exists(part)) {
                require(CreateDirectoryW(part.c_str(), &security.attributes) != 0 ||
                            GetLastError() == ERROR_ALREADY_EXISTS,
                        "Cannot create protected local profile directory");
            }
            Handle held(CreateFileW(part.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
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
        Handle file(CreateFileW((directory / name).c_str(), access | READ_CONTROL, share,
                                &security.attributes, creation,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        require(file.value != INVALID_HANDLE_VALUE);
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
        require(MoveFileExW((directory / name).c_str(), (directory / L"profile.state").c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
                "Cannot publish local profile record; stored database was preserved");
    }
    Handle lock() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
        for (;;) {
            Handle value(CreateFileW((directory / L"startup.lock").c_str(),
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
            value.at("schema") == 1 &&
            value.value("workspace", std::string{}) == workspace.root_path() &&
            value.value("workspace_id", std::string{}) == workspace.identity() &&
            value.value("directory", std::string{}) == text(store.directory) &&
            hex(value.value("auth", std::string{}), 64) && hex(value.value("manifest", std::string{}), 64) &&
            value.contains("pid") && value.at("pid").is_number_unsigned() &&
            value.at("pid").get<std::uint64_t>() <= MAXDWORD && value.contains("port") &&
            value.at("port").is_number_integer() && value.at("port") >= 0 && value.at("port") <= 65535 &&
            value.contains("approved_edits") && value.at("approved_edits").is_boolean());
    require(value.value("phase", std::string{}) == "preparing" ||
            value.value("phase", std::string{}) == "starting" ||
            value.value("phase", std::string{}) == "ready");
    require(contains(store.directory, absolute(value.at("runtime").get<std::string>())),
            "Saved runtime escaped its private profile directory");
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
                                      inherited_handles.data(), sizeof(inherited_handles), nullptr, nullptr) != 0);
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
                                   L"0",
                                   L"--runtime-manifest-sha256",
                                   wide(state.at("manifest").get<std::string>()),
                                   L"--profile-state",
                                   (store.directory / L"profile.state").native()};
    if (state.at("approved_edits").get<bool>()) {
        args.push_back(L"--workspace-edits");
        args.push_back(L"approved");
    }
    for (const auto *field : {"provider_config", "graphs_config"}) {
        const auto value = state.value(field, std::string{});
        if (!value.empty()) {
            args.push_back(std::string(field) == "provider_config" ? L"--provider-config"
                                                                   : L"--graphs-config");
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
} // namespace
LocalProfileConnection connect_local_profile(const LocalProfileOptions &options) {
    WorkspaceTools workspace(options.workspace.empty() ? text(fs::current_path()) : options.workspace);
    const auto base = absolute(options.profile_root.empty() ? default_root() : options.profile_root),
               selected = absolute(workspace.root_path());
    require(!contains(selected, base) && !contains(base, selected),
            "Profile storage overlaps the selected workspace");
    Store root(base, true);
    Store store(root.directory / wide(context_digest(workspace.identity())), true);
    auto lock = store.lock();
    Json state;
    bool started = false;
    if (fs::exists(store.directory / L"profile.state")) {
        state = store.state();
        shape(state, store, workspace);
        for (const auto &[field, value] : std::array<std::pair<const char *, std::string>, 2>{
                 {{"provider_config", options.provider_config}, {"graphs_config", options.graphs_config}}})
            if (!value.empty())
                require(state.value(field, std::string{}) == text(absolute(value)),
                        "Profile launch configuration changed; update the existing backend explicitly");
        if (options.approved_edits)
            require(state.at("approved_edits") == *options.approved_edits,
                    "Profile edit policy changed; update the existing backend explicitly");
    } else {
        for (const auto &item : fs::directory_iterator(store.directory))
            require(item.path().filename() == L"startup.lock",
                    "Profile record is missing but storage is not empty; operator recovery is required");
        const auto source = absolute(loaded_root());
        const auto raw = file_bytes(source / L"native-runtime-manifest.json", 4 * 1024 * 1024),
                   digest = context_digest(raw);
        VerifiedRuntimeGeneration verified(text(source), digest);
        verified.require_current_server();
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
        store.write(state);
    }
    if (state.at("phase") == "preparing") {
        const auto source = absolute(loaded_root());
        const auto raw = file_bytes(source / L"native-runtime-manifest.json", 4 * 1024 * 1024);
        require(context_digest(raw) == state.at("manifest").get<std::string>(),
                "Prepared profile belongs to a different accepted package; operator recovery is required");
        VerifiedRuntimeGeneration verified(text(source), state.at("manifest").get<std::string>());
        verified.require_current_server();
        const auto target = store.directory / (L"runtime-" + wide(random_id()));
        Store destination(target, true);
        state["runtime"] = text(destination.directory);
        store.write(state);
        const auto manifest = parse(raw);
        for (const auto &[name, digest] : manifest.at("files").items()) {
            const auto path = fs::u8path(name);
            fs::create_directories(fs::path(extended(destination.directory / path.parent_path())));
            require(CopyFileW(extended(source / path).c_str(), extended(destination.directory / path).c_str(),
                              TRUE) != 0,
                    "Cannot copy the verified profile runtime inventory");
        }
        std::ofstream output(destination.directory / L"native-runtime-manifest.json", std::ios::binary);
        output.write(raw.data(), static_cast<std::streamsize>(raw.size()));
        output.close();
        VerifiedRuntimeGeneration retained(text(destination.directory),
                                           state.at("manifest").get<std::string>(), workspace.root_path());
        retained.revalidate();
    }
    VerifiedRuntimeGeneration runtime(state.at("runtime").get<std::string>(),
                                      state.at("manifest").get<std::string>(), workspace.root_path());
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
            if (authenticated(state))
                break;
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
} // namespace agentflow
