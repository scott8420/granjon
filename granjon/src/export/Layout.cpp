// otf::Layout (M16) -- see Layout.hpp.
#include "export/Layout.hpp"

#include <algorithm>

namespace granjon::otf {

Bytes coverage(std::vector<int> gids) {
    std::sort(gids.begin(), gids.end());
    gids.erase(std::unique(gids.begin(), gids.end()), gids.end());
    Writer w;
    w.u16(1);
    w.u16(static_cast<unsigned>(gids.size()));
    for (int g : gids) w.u16(static_cast<unsigned>(g));
    return w.b;
}

Bytes class_def(std::vector<std::pair<int, int>> gid_class) {
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

Bytes layout_table(std::vector<Feature> features, const std::vector<Lookup>& lookups, int extension_type) {
    if (lookups.empty()) return {};
    std::stable_sort(features.begin(), features.end(), [](const Feature& a, const Feature& b) { return a.tag < b.tag; });
    const std::size_t nf = features.size();

    Writer scripts;   // DFLT and latn
    const std::size_t script_size = 4 + 6 + 2 * nf;
    scripts.u16(2);
    scripts.tag("DFLT"); scripts.u16(static_cast<unsigned>(14));
    scripts.tag("latn"); scripts.u16(static_cast<unsigned>(14 + script_size));
    for (int s = 0; s < 2; ++s) {
        scripts.u16(4); scripts.u16(0);                        // defaultLangSys right after; no languages
        scripts.u16(0); scripts.u16(0xFFFF); scripts.u16(static_cast<unsigned>(nf));
        for (std::size_t i = 0; i < nf; ++i) scripts.u16(static_cast<unsigned>(i));
    }

    Writer feats;
    feats.u16(static_cast<unsigned>(nf));
    std::size_t at = 2 + 6 * nf;
    for (const auto& f : features) {
        feats.tag(f.tag.c_str());
        feats.u16(static_cast<unsigned>(at));
        at += 4 + 2 * f.lookups.size();
    }
    for (const auto& f : features) {
        feats.u16(0);
        feats.u16(static_cast<unsigned>(f.lookups.size()));
        for (int l : f.lookups) feats.u16(static_cast<unsigned>(l));
    }

    // LookupList
    std::size_t total = 0;
    for (const auto& l : lookups) for (const auto& s : l.subtables) total += s.size();
    const bool ext = total + 2 + 2 * lookups.size() + 8 * lookups.size() > 60000;
    Writer list;
    const std::size_t nl = lookups.size();
    list.u16(static_cast<unsigned>(nl));
    std::vector<std::size_t> lsize;
    for (const auto& l : lookups) {
        const std::size_t head = 6 + 2 * l.subtables.size();
        std::size_t body = 0;
        if (ext) body = 8 * l.subtables.size();
        else for (const auto& s : l.subtables) body += s.size();
        lsize.push_back(head + body);
    }
    std::size_t pos = 2 + 2 * nl;
    std::vector<std::size_t> lat;
    for (std::size_t i = 0; i < nl; ++i) { lat.push_back(pos); list.u16(static_cast<unsigned>(pos)); pos += lsize[i]; }
    std::size_t data = pos;   // extension data starts after every lookup
    for (std::size_t i = 0; i < nl; ++i) {
        const auto& l = lookups[i];
        const std::size_t head = 6 + 2 * l.subtables.size();
        list.u16(static_cast<unsigned>(ext ? extension_type : l.type));
        list.u16(static_cast<unsigned>(l.flag));
        list.u16(static_cast<unsigned>(l.subtables.size()));
        if (!ext) {
            std::size_t o = head;
            for (const auto& s : l.subtables) { list.u16(static_cast<unsigned>(o)); o += s.size(); }
            for (const auto& s : l.subtables) list.bytes(s);
        } else {
            for (std::size_t k = 0; k < l.subtables.size(); ++k) list.u16(static_cast<unsigned>(head + 8 * k));
            for (std::size_t k = 0; k < l.subtables.size(); ++k) {
                const std::size_t here = lat[i] + head + 8 * k;
                list.u16(1);
                list.u16(static_cast<unsigned>(l.type));
                list.u32(static_cast<std::uint32_t>(data - here));
                data += l.subtables[k].size();
            }
        }
    }
    if (ext) for (const auto& l : lookups) for (const auto& s : l.subtables) list.bytes(s);

    Writer w;
    const std::size_t sl = 10, fl = sl + scripts.size(), ll = fl + feats.size();
    w.u16(1); w.u16(0);
    w.u16(static_cast<unsigned>(sl));
    w.u16(static_cast<unsigned>(fl));
    w.u16(static_cast<unsigned>(ll));
    w.bytes(scripts.b);
    w.bytes(feats.b);
    w.bytes(list.b);
    return w.b;
}

}  // namespace granjon::otf
