// reader::GposRead -- GPOS 'kern' pair positioning (or the old 'kern' table)
// into a UFO-style Kerning: glyph pairs, public.kern1 / kern2 groups, group
// pairs.
//
// OpenType stops at the first subtable whose coverage holds the left glyph
// (a class subtable answers for every right glyph, 0 included). UFO resolves
// glyph/glyph before group/group. So: glyph pairs are kept first-wins, and a
// glyph pair from a LATER subtable than a class subtable that already covers
// its left glyph is dropped (OpenType would never reach it). A glyph that two
// class subtables put in different groups keeps its first group; the other
// subtable's values for it come in as glyph pairs.
#include "import/ReaderParts.hpp"

#include <algorithm>
#include <bit>
#include <set>

namespace granjon::reader {

namespace {

std::vector<int> coverage(Span s) {
    std::vector<int> out;
    const unsigned fmt = s.u16(0), n = s.u16(2);
    if (fmt == 1) {
        for (unsigned i = 0; i < n; ++i) out.push_back(static_cast<int>(s.u16(4 + 2 * i)));
    } else if (fmt == 2) {
        for (unsigned i = 0; i < n; ++i) {
            const unsigned a = s.u16(4 + 6 * i), b = s.u16(6 + 6 * i);
            for (unsigned g = a; g <= b; ++g) out.push_back(static_cast<int>(g));
        }
    } else {
        throw BadFont("an unknown Coverage format");
    }
    return out;
}

std::map<int, int> class_def(Span s) {
    std::map<int, int> out;
    const unsigned fmt = s.u16(0);
    if (fmt == 1) {
        const unsigned start = s.u16(2), n = s.u16(4);
        for (unsigned i = 0; i < n; ++i) if (const unsigned c = s.u16(6 + 2 * i)) out[static_cast<int>(start + i)] = static_cast<int>(c);
    } else if (fmt == 2) {
        const unsigned n = s.u16(2);
        for (unsigned i = 0; i < n; ++i) {
            const unsigned a = s.u16(4 + 6 * i), b = s.u16(6 + 6 * i), c = s.u16(8 + 6 * i);
            if (c) for (unsigned g = a; g <= b; ++g) out[static_cast<int>(g)] = static_cast<int>(c);
        }
    } else {
        throw BadFont("an unknown ClassDef format");
    }
    return out;
}

int value_size(unsigned fmt) { return 2 * std::popcount(fmt & 0xFFu); }
// XAdvance's place in a value record, or -1.
int x_advance_at(unsigned fmt) { return (fmt & 0x4) ? 2 * std::popcount(fmt & 0x3u) : -1; }

struct Builder {
    const std::vector<std::string>& names;
    std::map<std::pair<int, int>, int> glyph_pairs;
    std::set<int> class_covered;                         // left glyphs an earlier class subtable answers for
    std::map<int, std::string> group_of[2];              // gid -> group, per side
    std::map<std::string, std::vector<int>> groups[2];
    std::map<std::pair<std::string, std::string>, int> group_pairs;
    int class0_right = 0;

    std::string name_of(int g) const {
        return g >= 0 && g < static_cast<int>(names.size()) ? names[static_cast<std::size_t>(g)] : std::string{};
    }
    // The group for these members on a side: a group with exactly these
    // members if there is one; else a new one of the members not yet grouped.
    // `taken` gets the members that already sit in another group.
    std::string group_for(int side, std::vector<int> members, std::vector<int>& taken) {
        std::sort(members.begin(), members.end());
        for (const auto& [n, m] : groups[side]) if (m == members) return n;
        std::vector<int> free;
        for (int g : members) (group_of[side].count(g) ? taken : free).push_back(g);
        if (free.empty()) return {};
        std::string base = std::string(side == 0 ? "public.kern1." : "public.kern2.") + name_of(free.front());
        std::string n = base;
        for (int k = 2; groups[side].count(n); ++k) n = base + "." + std::to_string(k);
        groups[side][n] = free;
        for (int g : free) group_of[side][g] = n;
        return n;
    }
    void pair(int l, int r, int v) { glyph_pairs.emplace(std::pair{l, r}, v); }

    void format1(Span st, unsigned vf1, unsigned vf2) {
        const auto cov = coverage(st.from(st.u16(2)));
        const unsigned n = st.u16(8);
        const int rec = 2 + value_size(vf1) + value_size(vf2), xa = x_advance_at(vf1);
        for (unsigned i = 0; i < n && i < cov.size(); ++i) {
            const int l = cov[i];
            if (class_covered.count(l)) continue;
            const Span ps = st.from(st.u16(10 + 2 * i));
            const unsigned m = ps.u16(0);
            for (unsigned k = 0; k < m; ++k) {
                const std::size_t at = 2 + static_cast<std::size_t>(k) * static_cast<std::size_t>(rec);
                const int r = static_cast<int>(ps.u16(at));
                const int v = xa < 0 ? 0 : ps.i16(at + 2 + static_cast<std::size_t>(xa));
                pair(l, r, v);
            }
        }
    }

