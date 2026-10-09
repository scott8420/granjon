#pragma once
// unicode::CharSet -- a named list of characters a font aims to cover
// (resources/charsets/*.txt, compiled in): Latin-1, GF Latin Core, Adobe
// Latin 2 / 3, WGL4. The overview shows a chosen set's gaps; the checklist
// counts what is missing. GTK-free.
//
// File shape: "# name: ..." and "# source: ..." header lines, then one
// character per line as hex, optionally followed by a glyph name.
#include <string>
#include <vector>

namespace granjon::unicode {

struct CharSet {
    std::string           id;       // "adobe-latin-2"
    std::string           name;     // "Adobe Latin 2"
    std::string           source;
    std::vector<char32_t> cps;      // code point order, unique
};

// The bundled sets, smallest first.
const std::vector<CharSet>& charsets();
// The set with `id`, or nullptr.
const CharSet* charset(const std::string& id);

}  // namespace granjon::unicode
