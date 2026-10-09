#include "model/Find.hpp"

#include "unicode/Agl.hpp"
#include "unicode/Blocks.hpp"
#include "unicode/UcdTable.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>

namespace granjon::model {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\n");
    return s.substr(a, b - a + 1);
}

bool all_hex(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c); });
}

// A single codepoint the query clearly means, if any.
std::optional<char32_t> as_codepoint(const std::string& q) {
    std::string h;
    if (q.size() > 2 && (q[0] == 'U' || q[0] == 'u') && q[1] == '+') h = q.substr(2);
    else if (q.size() > 2 && q[0] == '0' && (q[1] == 'x' || q[1] == 'X')) h = q.substr(2);
    else if (q.size() >= 4 && q.size() <= 6 && all_hex(q)) h = q;
    if (!h.empty()) {
        if (!all_hex(h) || h.size() > 6) return std::nullopt;
        const unsigned long v = std::stoul(h, nullptr, 16);
        if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return std::nullopt;
        return static_cast<char32_t>(v);
    }
    // One literal character (one UTF-8 sequence).
    std::vector<char32_t> cps;
    if (unicode::parse_cps(q, cps) && cps.size() == 1) {
        // parse_cps reads 2-6 hex digits as a codepoint; a 2-3 char token like
        // "ff" is a NAME here, so only accept a true one-character query.
        const auto* s = reinterpret_cast<const unsigned char*>(q.data());
        const std::size_t n = s[0] < 0x80 ? 1 : (s[0] >> 5) == 0x6 ? 2 : (s[0] >> 4) == 0xE ? 3 : 4;
        if (n == q.size()) return cps[0];
    }
    return std::nullopt;
}

}  // namespace

std::string name_for_codepoint(const Font& f, char32_t cp) {
    const std::string base = unicode::nice_name(cp);
    std::string name = base;
    for (int i = 1; f.glyph(name); ++i) name = base + "." + std::to_string(i);
    return name;
}

std::string other_case_glyph(const Font& f, const std::string& name, char32_t* other) {
    if (other) *other = 0;
    const Glyph* g = f.glyph(name);
    if (!g) return {};
    std::vector<char32_t> cps = g->unicodes;
    if (cps.empty()) cps = unicode::codepoints_for_name(name);
    if (cps.size() != 1) return {};
    const char32_t o = unicode::other_case(cps[0]);
    if (other) *other = o;
    if (!o) return {};
    const Glyph* target = f.glyph_for(o);
    if (!target) return {};
    // A suffixed, unencoded glyph (a.sc) goes to its suffixed partner (A.sc).
    if (g->unicodes.empty())
        if (const auto dot = name.find('.'); dot != std::string::npos)
            if (f.glyph(target->name + name.substr(dot))) return target->name + name.substr(dot);
    return target->name;
}

FindResult find_glyphs(const Font& f, const std::string& raw) {
    FindResult r;
    const std::string q = trim(raw);
    if (q.empty()) return r;
    r.empty_query = false;

    std::map<std::string, FindHit> best;   // name -> best hit
    std::vector<std::string> order = f.glyph_order();
    std::map<std::string, std::size_t> pos;
    for (std::size_t i = 0; i < order.size(); ++i) pos[order[i]] = i;
    const auto offer = [&](const std::string& name, int rank, const char* why) {
        auto it = best.find(name);
        if (it == best.end() || rank < it->second.rank) best[name] = {name, rank, why};
    };

    // "/name": exactly that glyph, nothing fuzzy.
    if (q[0] == '/' && q.size() > 1) {
        const std::string n = q.substr(1);
        if (f.glyph(n)) r.hits.push_back({n, 0, "name"});
        return r;
    }

    const auto cp = as_codepoint(q);
    const std::string lq = lower(q);
    std::map<char32_t, std::vector<std::string>> by_cp;    // who maps what
    for (const auto& name : order) {
        const Glyph& g = *f.glyph(name);
        for (char32_t c : g.unicodes) by_cp[c].push_back(name);
        if (cp && std::find(g.unicodes.begin(), g.unicodes.end(), *cp) != g.unicodes.end())
            offer(name, 1, "codepoint");
        const std::string ln = lower(name);
        if (name == q)                         offer(name, 0, "name");
        else if (ln == lq)                     offer(name, 2, "name");
        else if (ln.rfind(lq, 0) == 0)         offer(name, 3, "prefix");
        else if (ln.find(lq) != std::string::npos) offer(name, 4, "contains");
        // A codepoint typed as text ("00e9" / "u+00e9") also matches by its
        // formatted form, so partial hex finds glyphs as you type.
        for (char32_t c : g.unicodes)
            if (lower(unicode::format_cp(c)).find(lq) != std::string::npos) { offer(name, 5, "codepoint"); break; }
    }

    std::set<char32_t> offered;
    const auto creatable = [&](char32_t c, const std::string& name) {
        if (r.create.size() >= FindResult::kMaxCreate || by_cp.count(c) || !offered.insert(c).second) return;
        if (!unicode::is_assigned(c) && !(c >= 0xE000 && c <= 0xF8FF)) return;
        r.create.push_back({c, name});
    };

    // 1. A codepoint nothing maps.
    if (cp) creatable(*cp, name_for_codepoint(f, *cp));

    // 2. An AGL name for one character ("eacute", "uni0416", "u1F600"): the
    //    glyph mapping it, or create it under the name as typed.
    if (!cp && !f.glyph(q) && valid_glyph_name(q)) {
        const auto named = unicode::codepoints_for_name(q);
        if (named.size() == 1) {
            if (const auto it = by_cp.find(named[0]); it != by_cp.end())
                for (const auto& n : it->second) offer(n, 2, "codepoint");
            else
                creatable(named[0], q);
        }
    }

    // 3. A Unicode character name: "cedilla", "cyrillic zhe".
    const bool wordy = !cp && q[0] != '/' && q.size() >= 3 &&
                       std::any_of(q.begin(), q.end(), [](unsigned char c) { return std::isalpha(c); });
    if (wordy) {
        bool named_exactly = false;
        for (const auto& [n, h] : best) named_exactly = named_exactly || h.rank <= 2;
        for (char32_t c : unicode::find_by_name(q, 400)) {
            if (const auto it = by_cp.find(c); it != by_cp.end())
                for (const auto& n : it->second) offer(n, 6, "unicode name");
            else if (!named_exactly)
                creatable(c, name_for_codepoint(f, c));
        }
    }

    for (auto& [n, h] : best) r.hits.push_back(h);
    std::stable_sort(r.hits.begin(), r.hits.end(), [&](const FindHit& a, const FindHit& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return pos[a.name] < pos[b.name];
    });
    return r;
}

}  // namespace granjon::model
