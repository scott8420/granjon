// otf::Gsub (M16c) -- the feature file's substitutions as a GSUB table, on
// the shared layout frame (Layout.hpp). Glyph names are the designer's; a
// glyph left out of the font (a part) is an error like an unknown one.
// Any error in the file: no GSUB at all, and the report says the first few
// (the FEATURES view shows them as you type).
#include "export/Layout.hpp"
#include "export/OtfParts.hpp"
#include "model/Fea.hpp"

#include <algorithm>
#include <map>

namespace granjon::otf {

namespace {

using Kind = model::FeaRule::Kind;

Bytes single(const std::map<int, int>& m) {   // format 2
    std::vector<int> cov;
    for (const auto& [a, _] : m) cov.push_back(a);
    const Bytes cv = coverage(cov);
    Writer w;
    w.u16(2);
    w.u16(static_cast<unsigned>(6 + 2 * m.size()));
    w.u16(static_cast<unsigned>(m.size()));
    for (const auto& [_, b] : m) w.u16(static_cast<unsigned>(b));
    w.bytes(cv);
    return w.b;
}

// Multiple (type 2) and Alternate (type 3) share a shape: coverage + sets of glyphs.
Bytes sets(const std::map<int, std::vector<int>>& m) {
    std::vector<int> cov;
    for (const auto& [a, _] : m) cov.push_back(a);
    const std::size_t head = 6 + 2 * m.size();
    Writer body;
    std::vector<std::size_t> at;
    for (const auto& [_, v] : m) {
        at.push_back(head + body.size());
        body.u16(static_cast<unsigned>(v.size()));
        for (int g : v) body.u16(static_cast<unsigned>(g));
    }
    const Bytes cv = coverage(cov);
    Writer w;
    w.u16(1);
    w.u16(static_cast<unsigned>(head + body.size()));   // coverage after the sets
    w.u16(static_cast<unsigned>(m.size()));
    for (std::size_t a : at) w.u16(static_cast<unsigned>(a));
    w.bytes(body.b);
    w.bytes(cv);
    return w.b;
}

struct Lig { std::vector<int> comps; int glyph; };   // comps: the ones after the first

Bytes ligatures(std::map<int, std::vector<Lig>> m) {
    std::vector<int> cov;
    for (auto& [a, v] : m) {
        cov.push_back(a);
        std::stable_sort(v.begin(), v.end(), [](const Lig& x, const Lig& y) { return x.comps.size() > y.comps.size(); });   // longest first
    }
    const std::size_t head = 6 + 2 * m.size();
    Writer body;
    std::vector<std::size_t> set_at;
    for (const auto& [_, v] : m) {
        const std::size_t s = head + body.size();
        set_at.push_back(s);
        Writer set;
        const std::size_t shead = 2 + 2 * v.size();
        Writer ligs;
        set.u16(static_cast<unsigned>(v.size()));
        for (const auto& l : v) {
            set.u16(static_cast<unsigned>(shead + ligs.size()));
            ligs.u16(static_cast<unsigned>(l.glyph));
            ligs.u16(static_cast<unsigned>(l.comps.size() + 1));
            for (int c : l.comps) ligs.u16(static_cast<unsigned>(c));
        }
        body.bytes(set.b);
        body.bytes(ligs.b);
    }
    const Bytes cv = coverage(cov);
    Writer w;
    w.u16(1);
    w.u16(static_cast<unsigned>(head + body.size()));
    w.u16(static_cast<unsigned>(m.size()));
    for (std::size_t a : set_at) w.u16(static_cast<unsigned>(a));
    w.bytes(body.b);
    w.bytes(cv);
    return w.b;
}

// ChainContextSubst format 3: coverage per position; the lookups to apply
// at input positions. `back` goes closest-first, as the table wants.
Bytes chain(const std::vector<std::vector<int>>& back_reading, const std::vector<std::vector<int>>& in,
            const std::vector<std::vector<int>>& ahead, const std::vector<std::pair<int, int>>& recs) {
    std::vector<std::vector<int>> back(back_reading.rbegin(), back_reading.rend());
    const std::size_t head = 2 + 2 + 2 * back.size() + 2 + 2 * in.size() + 2 + 2 * ahead.size() + 2 + 4 * recs.size();
    std::vector<Bytes> covs;
    using Pos = std::vector<std::vector<int>>;
    const Pos* lists[] = {&back, &in, &ahead};
    for (const Pos* list : lists)
        for (const auto& v : *list) covs.push_back(coverage(v));
    std::vector<std::size_t> at;
    std::size_t o = head;
    for (const auto& c : covs) { at.push_back(o); o += c.size(); }
    Writer w;
    w.u16(3);
    std::size_t k = 0;
    w.u16(static_cast<unsigned>(back.size()));
    for (std::size_t i = 0; i < back.size(); ++i) w.u16(static_cast<unsigned>(at[k++]));
    w.u16(static_cast<unsigned>(in.size()));
    for (std::size_t i = 0; i < in.size(); ++i) w.u16(static_cast<unsigned>(at[k++]));
    w.u16(static_cast<unsigned>(ahead.size()));
    for (std::size_t i = 0; i < ahead.size(); ++i) w.u16(static_cast<unsigned>(at[k++]));
    w.u16(static_cast<unsigned>(recs.size()));
    for (const auto& [seq, li] : recs) { w.u16(static_cast<unsigned>(seq)); w.u16(static_cast<unsigned>(li)); }
    for (const auto& c : covs) w.bytes(c);
    return w.b;
}

}  // namespace

Bytes build_gsub(const std::string& fea, const Prepared& p, OtfReport& rep, int* max_context) {
    if (fea.find_first_not_of(" \t\r\n") == std::string::npos) return {};
    auto gid = [&](const std::string& n) { auto it = p.gid.find(n); return it == p.gid.end() ? -1 : it->second; };
    const model::FeaDoc doc = model::parse_fea(fea, [&](const std::string& n) { return gid(n) >= 0; });
    for (const auto& w : doc.warnings)
        if (rep.notes.size() < 40) rep.notes.push_back("features.fea line " + std::to_string(w.line) + ": " + w.text);
    if (!doc.ok()) {
        std::string first;
        for (std::size_t i = 0; i < doc.errors.size() && i < 3; ++i)
            first += (i ? "; " : "") + std::string("line ") + std::to_string(doc.errors[i].line) + ": " + doc.errors[i].text;
        rep.notes.push_back("features.fea has " + std::to_string(doc.errors.size()) +
                            (doc.errors.size() == 1 ? " error" : " errors") + " -- no GSUB written (" + first + ").");
        rep.fea_errors = static_cast<int>(doc.errors.size());
        return {};
    }
    std::vector<Lookup> lookups;
    std::vector<int> remap(doc.lookups.size(), -1);   // fea lookup -> GSUB lookup (empty ones dropped)
    int ctx = 1;
    for (std::size_t li = 0; li < doc.lookups.size(); ++li) {
        const auto& L = doc.lookups[li];
        if (L.rules.empty()) continue;
        Lookup out;
        switch (L.kind) {
            case Kind::Single: {
                std::map<int, int> m;
                for (const auto& r : L.rules)
                    for (std::size_t i = 0; i < r.in[0].size(); ++i) m.emplace(gid(r.in[0][i]), gid(r.out[i]));   // first rule wins
                out = {1, 0, {single(m)}};
                break;
            }
            case Kind::Multiple:
            case Kind::Alternate: {
                std::map<int, std::vector<int>> m;
                for (const auto& r : L.rules) {
                    std::vector<int> v;
                    for (const auto& n : r.out) v.push_back(gid(n));
                    m.emplace(gid(r.in[0][0]), v);
                }
                out = {L.kind == Kind::Multiple ? 2 : 3, 0, {sets(m)}};
                break;
            }
            case Kind::Chain:
                break;   // after every other lookup has its index (below)
            case Kind::Ligature: {
                std::map<int, std::vector<Lig>> m;
                for (const auto& r : L.rules) {
                    // every combination the classes name
                    std::vector<std::size_t> at(r.in.size(), 0);
                    while (true) {
                        Lig l;
                        l.glyph = gid(r.out[0]);
                        for (std::size_t k = 1; k < r.in.size(); ++k) l.comps.push_back(gid(r.in[k][at[k]]));
                        m[gid(r.in[0][at[0]])].push_back(l);
                        ctx = std::max(ctx, static_cast<int>(r.in.size()));
                        std::size_t k = 0;
                        while (k < at.size() && ++at[k] == r.in[k].size()) at[k++] = 0;
                        if (k == at.size()) break;
                    }
                }
                out = {4, 0, {ligatures(m)}};
                break;
            }
        }
        remap[li] = static_cast<int>(lookups.size());
        lookups.push_back(std::move(out));
    }
    // contextual lookups: their records point at other lookups, now numbered
    for (std::size_t li = 0; li < doc.lookups.size(); ++li) {
        const auto& L = doc.lookups[li];
        if (L.kind != Kind::Chain || L.rules.empty()) continue;
        Lookup& out = lookups[static_cast<std::size_t>(remap[li])];
        out.type = 6;
        auto gids = [&](const std::vector<std::vector<std::string>>& pos) {
            std::vector<std::vector<int>> v;
            for (const auto& p : pos) {
                std::vector<int> g;
                for (const auto& n : p) g.push_back(gid(n));
                v.push_back(std::move(g));
            }
            return v;
        };
        for (const auto& r : L.rules) {
            std::vector<std::pair<int, int>> recs;
            for (const auto& [seq, target] : r.lookups)
                if (remap[static_cast<std::size_t>(target)] >= 0) recs.push_back({seq, remap[static_cast<std::size_t>(target)]});
            out.subtables.push_back(chain(gids(r.back), gids(r.in), gids(r.ahead), recs));
            ctx = std::max(ctx, static_cast<int>(r.back.size() + r.in.size() + r.ahead.size()));
        }
    }
    std::vector<Feature> features;
    for (const auto& f : doc.features) {
        Feature x;
        x.tag = f.tag;
        for (int l : f.lookups) if (remap[static_cast<std::size_t>(l)] >= 0) x.lookups.push_back(remap[static_cast<std::size_t>(l)]);
        if (!x.lookups.empty()) features.push_back(std::move(x));
    }
    if (features.empty()) return {};
    rep.gsub_features = static_cast<int>(features.size());
    rep.gsub_rules = doc.rules;
    if (max_context) *max_context = ctx;
    return layout_table(std::move(features), lookups, 7);
}

}  // namespace granjon::otf
