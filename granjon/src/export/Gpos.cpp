// otf::Gpos -- kerning as a GPOS 'kern' feature: one PairPos lookup (DFLT and
// latn, default language), the first feature Granjon compiles.
//
// UFO precedence (glyph/glyph > glyph/group > group/glyph > group/group) is
// carried by subtable order -- a pair stops at the first subtable that lists
// it:
//   1. PairPos format 1 -- every glyph pair an entry with a glyph side reaches,
//      at the value model::kern_lookup gives it (the same lookup the spacing
//      strip draws with), minus pairs whose value equals what the class
//      subtables would give anyway. An exception of 0 stays: it must stop a
//      class kern.
//   2. PairPos format 2 -- the group/group entries as a class matrix.
// Subtables are split to stay under 64 KB; the lookup becomes an Extension
// (type 9) only if its subtables pass 64 KB together.
#include "export/OtfParts.hpp"
#include "model/Spacing.hpp"

#include <algorithm>
#include <set>

namespace granjon::otf {

namespace {

constexpr std::size_t kSubtableBudget = 60000;

bool left_group(const std::string& s)  { return s.rfind("public.kern1.", 0) == 0; }
bool right_group(const std::string& s) { return s.rfind("public.kern2.", 0) == 0; }

Bytes coverage(std::vector<int> gids) {
    std::sort(gids.begin(), gids.end());
    gids.erase(std::unique(gids.begin(), gids.end()), gids.end());
    Writer w;
    w.u16(1);
    w.u16(static_cast<unsigned>(gids.size()));
    for (int g : gids) w.u16(static_cast<unsigned>(g));
    return w.b;
}

Bytes class_def(std::vector<std::pair<int, int>> gid_class) {   // class 0 entries are left out
    std::sort(gid_class.begin(), gid_class.end());
    struct R { int a, b, c; };
    std::vector<R> rs;
    for (const auto& [g, c] : gid_class) {
        if (c == 0) continue;
        if (!rs.empty() && rs.back().b + 1 == g && rs.back().c == c) rs.back().b = g;
        else rs.push_back({g, g, c});
    }
    Writer w;
    w.u16(2);
    w.u16(static_cast<unsigned>(rs.size()));
    for (const auto& r : rs) { w.u16(static_cast<unsigned>(r.a)); w.u16(static_cast<unsigned>(r.b)); w.u16(static_cast<unsigned>(r.c)); }
    return w.b;
}

using Row = std::pair<int, std::vector<std::pair<int, int>>>;   // first gid -> (second gid, value)

Bytes pair_pos1(const std::vector<Row>& rows) {
    Writer sets;
    std::vector<std::size_t> at;
    for (const auto& [_, seconds] : rows) {
        at.push_back(sets.size());
        sets.u16(static_cast<unsigned>(seconds.size()));
        for (const auto& [g, v] : seconds) { sets.u16(static_cast<unsigned>(g)); sets.i16(v); }
    }
    std::vector<int> firsts;
    for (const auto& r : rows) firsts.push_back(r.first);
    const Bytes cov = coverage(firsts);
    const std::size_t head = 10 + 2 * rows.size();
    Writer w;
    w.u16(1);
    w.u16(static_cast<unsigned>(head + sets.size()));   // coverage after the sets
    w.u16(0x0004);                                       // valueFormat1: XAdvance
    w.u16(0);
    w.u16(static_cast<unsigned>(rows.size()));
    for (std::size_t a : at) w.u16(static_cast<unsigned>(head + a));
    w.bytes(sets.b);
    w.bytes(cov);
    return w.b;
}

}  // namespace

Bytes build_gpos(const model::Kerning& k, const Prepared& p, OtfReport& rep) {
    auto gid_of = [&](const std::string& n) {
        auto it = p.gid.find(n);
        return it == p.gid.end() ? -1 : it->second;
    };
    auto members = [&](const std::string& side) {
        std::vector<std::string> out;
        if (left_group(side) || right_group(side)) {
            if (auto it = k.groups.find(side); it != k.groups.end())
                for (const auto& m : it->second)
                    if (gid_of(m) >= 0) out.push_back(m);
        } else if (gid_of(side) >= 0) {
            out.push_back(side);
        }
        return out;
    };
    // What the class subtables answer for a glyph pair.
    auto class_value = [&](const std::string& l, const std::string& r) {
        const std::string g1 = model::kern_group(k, l, 1), g2 = model::kern_group(k, r, 2);
        if (g1.empty() || g2.empty()) return 0;
        auto it = k.pairs.find({g1, g2});
        return it == k.pairs.end() ? 0 : it->second;
    };

    // ── format 1: pairs with a glyph side ──
    std::set<std::pair<std::string, std::string>> cand;
    std::set<std::string> left_classes, right_classes;
    for (const auto& [key, v] : k.pairs) {
        const bool lg = left_group(key.first), rg = right_group(key.second);
        if (lg && rg) {
            if (!members(key.first).empty() && !members(key.second).empty()) {
                left_classes.insert(key.first);
                right_classes.insert(key.second);
            }
            continue;
        }
        for (const auto& l : members(key.first))
            for (const auto& r : members(key.second)) cand.insert({l, r});
    }
    std::map<int, std::vector<std::pair<int, int>>> f1;
    for (const auto& [l, r] : cand) {
        const int v = model::kern_lookup(k, l, r).value;
        if (v == class_value(l, r)) continue;
        f1[gid_of(l)].push_back({gid_of(r), v});
    }
    std::vector<Bytes> subtables;
    {
        std::vector<Row> chunk;
        std::size_t est = 10;
        for (auto& [g, seconds] : f1) {
            std::sort(seconds.begin(), seconds.end());
            rep.kern_glyph_pairs += static_cast<int>(seconds.size());
            const std::size_t row = 8 + 4 * seconds.size();
            if (!chunk.empty() && est + row > kSubtableBudget) {
                subtables.push_back(pair_pos1(chunk));
                chunk.clear();
                est = 10;
            }
            chunk.push_back({g, seconds});
            est += row;
        }
        if (!chunk.empty()) subtables.push_back(pair_pos1(chunk));
    }

    // ── format 2: group / group ──
    if (!left_classes.empty()) {
        std::vector<std::string> L(left_classes.begin(), left_classes.end());
        std::vector<std::string> R(right_classes.begin(), right_classes.end());
        std::vector<std::pair<int, int>> cd2;
        for (std::size_t c = 0; c < R.size(); ++c)
            for (const auto& m : members(R[c]))
                if (model::kern_group(k, m, 2) == R[c]) cd2.push_back({gid_of(m), static_cast<int>(c + 1)});
        const Bytes class2 = class_def(cd2);
        const std::size_t c2n = R.size() + 1;
        for (std::size_t i = 0; i < L.size();) {
            // Rows that fit the budget (at least one).
            std::vector<std::string> rows;
            std::size_t est = 16 + c2n * 2 + class2.size() + 8;
            for (; i < L.size(); ++i) {
                const std::size_t n = members(L[i]).size();
                const std::size_t add = c2n * 2 + n * 8;
                if (!rows.empty() && est + add > kSubtableBudget) break;
                rows.push_back(L[i]);
                est += add;
            }
            std::vector<int> cov;
            std::vector<std::pair<int, int>> cd1;
            for (std::size_t c = 0; c < rows.size(); ++c)
                for (const auto& m : members(rows[c]))
                    if (model::kern_group(k, m, 1) == rows[c]) {
                        cov.push_back(gid_of(m));
                        cd1.push_back({gid_of(m), static_cast<int>(c + 1)});
                    }
            const Bytes cv = coverage(cov), class1 = class_def(cd1);
            const std::size_t c1n = rows.size() + 1;
            const std::size_t head = 16 + c1n * c2n * 2;
            Writer w;
            w.u16(2);
            w.u16(static_cast<unsigned>(head));
            w.u16(0x0004);
            w.u16(0);
            w.u16(static_cast<unsigned>(head + cv.size()));
            w.u16(static_cast<unsigned>(head + cv.size() + class1.size()));
            w.u16(static_cast<unsigned>(c1n));
            w.u16(static_cast<unsigned>(c2n));
            for (std::size_t a = 0; a < c1n; ++a)
                for (std::size_t b = 0; b < c2n; ++b) {
                    int v = 0;
                    if (a > 0 && b > 0)
                        if (auto it = k.pairs.find({rows[a - 1], R[b - 1]}); it != k.pairs.end()) v = it->second;
                    if (v) ++rep.kern_class_pairs;
                    w.i16(v);
                }
            w.bytes(cv);
            w.bytes(class1);
            w.bytes(class2);
            subtables.push_back(w.b);
        }
    }
    if (subtables.empty()) return {};
    rep.gpos_subtables = static_cast<int>(subtables.size());

    // ── the lookup ──
    Writer lookup;
    std::size_t total = 0;
    for (const auto& s : subtables) total += s.size();
    const std::size_t lhead = 6 + 2 * subtables.size();
    const bool ext = lhead + total - subtables.back().size() > 0xFFFF;
    lookup.u16(ext ? 9 : 2);
    lookup.u16(0);
    lookup.u16(static_cast<unsigned>(subtables.size()));
    if (!ext) {
        std::size_t at = lhead;
        for (const auto& s : subtables) { lookup.u16(static_cast<unsigned>(at)); at += s.size(); }
        for (const auto& s : subtables) lookup.bytes(s);
    } else {
        const std::size_t ext_at = lhead;
        for (std::size_t i = 0; i < subtables.size(); ++i) lookup.u16(static_cast<unsigned>(ext_at + 8 * i));
        std::size_t data = ext_at + 8 * subtables.size();
        for (std::size_t i = 0; i < subtables.size(); ++i) {
            const std::size_t here = ext_at + 8 * i;
            lookup.u16(1);
            lookup.u16(2);
            lookup.u32(static_cast<std::uint32_t>(data - here));
            data += subtables[i].size();
        }
        for (const auto& s : subtables) lookup.bytes(s);
    }

    Writer scripts;   // DFLT and latn, each a default LangSys with feature 0
    scripts.u16(2);
    scripts.tag("DFLT"); scripts.u16(14);
    scripts.tag("latn"); scripts.u16(26);
    for (int i = 0; i < 2; ++i) {
        scripts.u16(4); scripts.u16(0);
        scripts.u16(0); scripts.u16(0xFFFF); scripts.u16(1); scripts.u16(0);
    }
    Writer features;
    features.u16(1);
    features.tag("kern"); features.u16(8);
    features.u16(0); features.u16(1); features.u16(0);

    Writer w;
    const std::size_t sl = 10, fl = sl + scripts.size(), ll = fl + features.size();
    w.u16(1); w.u16(0);
    w.u16(static_cast<unsigned>(sl));
    w.u16(static_cast<unsigned>(fl));
    w.u16(static_cast<unsigned>(ll));
    w.bytes(scripts.b);
    w.bytes(features.b);
    w.u16(1);   // LookupList: one lookup, right after
    w.u16(4);
    w.bytes(lookup.b);
    return w.b;
}

}  // namespace granjon::otf
