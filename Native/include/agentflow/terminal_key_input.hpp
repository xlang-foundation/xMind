#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace agentflow {

// Convert a Windows console key-down event into the corresponding VT input
// bytes expected by a pseudoterminal. The function is platform independent so
// the mapping stays covered by the native contract on every build host.
inline std::wstring terminal_key_input(unsigned short key, std::wstring_view characters,
                                       bool control, bool shift, bool alt) {
    constexpr unsigned short vk_back = 0x08, vk_tab = 0x09, vk_return = 0x0d;
    constexpr unsigned short vk_escape = 0x1b, vk_end = 0x23, vk_home = 0x24;
    constexpr unsigned short vk_left = 0x25, vk_up = 0x26, vk_right = 0x27, vk_down = 0x28;
    constexpr unsigned short vk_insert = 0x2d, vk_delete = 0x2e;
    constexpr unsigned short vk_page_up = 0x21, vk_page_down = 0x22;
    constexpr unsigned short vk_f1 = 0x70, vk_f12 = 0x7b;

    const auto printable = (characters.size() == 1 && characters[0] >= L' ' && characters[0] != 0x7f) ||
        (characters.size() == 2 && characters[0] >= 0xd800 && characters[0] <= 0xdbff &&
         characters[1] >= 0xdc00 && characters[1] <= 0xdfff);
    const auto altgr = control && alt && printable;
    std::wstring value;
    if (control && !alt && key >= 'A' && key <= 'Z') {
        value.push_back(static_cast<wchar_t>(key - 'A' + 1));
    } else {
        switch (key) {
        case vk_return: value = L"\r"; break;
        case vk_back: value = L"\x7f"; break;
        case vk_tab: value = shift ? L"\x1b[Z" : L"\t"; break;
        case vk_escape: value = L"\x1b"; break;
        case vk_up: value = L"\x1b[A"; break;
        case vk_down: value = L"\x1b[B"; break;
        case vk_right: value = L"\x1b[C"; break;
        case vk_left: value = L"\x1b[D"; break;
        case vk_home: value = L"\x1b[H"; break;
        case vk_end: value = L"\x1b[F"; break;
        case vk_insert: value = L"\x1b[2~"; break;
        case vk_delete: value = L"\x1b[3~"; break;
        case vk_page_up: value = L"\x1b[5~"; break;
        case vk_page_down: value = L"\x1b[6~"; break;
        default:
            if (key >= vk_f1 && key <= vk_f12) {
                static constexpr const wchar_t* function_keys[] = {
                    L"\x1bOP", L"\x1bOQ", L"\x1bOR", L"\x1bOS", L"\x1b[15~", L"\x1b[17~",
                    L"\x1b[18~", L"\x1b[19~", L"\x1b[20~", L"\x1b[21~", L"\x1b[23~", L"\x1b[24~"};
                value = function_keys[key - vk_f1];
            } else if (printable) {
                value.assign(characters);
            }
            break;
        }
        if (control && !alt && key == 'C') value = L"\x03";
    }
    if (alt && !altgr && !value.empty()) value.insert(value.begin(), L'\x1b');
    return value;
}

inline std::wstring terminal_key_input(unsigned short key, wchar_t character,
                                       bool control, bool shift, bool alt) {
    return terminal_key_input(key, std::wstring_view(&character, 1), control, shift, alt);
}

inline std::pair<unsigned short, unsigned short> terminal_console_dimensions(
    unsigned short rows, unsigned short columns) noexcept {
    if (rows < 5) rows = 5;
    if (rows > 300) rows = 300;
    if (columns < 20) columns = 20;
    if (columns > 500) columns = 500;
    return {rows, columns};
}

} // namespace agentflow
