// otf::Ttf -- M10a: the font compiled to TrueType (glyf outlines).
//
// Shares everything but the outlines with the OTF: same prepare (components
// flattened, production names, cmap), same OS/2 name cmap hhea hmtx GPOS.
// Different: quadratic outlines (cu2qu, 1/1000 em tolerance) in TrueType's
// direction (every contour reversed -- consistent, so the nonzero fill is
// unchanged), glyf + loca, maxp 1.0, post 2 (names in the font, since there
// is no CFF to carry them), gasp (unhinted: smooth at every size).
// Overlaps stay as drawn; each glyph sets OVERLAP_SIMPLE so rasterizers that
// care (Apple's) fill them right. Removal comes with M10b.
#include "export/OtfData.hpp"
#include "export/OtfParts.hpp"
#include "export/Quad.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace granjon::otf {

std::vector<TtOutline> quad_glyphs(Prepared& p, double tolerance, int* points) {
    std::vector<TtOutline> out;
    out.reserve(p.glyphs.size());
    int total = 0;
    bool first_font = true;
    for (auto& g : p.glyphs) {
        TtOutline tt;
        for (const auto& c : g.contours) {
            if (c.nodes.empty()) continue;
            const QContour q = quad_contour(c, tolerance);
            std::vector<QPointI> pts;
            for (const auto& pt : q)
                pts.push_back({static_cast<int>(std::lround(pt.x)), static_cast<int>(std::lround(pt.y)), pt.on});
            if (pts.size() < 2) continue;   // a lone point draws nothing
            total += static_cast<int>(pts.size());
            tt.push_back(std::move(pts));
        }
        g.empty = tt.empty();
        g.xmin = g.ymin = g.xmax = g.ymax = 0;
        bool first = true;
        for (const auto& c : tt)
            for (const auto& pt : c) {
                if (first) { g.xmin = g.xmax = pt.x; g.ymin = g.ymax = pt.y; first = false; }
                g.xmin = std::min(g.xmin, pt.x); g.xmax = std::max(g.xmax, pt.x);
                g.ymin = std::min(g.ymin, pt.y); g.ymax = std::max(g.ymax, pt.y);
            }
        if (!g.empty) {
            if (first_font) { p.xmin = g.xmin; p.ymin = g.ymin; p.xmax = g.xmax; p.ymax = g.ymax; first_font = false; }
            p.xmin = std::min(p.xmin, g.xmin); p.ymin = std::min(p.ymin, g.ymin);
            p.xmax = std::max(p.xmax, g.xmax); p.ymax = std::max(p.ymax, g.ymax);
        }
        out.push_back(std::move(tt));
    }
    if (first_font) p.xmin = p.ymin = p.xmax = p.ymax = 0;
    if (points) *points = total;
    return out;
}

namespace {

Bytes glyph_bytes(const TtOutline& tt) {
    if (tt.empty()) return {};
    int xmin = tt[0][0].x, xmax = xmin, ymin = tt[0][0].y, ymax = ymin;
    for (const auto& c : tt)
        for (const auto& p : c) {
            xmin = std::min(xmin, p.x); xmax = std::max(xmax, p.x);
            ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y);
        }
    Writer w;
    w.i16(static_cast<int>(tt.size()));
    w.i16(xmin); w.i16(ymin); w.i16(xmax); w.i16(ymax);
    int end = -1;
    for (const auto& c : tt) { end += static_cast<int>(c.size()); w.u16(static_cast<unsigned>(end)); }
    w.u16(0);   // no instructions

    std::vector<std::uint8_t> flags;
    Writer xs, ys;
    int px = 0, py = 0;
    bool first = true;
    for (const auto& c : tt)
        for (const auto& p : c) {
            const int dx = p.x - px, dy = p.y - py;
            px = p.x; py = p.y;
            std::uint8_t f = p.on ? 0x01 : 0x00;
            if (first) { f |= 0x40; first = false; }   // OVERLAP_SIMPLE
            if (dx == 0) f |= 0x10;
            else if (std::abs(dx) <= 255) { f |= 0x02; if (dx > 0) f |= 0x10; xs.u8(static_cast<unsigned>(std::abs(dx))); }
            else xs.i16(dx);
            if (dy == 0) f |= 0x20;
            else if (std::abs(dy) <= 255) { f |= 0x04; if (dy > 0) f |= 0x20; ys.u8(static_cast<unsigned>(std::abs(dy))); }
            else ys.i16(dy);
            flags.push_back(f);
        }
    for (std::size_t i = 0; i < flags.size();) {   // runs -> REPEAT
        std::size_t j = i + 1;
        while (j < flags.size() && flags[j] == flags[i] && j - i - 1 < 255) ++j;
        const std::size_t extra = j - i - 1;
        if (extra >= 1) { w.u8(flags[i] | 0x08); w.u8(static_cast<unsigned>(extra)); }
        else w.u8(flags[i]);
        i = j;
    }
    w.bytes(xs.b);
    w.bytes(ys.b);
    return w.b;
}

}  // namespace

