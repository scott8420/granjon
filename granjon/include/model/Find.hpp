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
//   eacute (no such glyph)    an AGL name: the glyph mapping its character
//   cedilla, cyrillic zhe     a Unicode character name (3+ letters): every
//                             glyph whose character's name holds those words
//
// Characters the query names that no glyph maps come back in `create`, each
// with a free glyph name: the AGL name typed, else AGLFN's (eacute), else
// uniXXXX / uXXXXX.
#include "model/Font.hpp"

#include <optional>
#include <string>
#include <vector>

namespace granjon::model {

struct FindHit {
    std::string name;
    int         rank = 0;     // lower is better
    const char* why  = "";    // "name", "codepoint", "prefix", "contains", "unicode name"
};

// A character the query asked for that no glyph maps, and a free name for it.
struct FindCreate {
    char32_t    cp;
    std::string name;
};

struct FindResult {
    std::vector<FindHit>    hits;        // best first, each glyph once
    std::vector<FindCreate> create;      // best first, at most kMaxCreate
    bool empty_query = true;
    static constexpr std::size_t kMaxCreate = 4;
};

FindResult find_glyphs(const Font& f, const std::string& query);

// The name a new glyph for `cp` would get: AGLFN's (A, eacute, Zcaron), else
// uniXXXX (BMP) / uXXXXX; made unique against the font with ".1", ".2".
std::string name_for_codepoint(const Font& f, char32_t cp);

// Case navigation (A <-> a): the glyph for the other case of `name`'s
// character -- by its code point, or for an unencoded name by the AGL
// reading of it (a.sc -> A.sc when that glyph exists, else A). "" when there
// is none; `other` (when given) is the other case's code point, 0 when the
// character has no case.
std::string other_case_glyph(const Font& f, const std::string& name, char32_t* other = nullptr);

}  // namespace granjon::model
