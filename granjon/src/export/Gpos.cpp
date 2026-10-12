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
#include "export/Layout.hpp"
#include "model/Spacing.hpp"
#include "unicode/UcdTable.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace granjon::otf {

namespace {

constexpr std::size_t kSubtableBudget = 60000;

bool left_group(const std::string& s)  { return s.rfind("public.kern1.", 0) == 0; }
bool right_group(const std::string& s) { return s.rfind("public.kern2.", 0) == 0; }

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

std::vector<Bytes> kern_subtables(const model::Kerning& k, const Prepared& p, OtfReport& rep) {
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
    return subtables;
}

// ── mark positioning (M16b) ────────────────────────────────────────────────
// A mark = a glyph with an "_X" anchor whose code points (if any) are all
// combining marks. Class X = every "_X" anchor name some mark carries; a base
// (any other glyph) with an "X" anchor attaches it. One subtable per class, so
// a mark with two underscore anchors joins both.
struct MarkData {
    std::vector<bool> is_mark;
    std::vector<int>  marks;                                           // gids
    std::map<std::string, std::vector<std::pair<int, model::Anchor>>> mark_of;   // class -> (mark gid, its "_X")
    std::map<std::string, std::vector<std::pair<int, model::Anchor>>> base_of;   // class -> (base gid, its "X")
    std::map<std::string, std::vector<std::pair<int, model::Anchor>>> mark2_of;  // class -> (mark gid, its "X")
};

MarkData mark_data(const Prepared& p) {
    MarkData d;
    d.is_mark.assign(p.glyphs.size(), false);
    for (std::size_t g = 0; g < p.glyphs.size(); ++g) {
        const auto& o = p.glyphs[g];
        bool under = false;
        for (const auto& a : o.anchors) under = under || (a.name.size() > 1 && a.name[0] == '_');
        bool combining = true;
        for (char32_t cp : o.unicodes) combining = combining && unicode::category(cp).starts_with("M");
        // a mark: an "_X" anchor (and no code point, or combining ones) -- or (s10)
        // a combining code point with no anchors at all, so a lookup's
        // IgnoreMarks knows it (GDEF) even before it is anchored
        const bool coded_mark = !o.unicodes.empty() && combining;
        if ((under && (o.unicodes.empty() || combining)) || (coded_mark && o.anchors.empty())) {
            d.is_mark[g] = true;
            d.marks.push_back(static_cast<int>(g));
        }
    }
    for (std::size_t g = 0; g < p.glyphs.size(); ++g)
        for (const auto& a : p.glyphs[g].anchors)
            if (d.is_mark[g] && a.name.size() > 1 && a.name[0] == '_') d.mark_of[a.name.substr(1)].push_back({static_cast<int>(g), a});
    for (std::size_t g = 0; g < p.glyphs.size(); ++g)
        for (const auto& a : p.glyphs[g].anchors) {
            if (a.name.empty() || a.name[0] == '_' || !d.mark_of.count(a.name)) continue;
            (d.is_mark[g] ? d.mark2_of : d.base_of)[a.name].push_back({static_cast<int>(g), a});
        }
    return d;
}

Bytes anchor_table(const model::Anchor& a) {
    Writer w;
    w.u16(1);
    w.i16(static_cast<int>(std::lround(a.x)));
    w.i16(static_cast<int>(std::lround(a.y)));
    return w.b;
}

// MarkBasePos / MarkMarkPos format 1, ONE mark class. `targets` are the bases
// (or the marks marks attach to).
Bytes mark_attach(const std::vector<std::pair<int, model::Anchor>>& marks,
                  const std::vector<std::pair<int, model::Anchor>>& targets) {
    auto sorted = [](std::vector<std::pair<int, model::Anchor>> v) {
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        v.erase(std::unique(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), v.end());
        return v;
    };
    const auto M = sorted(marks), B = sorted(targets);
    std::vector<int> mg, bg;
    for (const auto& m : M) mg.push_back(m.first);
    for (const auto& b : B) bg.push_back(b.first);
    const Bytes mcov = coverage(mg), bcov = coverage(bg);
    Writer marr;   // MarkArray: count, (class, anchor offset) records, anchors
    marr.u16(static_cast<unsigned>(M.size()));
    std::size_t at = 2 + 4 * M.size();
    for (std::size_t i = 0; i < M.size(); ++i) { marr.u16(0); marr.u16(static_cast<unsigned>(at)); at += 6; }
    for (const auto& m : M) marr.bytes(anchor_table(m.second));
    Writer barr;   // BaseArray: count, one anchor offset per base (one class), anchors
    barr.u16(static_cast<unsigned>(B.size()));
    at = 2 + 2 * B.size();
    for (std::size_t i = 0; i < B.size(); ++i) { barr.u16(static_cast<unsigned>(at)); at += 6; }
    for (const auto& b : B) barr.bytes(anchor_table(b.second));
    const std::size_t head = 12;
    Writer w;
    w.u16(1);
    w.u16(static_cast<unsigned>(head));                                   // mark coverage
    w.u16(static_cast<unsigned>(head + mcov.size()));                     // base coverage
    w.u16(1);                                                             // one mark class
    w.u16(static_cast<unsigned>(head + mcov.size() + bcov.size()));       // MarkArray
    w.u16(static_cast<unsigned>(head + mcov.size() + bcov.size() + marr.size()));   // BaseArray
    w.bytes(mcov);
    w.bytes(bcov);
    w.bytes(marr.b);
    w.bytes(barr.b);
    return w.b;
}

std::vector<Bytes> mark_subtables(const MarkData& d, bool mkmk, OtfReport& rep) {
    std::vector<Bytes> out;
    for (const auto& [cls, marks] : d.mark_of) {
        const auto& tmap = mkmk ? d.mark2_of : d.base_of;
        auto it = tmap.find(cls);
        if (it == tmap.end() || it->second.empty()) continue;
        out.push_back(mark_attach(marks, it->second));
        if (!mkmk) {
            ++rep.mark_classes;
            rep.mark_bases += static_cast<int>(it->second.size());
        } else {
            rep.mkmk_pairs += static_cast<int>(it->second.size());
        }
    }
    return out;
}

}  // namespace

