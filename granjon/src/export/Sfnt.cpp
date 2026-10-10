#include "export/Sfnt.hpp"

#include <algorithm>

namespace granjon::otf {

std::uint32_t checksum(const Bytes& d) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < d.size(); i += 4) {
        std::uint32_t w = 0;
        for (std::size_t k = 0; k < 4; ++k)
            w = (w << 8) | (i + k < d.size() ? d[i + k] : 0u);
        sum += w;
    }
    return sum;
}

Bytes assemble_sfnt(std::uint32_t sfnt_version, const std::map<std::string, Bytes>& tables) {
    // Data order: the spec's recommendation for CFF and TrueType fonts, then
    // anything else by tag. The directory itself is sorted by tag (std::map).
    static const char* kOrder[] = {"head", "hhea", "maxp", "OS/2", "hmtx", "LTSH", "VDMX", "hdmx", "cmap",
                                   "fpgm", "prep", "cvt ", "loca", "glyf", "kern", "name", "post", "gasp",
                                   "PCLT", "DSIG", "CFF ", "GDEF", "GPOS", "GSUB"};
    std::vector<std::string> order;
    for (const char* t : kOrder)
        if (tables.count(t)) order.push_back(t);
    for (const auto& [t, _] : tables)
        if (std::find(order.begin(), order.end(), t) == order.end()) order.push_back(t);

    const unsigned n = static_cast<unsigned>(tables.size());
    unsigned pow2 = 1, log2 = 0;
    while (pow2 * 2 <= n) { pow2 *= 2; ++log2; }

    Writer w;
    w.u32(sfnt_version);
    w.u16(n);
    w.u16(pow2 * 16);
    w.u16(log2);
    w.u16(n * 16 - pow2 * 16);

    std::map<std::string, std::uint32_t> offset;
    std::uint32_t at = 12 + 16 * n;
    for (const auto& t : order) {
        offset[t] = at;
        at += (static_cast<std::uint32_t>(tables.at(t).size()) + 3) & ~3u;
    }
    for (const auto& [t, data] : tables) {   // sorted by tag
        w.tag(t.c_str());
        w.u32(checksum(data));
        w.u32(offset[t]);
        w.u32(static_cast<std::uint32_t>(data.size()));
    }
    std::size_t head_at = 0;
    for (const auto& t : order) {
        if (t == "head") head_at = w.size();
        w.bytes(tables.at(t));
        while (w.size() % 4) w.u8(0);
    }
    // checkSumAdjustment: 0xB1B0AFBA minus the whole file's checksum, computed
    // with the field at zero (it is: head is built with it zero).
    if (tables.count("head")) w.put_u32(head_at + 8, 0xB1B0AFBAu - checksum(w.b));
    return w.b;
}

Bytes table(const Bytes& font, const std::string& tag) {
    auto rd16 = [&](std::size_t i) { return static_cast<unsigned>(font[i] << 8 | font[i + 1]); };
    auto rd32 = [&](std::size_t i) { return static_cast<std::uint32_t>(rd16(i)) << 16 | rd16(i + 2); };
    if (font.size() < 12 || tag.size() != 4) return {};
    const unsigned n = rd16(4);
    for (unsigned i = 0; i < n; ++i) {
        const std::size_t rec = 12 + 16 * i;
        if (rec + 16 > font.size()) return {};
        if (std::equal(tag.begin(), tag.end(), font.begin() + static_cast<long>(rec))) {
            const std::uint32_t off = rd32(rec + 8), len = rd32(rec + 12);
            if (static_cast<std::size_t>(off) + len > font.size()) return {};
            return Bytes(font.begin() + off, font.begin() + off + len);
        }
    }
    return {};
}

}  // namespace granjon::otf
