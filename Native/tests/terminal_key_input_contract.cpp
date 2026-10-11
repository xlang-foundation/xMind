#include "agentflow/terminal_key_input.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using agentflow::terminal_console_dimensions;
    using agentflow::terminal_key_input;
    require(terminal_key_input('A', L'a', false, false, false) == L"a", "Printable input must retain the translated Unicode character");
    require(terminal_key_input('C', L'\x03', true, false, false) == L"\x03", "Ctrl+C must reach the pseudoterminal as an interrupt byte");
    require(terminal_key_input('Z', L'\x1a', true, false, false) == L"\x1a", "Control letters must map to their control bytes");
    require(terminal_key_input(0x0d, L'\r', false, false, false) == L"\r", "Enter must submit immediately");
    require(terminal_key_input(0x08, L'\b', false, false, false) == L"\x7f", "Backspace must use the conventional terminal erase byte");
    require(terminal_key_input(0x09, L'\t', false, true, false) == L"\x1b[Z", "Shift+Tab must use the reverse-tab sequence");
    require(terminal_key_input(0x25, 0, false, false, false) == L"\x1b[D", "Left arrow must use its VT sequence");
    require(terminal_key_input(0x2e, 0, false, false, false) == L"\x1b[3~", "Delete must use its VT sequence");
    require(terminal_key_input(0x70, 0, false, false, false) == L"\x1bOP", "F1 must use its VT sequence");
    require(terminal_key_input(0x7b, 0, false, false, false) == L"\x1b[24~", "F12 must use its VT sequence");
    require(terminal_key_input('X', L'x', false, false, true) == L"\x1bx", "Alt+character must be prefixed with Escape");
    require(terminal_key_input(0xe7, L"@", true, false, true) == L"@", "AltGr printable input must not be rewritten as a control or Alt shortcut");
    const std::wstring supplementary = L"\U0001F600";
    require(terminal_key_input(0xe7, std::wstring_view(supplementary), false, false, false) == supplementary, "A supplementary Unicode key must preserve its UTF-16 surrogate pair");
    require(terminal_key_input(0x10, 0, false, false, false).empty(), "Modifier-only events must not emit terminal input");
    require(terminal_console_dimensions(1, 1) == std::pair<unsigned short, unsigned short>{5, 20}, "Terminal dimensions must clamp to backend minimums");
    require(terminal_console_dimensions(500, 700) == std::pair<unsigned short, unsigned short>{300, 500}, "Terminal dimensions must clamp to backend maximums");
    return 0;
}
