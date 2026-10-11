#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        result.push_back(ch);
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

int fail(const char* message) {
    std::cerr << message << " (Win32 " << GetLastError() << ")\n";
    return 1;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { std::cerr << "Usage: conpty_cli_harness EXECUTABLE [ARG... ]\n"; return 2; }
    HANDLE input_read = nullptr, input_write = nullptr, output_read = nullptr, output_write = nullptr;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    if (!CreatePipe(&input_read, &input_write, &security, 0) ||
        !CreatePipe(&output_read, &output_write, &security, 0)) return fail("Cannot create console pipes");

    HPCON console = nullptr;
    if (FAILED(CreatePseudoConsole(COORD{80, 24}, input_read, output_write, 0, &console)))
        return fail("Cannot create nested pseudoconsole");
    CloseHandle(input_read); input_read = nullptr;
    CloseHandle(output_write); output_write = nullptr;

    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    std::vector<unsigned char> attribute_storage(attribute_bytes);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_bytes) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   console, sizeof(console), nullptr, nullptr)) {
        ClosePseudoConsole(console); CloseHandle(input_write); CloseHandle(output_read);
        return fail("Cannot bind nested pseudoconsole");
    }

    std::wstring command = quote(argv[1]);
    for (int i = 2; i < argc; ++i) { command.push_back(L' '); command += quote(argv[i]); }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(argv[1], command.data(), nullptr, nullptr, FALSE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                        nullptr, nullptr, &startup.StartupInfo, &process)) {
        DeleteProcThreadAttributeList(attributes); ClosePseudoConsole(console);
        CloseHandle(input_write); CloseHandle(output_read);
        return fail("Cannot launch terminal client in nested pseudoconsole");
    }
    DeleteProcThreadAttributeList(attributes);
    CloseHandle(process.hThread);

    std::atomic_bool resized{false};
    std::atomic<unsigned long long> input_bytes{0}, output_bytes{0};
    std::thread resize([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT)
            resized.store(SUCCEEDED(ResizePseudoConsole(console, COORD{110, 42})), std::memory_order_release);
    });
    std::thread output([&] {
        char bytes[8192]; DWORD count = 0;
        while (ReadFile(output_read, bytes, sizeof(bytes), &count, nullptr) && count) {
            output_bytes.fetch_add(count, std::memory_order_relaxed);
            DWORD written = 0; std::size_t offset = 0;
            while (offset < count && WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes + offset,
                                                count - static_cast<DWORD>(offset), &written, nullptr) && written)
                offset += written;
            if (offset < count) break;
        }
    });
    std::thread input([&] {
        char bytes[4096]; DWORD count = 0;
        while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), bytes, sizeof(bytes), &count, nullptr) && count) {
            input_bytes.fetch_add(count, std::memory_order_relaxed);
            DWORD written = 0; std::size_t offset = 0;
            while (offset < count && WriteFile(input_write, bytes + offset,
                                                count - static_cast<DWORD>(offset), &written, nullptr) && written)
                offset += written;
            if (offset < count) break;
        }
    });

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    ClosePseudoConsole(console);
    console = nullptr;
    CloseHandle(input_write); input_write = nullptr;
    if (input.joinable()) input.join();
    if (resize.joinable()) resize.join();
    if (output.joinable()) output.join();
    CloseHandle(output_read);
    std::cerr << "nested_conpty input_bytes=" << input_bytes.load() << " output_bytes=" << output_bytes.load()
              << " resized=" << resized.load() << " child_exit=" << exit_code << '\n';
    if (!resized.load(std::memory_order_acquire)) {
        std::cerr << "Nested pseudoconsole did not complete the requested live resize\n";
        return 1;
    }
    return static_cast<int>(exit_code);
}
