#pragma once
// unicode::Agl -- glyph names the Adobe way (resources/ucd/aglfn.txt and
// glyphlist.txt, compiled in). GTK-free.
//
//   new glyphs     AGLFN's "nice" name when it has one (eacute, Zcaron),
//                  else uniXXXX (BMP) / uXXXXX -- nice_name()
//   reading names  the AGL specification's mapping: drop a ".suffix", split
//                  ligatures on "_", each part from the full AGL, or uniXXXX
//                  (one or more groups of 4), or uXXXX..uXXXXXX --
//                  codepoints_for_name("eacute.sc") == {U+00E9}
//
// Production names (uni00E9 for every encoded glyph, as some toolchains
// want) are an export concern, not decided here.
#include <string>
#include <string_view>
#include <vector>

namespace granjon::unicode {

// AGLFN's name for `cp`, or "" when it has none.
std::string_view aglfn_name(char32_t cp);
// The name a new glyph for `cp` takes: AGLFN, else uniXXXX / uXXXXX.
std::string nice_name(char32_t cp);
// The characters a glyph name stands for, per the AGL spec; empty when the
// name maps nothing (a.alt maps 'a'; f_i maps f, i; foo maps nothing).
std::vector<char32_t> codepoints_for_name(std::string_view glyph_name);

}  // namespace granjon::unicode
