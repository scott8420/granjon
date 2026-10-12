// otf::Cff -- the 'CFF ' table (CFF 1.0 + Type 2 charstrings).
//
// One font, no subroutines, no hints. Layout:
//   Header | Name INDEX | Top DICT INDEX | String INDEX | Global Subr INDEX (empty)
//   | charset (format 0) | CharStrings INDEX | Private DICT
// The Top DICT's offsets are written as 5-byte integers, so its length does
// not depend on where things land and one layout pass suffices.
//
// Widths: defaultWidthX = nominalWidthX = the commonest advance; a glyph with
// another width carries (width - nominal) as the first charstring argument.
#include "export/OtfData.hpp"
#include "export/OtfParts.hpp"

#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>

namespace granjon::otf {

namespace {

// ── numbers ────────────────────────────────────────────────────────────────
void t2num(Bytes& o, int v) {   // charstring operand
    if (v >= -107 && v <= 107) {
        o.push_back(static_cast<std::uint8_t>(v + 139));
    } else if (v >= 108 && v <= 1131) {
        v -= 108;
        o.push_back(static_cast<std::uint8_t>((v >> 8) + 247));
        o.push_back(static_cast<std::uint8_t>(v & 0xFF));
    } else if (v >= -1131 && v <= -108) {
        v = -v - 108;
        o.push_back(static_cast<std::uint8_t>((v >> 8) + 251));
        o.push_back(static_cast<std::uint8_t>(v & 0xFF));
    } else {
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        o.push_back(28);
        o.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        o.push_back(static_cast<std::uint8_t>(v & 0xFF));
    }
}

void dnum5(Bytes& o, std::int32_t v) {   // DICT operand, fixed 5 bytes
    const auto u = static_cast<std::uint32_t>(v);
    o.push_back(29);
    o.push_back(static_cast<std::uint8_t>(u >> 24));
    o.push_back(static_cast<std::uint8_t>(u >> 16));
    o.push_back(static_cast<std::uint8_t>(u >> 8));
    o.push_back(static_cast<std::uint8_t>(u));
}

void dnum(Bytes& o, int v) {   // DICT operand, shortest
    if (v >= -107 && v <= 107) {
        o.push_back(static_cast<std::uint8_t>(v + 139));
    } else if (v >= 108 && v <= 1131) {
        v -= 108;
        o.push_back(static_cast<std::uint8_t>((v >> 8) + 247));
        o.push_back(static_cast<std::uint8_t>(v & 0xFF));
    } else if (v >= -1131 && v <= -108) {
        v = -v - 108;
        o.push_back(static_cast<std::uint8_t>((v >> 8) + 251));
        o.push_back(static_cast<std::uint8_t>(v & 0xFF));
    } else if (v >= -32768 && v <= 32767) {
        o.push_back(28);
        o.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        o.push_back(static_cast<std::uint8_t>(v & 0xFF));
    } else {
        dnum5(o, v);
    }
}

void dreal(Bytes& o, double v) {   // DICT real: BCD nibbles
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.10g", v);
    std::vector<int> nib;
    for (const char* p = buf; *p; ++p) {
        const char c = *p;
        if (c >= '0' && c <= '9') nib.push_back(c - '0');
        else if (c == '.') nib.push_back(0xA);
        else if (c == '-') nib.push_back(0xE);
        else if (c == 'e' || c == 'E') {
            if (p[1] == '-') { nib.push_back(0xC); ++p; }
            else { nib.push_back(0xB); if (p[1] == '+') ++p; }
        }
    }
    nib.push_back(0xF);
    if (nib.size() % 2) nib.push_back(0xF);
    o.push_back(30);
    for (std::size_t i = 0; i < nib.size(); i += 2)
        o.push_back(static_cast<std::uint8_t>(nib[i] << 4 | nib[i + 1]));
}

void dop(Bytes& o, int op) {   // 1200+ = escape 12
    if (op >= 1200) { o.push_back(12); o.push_back(static_cast<std::uint8_t>(op - 1200)); }
    else o.push_back(static_cast<std::uint8_t>(op));
}

Bytes index(const std::vector<Bytes>& items) {
    Writer w;
    w.u16(static_cast<unsigned>(items.size()));
    if (items.empty()) return w.b;
    std::size_t total = 0;
    for (const auto& it : items) total += it.size();
    const int off_size = total + 1 <= 0xFF ? 1 : total + 1 <= 0xFFFF ? 2 : total + 1 <= 0xFFFFFF ? 3 : 4;
    w.u8(static_cast<unsigned>(off_size));
    auto put = [&](std::size_t v) {
        for (int k = off_size - 1; k >= 0; --k) w.u8(static_cast<unsigned>(v >> (8 * k)));
    };
    std::size_t off = 1;
    put(off);
    for (const auto& it : items) { off += it.size(); put(off); }
    for (const auto& it : items) w.bytes(it);
    return w.b;
}

// ── charstrings ────────────────────────────────────────────────────────────
// Batches same-operator runs up to the Type 2 argument stack (48).
struct Cs {
    Bytes            out;
    std::vector<int> args;
    int              op = -1;
    bool             width_due = false;
    int              width = 0;

