#pragma once
// unicode::TypeIt (s08) -- how a user types a character in an ordinary app on
// the desktop (a text editor, a word processor). Shown in the inspector's
// GLYPH card; it never changes the font. GTK-free.
//
//   key       printable ASCII on a US layout: "n", "Shift+N", "Shift+1" (!)
//   compose   a Compose-key sequence from the system's X11 Compose table
//             (<Multi_key> sequences only -- dead keys depend on the layout),
//             the shortest one: "Compose ' e"
//   hex       GNOME / GTK / IBus, any code point: "Ctrl+Shift+U e9 Space"
//   windows   "Alt+0233 (numeric keypad)" below 256; "00E9 then Alt+X (Word)"
//   mac       "Option+00E9 (Unicode Hex Input)" in the BMP
#include <map>
#include <string>
#include <vector>

namespace granjon::unicode {

struct TypeIt {
    std::string key, compose, hex, windows, mac;
};

// The Compose table: code point -> the shortest <Multi_key> sequence, keys
// spelled for reading ("'", "e", "<", "U+2013"). Sequences with a key that has
// no readable name are skipped.
std::map<char32_t, std::string> parse_compose(const std::string& text);
// The system table (en_US.UTF-8 Compose, read once; empty if absent).
const std::map<char32_t, std::string>& system_compose();

TypeIt type_it(char32_t cp, const std::map<char32_t, std::string>& compose);
inline TypeIt type_it(char32_t cp) { return type_it(cp, system_compose()); }

}  // namespace granjon::unicode