GlyfLoca build_glyf(const std::vector<TtOutline>& glyphs) {
    GlyfLoca r;
    Writer glyf;
    std::vector<std::uint32_t> offs;
    for (const auto& tt : glyphs) {
        offs.push_back(static_cast<std::uint32_t>(glyf.size()));
        glyf.bytes(glyph_bytes(tt));
        while (glyf.size() % 2) glyf.u8(0);
        int pts = 0;
        for (const auto& c : tt) pts += static_cast<int>(c.size());
        r.max_points = std::max(r.max_points, pts);
        r.max_contours = std::max(r.max_contours, static_cast<int>(tt.size()));
    }
    offs.push_back(static_cast<std::uint32_t>(glyf.size()));
    r.loca_format = glyf.size() / 2 <= 0xFFFF ? 0 : 1;
    Writer loca;
    for (auto o : offs) {
        if (r.loca_format == 0) loca.u16(o / 2);
        else loca.u32(o);
    }
    r.glyf = std::move(glyf.b);
    r.loca = std::move(loca.b);
    return r;
}

Bytes build_maxp_tt(const Prepared& p, const GlyfLoca& g) {
    Writer w;
    w.u32(0x00010000);
    w.u16(static_cast<unsigned>(p.glyphs.size()));
    w.u16(static_cast<unsigned>(g.max_points));
    w.u16(static_cast<unsigned>(g.max_contours));
    w.u16(0); w.u16(0);   // composites: none (decomposed)
    w.u16(2);             // maxZones
    w.zeros(2 * 8);       // twilight, storage, fdefs, idefs, stack, instructions, components, depth: none
    return w.b;
}

Bytes build_post2(const Prepared& p) {
    static const std::unordered_map<std::string, int> mac = [] {
        std::unordered_map<std::string, int> m;
        for (int i = 0; i < kMacStandardCount; ++i) m.emplace(kMacStandardNames[i], i);
        return m;
    }();
    const Bytes head = build_post(p);   // format 3's 32 bytes, then patched to 2
    Writer w;
    w.bytes(head);
    w.put_u32(0, 0x00020000);
    w.u16(static_cast<unsigned>(p.glyphs.size()));
    Writer names;
    int extra = 0;
    for (const auto& g : p.glyphs) {
        if (auto it = mac.find(g.name); it != mac.end()) { w.u16(static_cast<unsigned>(it->second)); continue; }
        w.u16(static_cast<unsigned>(kMacStandardCount + extra++));
        const std::string n = g.name.substr(0, 255);
        names.u8(static_cast<unsigned>(n.size()));
        for (char c : n) names.u8(static_cast<unsigned char>(c));
    }
    w.bytes(names.b);
    return w.b;
}

Bytes build_gasp() {
    Writer w;
    w.u16(1);        // version
    w.u16(1);        // one range
    w.u16(0xFFFF);   // every size
    w.u16(0x000F);   // gridfit, grayscale, symmetric gridfit + smoothing (unhinted: harmless, smooth)
    return w.b;
}

Bytes compile_ttf(const model::Font& f, const OtfOptions& opt, OtfReport* out) {
    const auto t0 = std::chrono::steady_clock::now();
    OtfReport rep;
    Prepared p = prepare(f, opt, rep);
    const double tol = p.info.units_per_em / 1000.0;
    int points = 0;
    const auto quads = quad_glyphs(p, tol, &points);
    const GlyfLoca gl = build_glyf(quads);

    std::map<std::string, Bytes> tables;
    const Bytes gpos = build_gpos(f.kerning(), p, rep);
    if (!gpos.empty()) tables["GPOS"] = gpos;
    if (const Bytes gdef = build_gdef(p); !gdef.empty()) tables["GDEF"] = gdef;
    int lig_context = 1;
    if (const Bytes gsub = build_gsub(f.features().text, p, rep, &lig_context); !gsub.empty()) tables["GSUB"] = gsub;
    const int max_context = std::max(gpos.empty() ? 1 : 2, lig_context);
    tables["head"] = build_head(p, gl.loca_format);
    tables["hhea"] = build_hhea(p);
    tables["maxp"] = build_maxp_tt(p, gl);
    tables["OS/2"] = build_os2(p, max_context);
    tables["name"] = build_name(p);
    tables["cmap"] = build_cmap(p);
    tables["post"] = build_post2(p);
    tables["hmtx"] = build_hmtx(p);
    tables["glyf"] = gl.glyf;
    tables["loca"] = gl.loca;
    tables["gasp"] = build_gasp();
    Bytes font = assemble_sfnt(0x00010000, tables);

    rep.glyphs  = static_cast<int>(p.glyphs.size());
    rep.encoded = static_cast<int>(p.cmap.size());
    rep.tt_points = points;
    rep.bytes   = font.size();
    rep.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (out) *out = std::move(rep);
    return font;
}

bool export_ttf(const model::Font& f, const std::filesystem::path& to, const OtfOptions& opt, OtfReport* rep,
                std::string* why) {
    return write_font_file(compile_ttf(f, opt, rep), to, why);
}

}  // namespace granjon::otf