Bytes build_gpos(const model::Kerning& k, const Prepared& p, OtfReport& rep) {
    std::vector<Lookup> lookups;
    std::vector<Feature> features;
    if (auto ks = kern_subtables(k, p, rep); !ks.empty()) {
        features.push_back({"kern", {static_cast<int>(lookups.size())}});
        lookups.push_back({2, 0, std::move(ks)});
    }
    const MarkData md = mark_data(p);
    if (auto ms = mark_subtables(md, false, rep); !ms.empty()) {
        features.push_back({"mark", {static_cast<int>(lookups.size())}});
        lookups.push_back({4, 0, std::move(ms)});
    }
    if (auto ms = mark_subtables(md, true, rep); !ms.empty()) {
        features.push_back({"mkmk", {static_cast<int>(lookups.size())}});
        lookups.push_back({6, 0, std::move(ms)});
    }
    int n = 0;
    for (const auto& l : lookups) n += static_cast<int>(l.subtables.size());
    rep.gpos_subtables = n;
    return layout_table(std::move(features), lookups, 9);
}

Bytes build_gdef(const Prepared& p) {
    const MarkData md = mark_data(p);
    if (md.marks.empty()) return {};
    std::vector<std::pair<int, int>> cls;
    for (std::size_t g = 0; g < p.glyphs.size(); ++g) cls.push_back({static_cast<int>(g), md.is_mark[g] ? 3 : 1});
    const Bytes cd = class_def(cls);
    Writer w;
    w.u16(1); w.u16(0);
    w.u16(12);   // GlyphClassDef right after the header
    w.u16(0); w.u16(0); w.u16(0);
    w.bytes(cd);
    return w.b;
}

}  // namespace granjon::otf