    void flush() {
        if (op < 0) return;
        if (width_due) { t2num(out, width); width_due = false; }
        for (int a : args) t2num(out, a);
        out.push_back(static_cast<std::uint8_t>(op));
        args.clear();
        op = -1;
    }
    void emit(int o, std::initializer_list<int> a, bool batch) {
        const std::size_t cap = 48 - (width_due ? 1u : 0u);
        if (op >= 0 && (op != o || args.size() + a.size() > cap)) flush();
        op = o;
        args.insert(args.end(), a.begin(), a.end());
        if (!batch) flush();
    }
};

int r(double v) { return static_cast<int>(std::lround(v)); }

}  // namespace

Bytes charstring(const OutGlyph& g, int default_width, int nominal_width) {
    constexpr int kRmoveto = 21, kRlineto = 5, kRrcurveto = 8, kEndchar = 14;
    Cs cs;
    if (g.advance != default_width) { cs.width_due = true; cs.width = g.advance - nominal_width; }
    int cx = 0, cy = 0;
    for (const auto& c : g.contours) {
        const auto& N = c.nodes;
        const std::size_t n = N.size();
        if (n == 0) continue;
        if (n == 1 && !(c.closed && (N[0].has_in() || N[0].has_out()))) continue;   // a lone point draws nothing
        const std::size_t segs = c.closed ? n : n - 1;
        const int x0 = r(N[0].x), y0 = r(N[0].y);
        cs.emit(kRmoveto, {x0 - cx, y0 - cy}, false);
        cx = x0; cy = y0;
        for (std::size_t i = 0; i < segs; ++i) {
            const auto& a = N[i];
            const auto& b = N[(i + 1) % n];
            const int bx = r(b.x), by = r(b.y);
            if (!a.has_out() && !b.has_in()) {
                if (c.closed && i == n - 1) break;   // the implicit close draws it
                cs.emit(kRlineto, {bx - cx, by - cy}, true);
            } else {
                const int x1 = r(a.cx2), y1 = r(a.cy2), x2 = r(b.cx1), y2 = r(b.cy1);
                cs.emit(kRrcurveto, {x1 - cx, y1 - cy, x2 - x1, y2 - y1, bx - x2, by - y2}, true);
            }
            cx = bx; cy = by;
        }
    }
    cs.flush();
    if (cs.width_due) t2num(cs.out, cs.width);
    cs.out.push_back(kEndchar);
    return cs.out;
}

Bytes build_cff(const Prepared& p) {
    // Strings: standard SIDs for standard names, the rest in the String INDEX.
    static const std::unordered_map<std::string, int> standard = [] {
        std::unordered_map<std::string, int> m;
        for (int i = 0; i < kCffStandardCount; ++i) m.emplace(kCffStandardStrings[i], i);
        return m;
    }();
    std::vector<Bytes>         custom;
    std::map<std::string, int> custom_sid;
    auto sid = [&](const std::string& s) {
        if (auto it = standard.find(s); it != standard.end()) return it->second;
        if (auto it = custom_sid.find(s); it != custom_sid.end()) return it->second;
        const int id = kCffStandardCount + static_cast<int>(custom.size());
        custom.emplace_back(s.begin(), s.end());
        custom_sid.emplace(s, id);
        return id;
    };

    // Widths.
    std::map<int, int> freq;
    for (const auto& g : p.glyphs) ++freq[g.advance];
    int common = 0, best = -1;
    for (const auto& [w, n] : freq)
        if (n > best) { best = n; common = w; }

    // Private DICT.
    Bytes priv;
    dnum(priv, common); dop(priv, 20);   // defaultWidthX
    dnum(priv, common); dop(priv, 21);   // nominalWidthX

    // charset (format 0) and CharStrings.
    Writer charset;
    charset.u8(0);
    std::vector<Bytes> cstr;
    cstr.reserve(p.glyphs.size());
    for (std::size_t i = 0; i < p.glyphs.size(); ++i) {
        if (i > 0) charset.u16(static_cast<unsigned>(sid(p.glyphs[i].name)));
        cstr.push_back(charstring(p.glyphs[i], common, common));
    }
    const Bytes charstrings = index(cstr);

    // Top DICT (strings first so their SIDs exist before the String INDEX).
    char ver[32];
    std::snprintf(ver, sizeof ver, "%d.%03d", p.info.version_major, p.info.version_minor);
    const int sid_version = sid(ver);
    const int sid_notice  = p.info.copyright.empty() ? -1 : sid(p.info.copyright);
    const int sid_full    = sid(p.family + " " + p.style);
    const int sid_family  = sid(p.family);
    auto top_dict = [&](std::int32_t charset_off, std::int32_t cs_off, std::int32_t priv_off) {
        Bytes d;
        dnum(d, sid_version); dop(d, 0);
        if (sid_notice >= 0) { dnum(d, sid_notice); dop(d, 1); }
        dnum(d, sid_full);    dop(d, 2);
        dnum(d, sid_family);  dop(d, 3);
        if (p.info.italic_angle != 0) { dreal(d, p.info.italic_angle); dop(d, 1202); }   // M16a: ItalicAngle
        if (p.info.units_per_em != 1000 && p.info.units_per_em > 0) {
            const double s = 1.0 / p.info.units_per_em;
            dreal(d, s); dnum(d, 0); dnum(d, 0); dreal(d, s); dnum(d, 0); dnum(d, 0);
            dop(d, 1207);   // FontMatrix
        }
        dnum(d, p.xmin); dnum(d, p.ymin); dnum(d, p.xmax); dnum(d, p.ymax); dop(d, 5);
        dnum5(d, charset_off); dop(d, 15);
        dnum5(d, cs_off);      dop(d, 17);
        dnum5(d, static_cast<std::int32_t>(priv.size())); dnum5(d, priv_off); dop(d, 18);
        return d;
    };

    const Bytes name_index = index({Bytes(p.ps_name.begin(), p.ps_name.end())});
    const Bytes str_index  = index(custom);
    const Bytes gsubrs     = index({});
    const std::size_t top_len = index({top_dict(0, 0, 0)}).size();
    const std::size_t charset_off = 4 + name_index.size() + top_len + str_index.size() + gsubrs.size();
    const std::size_t cs_off      = charset_off + charset.size();
    const std::size_t priv_off    = cs_off + charstrings.size();
    const Bytes top_index = index({top_dict(static_cast<std::int32_t>(charset_off), static_cast<std::int32_t>(cs_off),
                                            static_cast<std::int32_t>(priv_off))});

    Writer w;
    w.u8(1); w.u8(0); w.u8(4); w.u8(4);   // major, minor, hdrSize, offSize
    w.bytes(name_index);
    w.bytes(top_index);
    w.bytes(str_index);
    w.bytes(gsubrs);
    w.bytes(charset.b);
    w.bytes(charstrings);
    w.bytes(priv);
    return w.b;
}

}  // namespace granjon::otf
