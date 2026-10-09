#pragma once
// model::Find -- one query, any kind of glyph reference, ranked hits. The
// header search, the navigator / overview filter and (later) granjon-cli all
// ask this. GTK-free.
//
// A query may be:
//   eacute, uni0416, a.sc     a glyph name: exact, then prefix, then substring
//   /eacute                   a glyph-name escape: that name exactly
//   é                         the character: the glyph(s) mapping it
//   U+00E9  0xE9  00E9        a codepoint (bare hex needs 4+ digits, so "ff"
//                             and "a" stay names)
// Names compare case-insensitively, but an exact-case match ranks first
// ("a" before "A").
//
// When the query names a codepoint no glyph maps, `missing` holds it and
// `suggested` a glyph name to create it under (the character itself for
// A-Z a-z, else uniXXXX / uXXXXX; M3's AGL table supplies "eacute").
#include "model/Font.hpp"

#include <optional>
#include <string>
#include <vector>

namespace granjon::model {

struct FindHit {
    std::string name;
    int         rank = 0;     // lower is better
    const char* why  = "";    // "name", "codepoint", "prefix", "contains"
};

struct FindResult {
    std::vector<FindHit>    hits;        // best first, each glyph once
    std::optional<char32_t> missing;     // a codepoint asked for that nothing maps
    std::string             suggested;   // a free name to create `missing` under
    bool empty_query = true;
};

FindResult find_glyphs(const Font& f, const std::string& query);

// The name a new glyph for `cp` would get: the letter itself for A-Z a-z,
// else uniXXXX (BMP) / uXXXXX; made unique against the font with ".1", ".2".
std::string name_for_codepoint(const Font& f, char32_t cp);

}  // namespace granjon::model
