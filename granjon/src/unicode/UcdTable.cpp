#include "unicode/UcdTable.hpp"

#include "unicode/Embedded.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>

namespace granjon::unicode {

namespace {

struct Entry {
    char32_t         cp;
    std::string_view name, gc, decomp;
    char32_t         upper = 0, lower = 0;
};
struct Range {
    char32_t         first, last;
    std::string_view prefix, gc;
};
struct Table {
    std::vector<Entry> entries;   // sorted by cp
    std::vector<Range> ranges;
};

char32_t hex(std::string_view s) {
    unsigned v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v, 16);
    return static_cast<char32_t>(v);
}

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    std::size_t a = 0;
    for (;;) {
        const auto b = s.find(sep, a);
        out.push_back(s.substr(a, b == std::string_view::npos ? std::string_view::npos : b - a));
        if (b == std::string_view::npos) return out;
        a = b + 1;
    }
}

const Table& table() {
    static const Table t = [] {
        Table t;
        const std::string_view data = embedded::get("ucd/ucd.txt");
        t.entries.reserve(41000);
        for (std::string_view line : split(data, '\n')) {
            if (line.empty() || line[0] == '#') continue;
            const auto f = split(line, ';');
            if (f.size() == 3) {                       // FIRST..LAST;PREFIX;GC
                const auto dots = f[0].find("..");
                t.ranges.push_back({hex(f[0].substr(0, dots)), hex(f[0].substr(dots + 2)), f[1], f[2]});
            } else if (f.size() >= 6) {
                Entry e{hex(f[0]), f[1], f[2], f[3]};
                if (!f[4].empty()) e.upper = hex(f[4]);
                if (!f[5].empty()) e.lower = hex(f[5]);
                t.entries.push_back(e);
            }
        }
        // ucd.txt is in code point order; a cheap guard keeps lookups honest.
        std::sort(t.entries.begin(), t.entries.end(), [](const Entry& a, const Entry& b) { return a.cp < b.cp; });
        return t;
    }();
    return t;
}

const Entry* entry(char32_t cp) {
    const auto& e = table().entries;
    const auto it = std::lower_bound(e.begin(), e.end(), cp, [](const Entry& x, char32_t c) { return x.cp < c; });
    return (it != e.end() && it->cp == cp) ? &*it : nullptr;
}

const Range* range(char32_t cp) {
    for (const auto& r : table().ranges)
        if (cp >= r.first && cp <= r.last) return &r;
    return nullptr;
}

// Hangul syllable names (Unicode ch. 3.12): the jamo short names.
std::string hangul_name(char32_t cp) {
    static const char* L[] = {"G", "GG", "N", "D", "DD", "R", "M", "B", "BB", "S", "SS", "", "J", "JJ", "C", "K", "T", "P", "H"};
    static const char* V[] = {"A", "AE", "YA", "YAE", "EO", "E", "YEO", "YE", "O", "WA", "WAE", "OE", "YO",
                              "U", "WEO", "WE", "WI", "YU", "EU", "YI", "I"};
    static const char* T[] = {"", "G", "GG", "GS", "N", "NJ", "NH", "D", "L", "LG", "LM", "LB", "LS", "LT",
                              "LP", "LH", "M", "B", "BS", "S", "SS", "NG", "J", "C", "K", "T", "P", "H"};
    const unsigned s = cp - 0xAC00;
    return std::string("HANGUL SYLLABLE ") + L[s / (21 * 28)] + V[(s % (21 * 28)) / 28] + T[s % 28];
}

char upper_ascii(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }

}  // namespace

std::string_view ucd_version() { return "17.0.0"; }

bool is_assigned(char32_t cp) { return entry(cp) || range(cp); }

std::string name_of(char32_t cp) {
    if (const Entry* e = entry(cp)) return std::string(e->name);
    if (const Range* r = range(cp)) {
        if (r->prefix.empty()) return {};
        if (r->prefix == "HANGUL SYLLABLE ") return hangul_name(cp);
        char buf[16];
        std::snprintf(buf, sizeof buf, "%04X", static_cast<unsigned>(cp));
        return std::string(r->prefix) + buf;
    }
    return {};
}

std::string_view category(char32_t cp) {
    if (const Entry* e = entry(cp)) return e->gc;
    if (const Range* r = range(cp)) return r->gc;
    return "Cn";
}

std::vector<char32_t> decomposition(char32_t cp, bool* compat) {
    std::vector<char32_t> out;
    if (compat) *compat = false;
    const Entry* e = entry(cp);
    if (!e || e->decomp.empty()) return out;
    for (std::string_view tok : split(e->decomp, ' ')) {
        if (tok.empty()) continue;
        if (tok.front() == '<') { if (compat) *compat = true; continue; }
        out.push_back(hex(tok));
    }
    return out;
}

char32_t upper_of(char32_t cp) { const Entry* e = entry(cp); return e ? e->upper : 0; }
char32_t lower_of(char32_t cp) { const Entry* e = entry(cp); return e ? e->lower : 0; }

char32_t other_case(char32_t cp) {
    if (char32_t l = lower_of(cp)) return l;
    if (char32_t u = upper_of(cp)) return u;
    return 0;
}

std::vector<char32_t> find_by_name(std::string_view query, std::size_t limit) {
    // Query words, upper-cased (UCD names are A-Z 0-9 space hyphen).
    std::vector<std::string> words;
    std::string cur;
    for (char c : query) {
        if (c == ' ' || c == '\t' || c == ',') { if (!cur.empty()) words.push_back(cur), cur.clear(); }
        else cur += upper_ascii(c);
    }
    if (!cur.empty()) words.push_back(cur);
    if (words.empty()) return {};
    std::string full;
    for (const auto& w : words) full += (full.empty() ? "" : " ") + w;

    struct Hit { int score; char32_t cp; };
    std::vector<Hit> hits;
    std::vector<std::string_view> nw;
    for (const Entry& e : table().entries) {
        if (e.name.empty() || e.name.front() == '<') continue;
        nw.clear();
        std::size_t a = 0;
        for (std::size_t i = 0; i <= e.name.size(); ++i)
            if (i == e.name.size() || e.name[i] == ' ' || e.name[i] == '-') {
                if (i > a) nw.push_back(e.name.substr(a, i - a));
                a = i + 1;
            }
        bool all = true;
        int exact_words = 0;
        for (const auto& w : words) {
            bool found = false;
            for (auto n : nw)
                if (n.starts_with(w)) { found = true; exact_words += (n.size() == w.size()); break; }
            if (!found) { all = false; break; }
        }
        if (!all) continue;
        // Exact name first; then whole-word matches; then the shortest name.
        int score = (e.name == full) ? 0 : 1000 + static_cast<int>(nw.size()) * 10
                                            - exact_words * 5;
        hits.push_back({score, e.cp});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score < b.score; });
    std::vector<char32_t> out;
    for (const auto& h : hits) {
        if (out.size() == limit) break;
        out.push_back(h.cp);
    }
    return out;
}

std::string utf8(char32_t c) {
    std::string s;
    if (c < 0x80) s += static_cast<char>(c);
    else if (c < 0x800) { s += static_cast<char>(0xC0 | (c >> 6)); s += static_cast<char>(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) {
        s += static_cast<char>(0xE0 | (c >> 12));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (c >> 18));
        s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    }
    return s;
}

}  // namespace granjon::unicode
