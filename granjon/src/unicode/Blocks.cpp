#include "unicode/Blocks.hpp"

#include <cctype>
#include <cstdio>

namespace granjon::unicode {

namespace {
const Block kBlocks[] = {
    {0x0000, 0x007F, "Basic Latin",               10},
    {0x0080, 0x00FF, "Latin-1 Supplement",        11},
    {0x0100, 0x017F, "Latin Extended-A",          12},
    {0x0180, 0x024F, "Latin Extended-B",          13},
    {0x0250, 0x02AF, "IPA Extensions",            14},
    {0x02B0, 0x02FF, "Spacing Modifiers",         15},
    {0x0300, 0x036F, "Combining Diacritics",      16},
    {0x0370, 0x03FF, "Greek and Coptic",          20},
    {0x0400, 0x04FF, "Cyrillic",                  30},
    {0x0500, 0x052F, "Cyrillic Supplement",       31},
    {0x1E00, 0x1EFF, "Latin Extended Additional", 17},
    {0x1F00, 0x1FFF, "Greek Extended",            21},
    {0x2000, 0x206F, "General Punctuation",       40},
    {0x20A0, 0x20CF, "Currency Symbols",          41},
    {0x2100, 0x214F, "Letterlike Symbols",        42},
    {0x2190, 0x21FF, "Arrows",                    43},
    {0x2200, 0x22FF, "Mathematical Operators",    44},
    {0xFB00, 0xFB4F, "Alphabetic Presentation",   45},
};
const Block kOther{0, 0, "Other", 90};
}  // namespace

const Block& block_of(char32_t cp) {
    for (const auto& b : kBlocks)
        if (cp >= b.first && cp <= b.last) return b;
    return kOther;
}

std::string section_of(const std::vector<char32_t>& cps) {
    return cps.empty() ? "Unencoded" : block_of(cps.front()).name;
}

int section_rank(const std::string& section) {
    if (section == "Unencoded") return 99;
    for (const auto& b : kBlocks) if (section == b.name) return b.rank;
    return kOther.rank;
}

std::string format_cp(char32_t cp) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "U+%04X", static_cast<unsigned>(cp));
    return buf;
}

bool parse_cps(const std::string& text, std::vector<char32_t>& out) {
    out.clear();
    std::size_t i = 0;
    const auto sep = [](char c) { return c == ' ' || c == ',' || c == ';' || c == '\t'; };
    while (i < text.size()) {
        while (i < text.size() && sep(text[i])) ++i;
        if (i >= text.size()) break;
        std::size_t j = i;
        while (j < text.size() && !sep(text[j])) ++j;
        std::string tok = text.substr(i, j - i);
        i = j;
        if (tok.size() > 2 && (tok[0] == 'U' || tok[0] == 'u') && tok[1] == '+') tok = tok.substr(2);
        else if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) tok = tok.substr(2);
        bool hex = !tok.empty() && tok.size() <= 6;
        for (char c : tok) hex = hex && std::isxdigit(static_cast<unsigned char>(c));
        if (hex && tok.size() >= 2) {
            const unsigned long v = std::stoul(tok, nullptr, 16);
            if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return false;
            out.push_back(static_cast<char32_t>(v));
            continue;
        }
        // A single literal character (UTF-8): "A", "é".
        const auto* s = reinterpret_cast<const unsigned char*>(tok.data());
        char32_t cp = 0;
        std::size_t n = 0;
        if (s[0] < 0x80)              { cp = s[0]; n = 1; }
        else if ((s[0] >> 5) == 0x6)  { cp = s[0] & 0x1F; n = 2; }
        else if ((s[0] >> 4) == 0xE)  { cp = s[0] & 0x0F; n = 3; }
        else if ((s[0] >> 3) == 0x1E) { cp = s[0] & 0x07; n = 4; }
        if (n == 0 || n != tok.size()) return false;
        for (std::size_t k = 1; k < n; ++k) {
            if ((s[k] >> 6) != 0x2) return false;
            cp = (cp << 6) | (s[k] & 0x3F);
        }
        out.push_back(cp);
    }
    return true;
}

}  // namespace granjon::unicode
