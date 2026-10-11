#include "unicode/Blocks.hpp"

#include "unicode/Embedded.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <map>
#include <string_view>

namespace granjon::unicode {

namespace {
const Block kNone{0, 0, "No Block", 100000};
}  // namespace

const std::vector<Block>& blocks() {
    static const std::vector<Block> all = [] {
        std::vector<Block> v;
        const std::string_view data = embedded::get("ucd/blocks.txt");
        std::size_t a = 0;
        while (a < data.size()) {
            std::size_t b = data.find('\n', a);
            if (b == std::string_view::npos) b = data.size();
            const std::string_view line = data.substr(a, b - a);
            a = b + 1;
            if (line.empty() || line[0] == '#') continue;
            const auto dots = line.find(".."), semi = line.find(';');
            if (dots == std::string_view::npos || semi == std::string_view::npos) continue;
            const auto num = [](std::string_view h) {
                unsigned x = 0;
                std::from_chars(h.data(), h.data() + h.size(), x, 16);
                return static_cast<char32_t>(x);
            };
            v.push_back({num(line.substr(0, dots)), num(line.substr(dots + 2, semi - dots - 2)),
                         std::string(line.substr(semi + 1)), static_cast<int>(v.size())});
        }
        return v;
    }();
    return all;
}

const Block& block_of(char32_t cp) {
    const auto& v = blocks();
    const auto it = std::upper_bound(v.begin(), v.end(), cp, [](char32_t c, const Block& b) { return c < b.first; });
    if (it != v.begin() && cp <= std::prev(it)->last) return *std::prev(it);
    return kNone;
}

std::string section_of(const std::vector<char32_t>& cps) {
    return cps.empty() ? "Unencoded" : block_of(cps.front()).name;
}

int section_rank(const std::string& section) {
    static const std::map<std::string, int> rank = [] {
        std::map<std::string, int> m;
        for (const auto& b : blocks()) m[b.name] = b.rank;
        return m;
    }();
    if (section == "Unencoded") return 200000;
    if (section == "Parts") return -1;   // M14c: glyphs left out of the compile -- first (Scott, s05: "at the top of the navigator")
    const auto it = rank.find(section);
    return it == rank.end() ? kNone.rank : it->second;
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