    void format2(Span st, unsigned vf1, unsigned vf2, int glyph_count) {
        const auto cov = coverage(st.from(st.u16(2)));
        const auto cd1 = class_def(st.from(st.u16(8)));
        const auto cd2 = class_def(st.from(st.u16(10)));
        const unsigned c1n = st.u16(12), c2n = st.u16(14);
        const int rec = value_size(vf1) + value_size(vf2), xa = x_advance_at(vf1);
        std::map<int, std::vector<int>> left, right;
        for (int g : cov) {
            if (class_covered.count(g)) continue;   // an earlier class subtable answers for it
            auto it = cd1.find(g);
            left[it == cd1.end() ? 0 : it->second].push_back(g);
        }
        for (const auto& [g, c] : cd2) if (g < glyph_count) right[c].push_back(g);
        for (unsigned a = 0; a < c1n; ++a) {
            auto L = left.find(static_cast<int>(a));
            if (L == left.end()) continue;
            for (unsigned b = 0; b < c2n; ++b) {
                const std::size_t at = 16 + (static_cast<std::size_t>(a) * c2n + b) * static_cast<std::size_t>(rec);
                const int v = xa < 0 ? 0 : st.i16(at + static_cast<std::size_t>(xa));
                if (v == 0) continue;
                if (b == 0) { ++class0_right; continue; }
                auto R = right.find(static_cast<int>(b));
                if (R == right.end()) continue;
                std::vector<int> taken_l, taken_r;
                const std::string gl = group_for(0, L->second, taken_l);
                const std::string gr = group_for(1, R->second, taken_r);
                if (!gl.empty() && !gr.empty()) group_pairs.emplace(std::pair{gl, gr}, v);
                // Members another subtable already grouped: their values here as glyph pairs.
                for (int l : taken_l) for (int r : R->second) pair(l, r, v);
                if (!gl.empty()) for (int r : taken_r) for (int l : groups[0][gl]) pair(l, r, v);
            }
        }
        for (const auto& [c, gs] : left) for (int g : gs) class_covered.insert(g);
    }
};

}  // namespace

model::Kerning read_kerning(Span gpos, Span kern, const std::vector<std::string>& names, std::vector<std::string>& notes) {
    model::Kerning k;
    Builder b{names};
    const int gc = static_cast<int>(names.size());
    bool any = false;
    if (!gpos.empty()) {
        const Span fl = gpos.from(gpos.u16(6)), ll = gpos.from(gpos.u16(8));
        std::set<unsigned> lookups;
        const unsigned nf = fl.u16(0);
        for (unsigned i = 0; i < nf; ++i) {
            const std::string tag(reinterpret_cast<const char*>(fl.sub(2 + 6 * i, 4).p), 4);
            if (tag != "kern") continue;
            const Span f = fl.from(fl.u16(6 + 6 * i));
            const unsigned nl = f.u16(2);
            for (unsigned j = 0; j < nl; ++j) lookups.insert(f.u16(4 + 2 * j));
        }
        const unsigned lc = ll.u16(0);
        int used = 0;
        for (unsigned li : lookups) {
            if (li >= lc) continue;
            const Span lk = ll.from(ll.u16(2 + 2 * li));
            const unsigned type = lk.u16(0), ns = lk.u16(4);
            if (type != 2 && type != 9) continue;
            for (unsigned s = 0; s < ns; ++s) {
                Span st = lk.from(lk.u16(6 + 2 * s));
                if (type == 9) {
                    if (st.u16(2) != 2) continue;
                    st = st.from(st.u32(4));
                }
                const unsigned fmt = st.u16(0), vf1 = st.u16(4), vf2 = st.u16(6);
                if (fmt == 1) b.format1(st, vf1, vf2);
                else if (fmt == 2) b.format2(st, vf1, vf2, gc);
                any = true;
            }
            ++used;
        }
        if (used > 1) notes.push_back("Kerning came from " + std::to_string(used) +
                                      " lookups, merged first-wins (OpenType would add them if they overlap).");
    }
    if (!any && kern.n >= 4 && kern.u16(0) == 0) {   // the old 'kern' table, format 0 subtables
        const unsigned nt = kern.u16(2);
        std::size_t at = 4;
        for (unsigned t = 0; t < nt; ++t) {
            const unsigned len = kern.u16(at + 2), cov = kern.u16(at + 4);
            if ((cov >> 8) == 0 && (cov & 1)) {   // format 0, horizontal
                const unsigned np = kern.u16(at + 6);
                for (unsigned i = 0; i < np; ++i) {
                    const std::size_t p = at + 14 + 6 * static_cast<std::size_t>(i);
                    b.pair(static_cast<int>(kern.u16(p)), static_cast<int>(kern.u16(p + 2)), kern.i16(p + 4));
                }
                any = true;
            }
            at += len;
        }
    }
    if (b.class0_right) notes.push_back(std::to_string(b.class0_right) + " class kern value(s) against 'every other glyph' were left out.");

    for (int side = 0; side < 2; ++side)
        for (const auto& [n, m] : b.groups[side]) {
            auto& v = k.groups[n];
            for (int g : m) v.push_back(b.name_of(g));
        }
    for (const auto& [gp, v] : b.group_pairs) k.pairs[gp] = v;
    for (const auto& [lr, v] : b.glyph_pairs) {
        const std::string l = b.name_of(lr.first), r = b.name_of(lr.second);
        if (l.empty() || r.empty()) continue;
        // A zero pair only matters where a group pair could otherwise answer.
        if (v == 0 && !b.group_of[0].count(lr.first) && !b.group_of[1].count(lr.second)) continue;
        k.pairs[{l, r}] = v;
    }
    return k;
}

}  // namespace granjon::reader
