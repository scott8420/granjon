#pragma once
// unicode::Blocks -- which Unicode block a codepoint lives in, for grouping the
// navigator and the overview. A small hand table for the v1 scripts (Latin,
// Greek, Cyrillic and their neighbours); M3 replaces it with the bundled UCD
// table and keeps this signature. GTK-free.
#include <string>
#include <vector>

namespace granjon::unicode {

struct Block {
    char32_t    first;
    char32_t    last;
    const char* name;
    int         rank;      // display order of the section
};

// The block holding `cp`, or {0,0,"Other",...} when the table has none.
const Block& block_of(char32_t cp);

// The section a glyph files under: its first codepoint's block, or
// "Unencoded" when it maps no codepoint.
std::string section_of(const std::vector<char32_t>& cps);
int         section_rank(const std::string& section);

// "U+0041"
std::string format_cp(char32_t cp);
// "U+0041 U+00C1" / "0041,00c1" / "A" -> codepoints. False on anything unreadable.
bool parse_cps(const std::string& text, std::vector<char32_t>& out);

}  // namespace granjon::unicode
