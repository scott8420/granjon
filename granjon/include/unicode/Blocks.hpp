#pragma once
// unicode::Blocks -- which Unicode block a codepoint lives in, for grouping the
// navigator and the overview. Every block of the bundled UCD (resources/ucd/
// blocks.txt, compiled in -- M3; the M1b hand table is gone). Sections sort
// in code point order, "Unencoded" last. GTK-free.
#include <string>
#include <vector>

namespace granjon::unicode {

struct Block {
    char32_t    first;
    char32_t    last;
    std::string name;
    int         rank;      // display order of the section (code point order)
};

// Every block, in code point order.
const std::vector<Block>& blocks();
// The block holding `cp`, or {0,0,"No Block",...} when none does.
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
