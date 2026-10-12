// otf::Otf -- the pre-export steps and the whole-font compile (M9).
#include "export/OtfParts.hpp"
#include "geom/Edit.hpp"
#include "geom/Overlap.hpp"
#include "model/Bounds.hpp"
#include "unicode/Agl.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>

namespace granjon::otf {

namespace {

std::string strip_ps(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (c >= 33 && c <= 126 && !std::string("[](){}<>/%").contains(static_cast<char>(c)))
            out.push_back(static_cast<char>(c));
    return out;
}

// The box .notdef supplied when the font has none: half an em wide, cap high.
OutGlyph made_notdef(const model::Metrics& m, int upm) {
    OutGlyph g;
    g.name = ".notdef";
    g.advance = upm / 2;
    const double t = std::max(10, upm / 20);
    const double x0 = t, x1 = g.advance - t, y0 = 0, y1 = std::max(m.cap_height, upm / 2);
    model::Contour outer, inner;
    for (auto [x, y] : {std::pair{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}) outer.nodes.push_back(model::Node::corner(x, y));
    for (auto [x, y] : {std::pair{x0 + t, y0 + t}, {x0 + t, y1 - t}, {x1 - t, y1 - t}, {x1 - t, y0 + t}})
        inner.nodes.push_back(model::Node::corner(x, y));
    g.contours = {outer, inner};
    return g;
}

}  // namespace

std::string postscript_name(const std::string& family, const std::string& style) {
    std::string n = strip_ps(family);
    if (n.empty()) n = "Untitled";
    const std::string s = strip_ps(style);
    if (!s.empty()) n += "-" + s;
    if (n.size() > 63) n.resize(63);
    return n;
}

std::string default_file_name(const model::FontInfo& info, const std::string& ext) {
    return postscript_name(info.family, info.style) + ext;
}

std::string production_name(const model::Glyph& g) {
    if (g.unicodes.empty() || g.name == ".notdef") return g.name;
    const auto cps = unicode::codepoints_for_name(g.name);
    if (cps.size() == 1 && cps[0] == g.unicodes[0]) return g.name;
    char buf[16];
    const auto c = static_cast<unsigned>(g.unicodes[0]);
    std::snprintf(buf, sizeof buf, c <= 0xFFFF ? "uni%04X" : "u%05X", c);
    return buf;
}

OutGlyph prepare_glyph(const model::Font& f, const model::Glyph& src, bool overlaps, bool* changed,
                       bool* fell_back) {
    OutGlyph o;
    o.source   = src.name;
    o.name     = production_name(src);
    o.unicodes = src.unicodes;
    o.advance  = static_cast<int>(std::clamp(std::lround(src.advance), 0L, 65535L));
    model::Glyph flat;
    flat.contours = src.contours;
    for (const auto& k : src.components)
        for (auto& c : geom::component_contours(f, k)) flat.contours.push_back(std::move(c));
    if (changed) *changed = false;
    if (fell_back) *fell_back = false;
    if (overlaps && flat.contours.size() > 0) {
        auto r = geom::remove_overlaps(flat.contours);
        if (r.changed) flat.contours = std::move(r.contours);
        if (changed) *changed = r.changed;
        if (fell_back) *fell_back = r.fallback;
    }
    flat.snap();
    if (auto b = model::bounds(f, flat)) {
        o.empty = false;
        o.xmin = static_cast<int>(std::floor(b->x0));
        o.ymin = static_cast<int>(std::floor(b->y0));
        o.xmax = static_cast<int>(std::ceil(b->x1));
        o.ymax = static_cast<int>(std::ceil(b->y1));
    }
    o.contours = std::move(flat.contours);
    o.anchors = glyph_anchors(f, src);
    return o;
}

std::vector<model::Anchor> glyph_anchors(const model::Font& f, const model::Glyph& g, int depth) {
    if (!g.anchors.empty() || g.components.empty() || depth > 8) return g.anchors;
    auto moved = [](const model::Anchor& a, const model::Component& k) {
        return model::Anchor{a.name, k.xx * a.x + k.yx * a.y + k.dx, k.xy * a.x + k.yy * a.y + k.dy};
    };
    std::vector<model::Anchor> out;
    const model::Component& first = g.components.front();
    if (const model::Glyph* b = f.glyph(first.base))
        for (const auto& a : glyph_anchors(f, *b, depth + 1))
            if (!a.name.empty() && a.name[0] != '_') out.push_back(moved(a, first));
    for (std::size_t i = 1; i < g.components.size(); ++i) {
        const model::Glyph* m = f.glyph(g.components[i].base);
        if (!m) continue;
        const auto ma = glyph_anchors(f, *m, depth + 1);
        for (const auto& a : ma) {
            if (a.name.empty() || a.name[0] == '_') continue;
            const bool rides = std::any_of(ma.begin(), ma.end(), [&](const model::Anchor& u) { return u.name == "_" + a.name; });
            if (!rides) continue;
            for (auto& o : out) if (o.name == a.name) o = moved(a, g.components[i]);
        }
    }
    return out;
}

Prepared prepare(const model::Font& f, const OtfOptions& opt, OtfReport& rep) {
    Prepared p;
    p.info    = f.info();
    p.metrics = f.metrics();
    if (p.info.units_per_em < 16 || p.info.units_per_em > 16384) {
        rep.notes.push_back("Units per em " + std::to_string(p.info.units_per_em) + " is out of range; written as 1000.");
        p.info.units_per_em = 1000;
    }
    p.family  = opt.family_override.empty() ? (p.info.family.empty() ? "Untitled" : p.info.family) : opt.family_override;
    p.style   = p.info.style.empty() ? "Regular" : p.info.style;
    p.ps_name = postscript_name(p.family, p.style);

    if (opt.time) p.time = *opt.time;
    else if (const char* e = std::getenv("SOURCE_DATE_EPOCH"); e && *e) p.time = std::atoll(e);
    else p.time = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count();

    std::vector<std::string> order = f.glyph_order();
    // M14c: parts (public.skipExportGlyphs) stay out of the file; a glyph
    // that uses one as a component is decomposed like any other.
    {
        const auto n0 = order.size();
        std::erase_if(order, [&](const std::string& n) { return f.glyph(n)->skip_export; });
        if (const auto parts = n0 - order.size())
            rep.notes.push_back(std::to_string(parts) + " part(s) left out (not exported).");
    }
    if (f.glyph(".notdef")) {
        order.erase(std::remove(order.begin(), order.end(), ".notdef"), order.end());
        order.insert(order.begin(), ".notdef");
    } else {
        p.glyphs.push_back(made_notdef(p.metrics, p.info.units_per_em));
        rep.made_notdef = true;
        rep.notes.push_back("The font has no .notdef; a box was supplied.");
    }
    if (order.size() + p.glyphs.size() > 65535) {
        rep.notes.push_back("More than 65,535 glyphs; the rest were left out.");
        order.resize(65535 - p.glyphs.size());
    }

    std::set<std::string> used;
    std::vector<std::string> fell_back;
    for (const auto& g : p.glyphs) used.insert(g.name);
    for (const auto& name : order) {
        const model::Glyph& src = *f.glyph(name);
        OutGlyph o;
        o.source = name;
        o.name = production_name(src);
        if (o.name != name && used.count(o.name)) o.name = name;   // two glyphs want one uniXXXX: keep the designer's
        if (used.count(o.name)) {                                 // still taken: suffix
            int k = 1;
            while (used.count(name + "." + std::to_string(k))) ++k;
            o.name = name + "." + std::to_string(k);
        }
        if (o.name != name) ++rep.renamed;
        used.insert(o.name);
        bool ov = false, fb = false;
        OutGlyph shaped = prepare_glyph(f, src, opt.remove_overlaps, &ov, &fb);
        if (ov) ++rep.overlaps_removed;
        if (fb) { if (rep.overlap_fallbacks++ < 5) fell_back.push_back(name); }
        o.unicodes = std::move(shaped.unicodes);
        o.advance  = shaped.advance;
        o.contours = std::move(shaped.contours);
        o.empty = shaped.empty;
        o.xmin = shaped.xmin; o.ymin = shaped.ymin; o.xmax = shaped.xmax; o.ymax = shaped.ymax;
        o.anchors = std::move(shaped.anchors);
        if (!src.components.empty()) ++rep.decomposed;
        for (const auto& c : o.contours)
            if (!c.closed && c.nodes.size() > 1) ++rep.open_contours;
        p.glyphs.push_back(std::move(o));
    }
    if (rep.made_notdef) {   // its bbox, as for the rest
        auto& g = p.glyphs.front();
        model::Glyph flat;
        flat.contours = g.contours;
        if (auto b = model::bounds(f, flat)) {
            g.empty = false;
            g.xmin = static_cast<int>(std::floor(b->x0)); g.ymin = static_cast<int>(std::floor(b->y0));
            g.xmax = static_cast<int>(std::ceil(b->x1));  g.ymax = static_cast<int>(std::ceil(b->y1));
        }
    }
    if (rep.open_contours)
        rep.notes.push_back(std::to_string(rep.open_contours) + " open contour(s) were written closed (CFF closes every path).");
    if (rep.overlap_fallbacks) {
        std::string l;
        for (const auto& n : fell_back) l += (l.empty() ? "" : ", ") + n;
        if (rep.overlap_fallbacks > 5) l += ", ...";
        rep.notes.push_back(std::to_string(rep.overlap_fallbacks) +
                            " glyph(s) kept their overlaps (the union failed its check): " + l + ".");
    }
    if (rep.renamed)
        rep.notes.push_back(std::to_string(rep.renamed) + " glyph(s) carry a production name (uniXXXX) in the file.");

    bool first = true;
    for (std::size_t i = 0; i < p.glyphs.size(); ++i) {
        const auto& g = p.glyphs[i];
        if (!g.source.empty()) p.gid[g.source] = static_cast<int>(i);
        for (char32_t cp : g.unicodes)
            if (cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) p.cmap.emplace(cp, static_cast<int>(i));
        if (g.empty) continue;
        if (first) { p.xmin = g.xmin; p.ymin = g.ymin; p.xmax = g.xmax; p.ymax = g.ymax; first = false; }
        p.xmin = std::min(p.xmin, g.xmin); p.ymin = std::min(p.ymin, g.ymin);
        p.xmax = std::max(p.xmax, g.xmax); p.ymax = std::max(p.ymax, g.ymax);
    }
    return p;
}

Bytes compile_otf(const model::Font& f, const OtfOptions& opt, OtfReport* out) {
    const auto t0 = std::chrono::steady_clock::now();
    OtfReport rep;
    const Prepared p = prepare(f, opt, rep);
    std::map<std::string, Bytes> tables;
    const Bytes gpos = build_gpos(f.kerning(), p, rep);
    if (!gpos.empty()) tables["GPOS"] = gpos;
    if (const Bytes gdef = build_gdef(p); !gdef.empty()) tables["GDEF"] = gdef;
    int lig_context = 1;
    if (const Bytes gsub = build_gsub(f.features().text, p, rep, &lig_context); !gsub.empty()) tables["GSUB"] = gsub;
    const int max_context = std::max(gpos.empty() ? 1 : 2, lig_context);
    tables["head"] = build_head(p);
    tables["hhea"] = build_hhea(p);
    tables["maxp"] = build_maxp(p);
    tables["OS/2"] = build_os2(p, max_context);
    tables["name"] = build_name(p);
    tables["cmap"] = build_cmap(p);
    tables["post"] = build_post(p);
    tables["hmtx"] = build_hmtx(p);
    tables["CFF "] = build_cff(p);
    Bytes font = assemble_sfnt(0x4F54544F, tables);

    rep.glyphs  = static_cast<int>(p.glyphs.size());
    rep.encoded = static_cast<int>(p.cmap.size());
    rep.bytes   = font.size();
    rep.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (out) *out = std::move(rep);
    return font;
}

bool export_otf(const model::Font& f, const std::filesystem::path& to, const OtfOptions& opt, OtfReport* rep,
                std::string* why) {
    return write_font_file(compile_otf(f, opt, rep), to, why);
}

bool write_font_file(const Bytes& font, const std::filesystem::path& to, std::string* why) {
    std::filesystem::path tmp = to;
    tmp += ".exporting";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) {
            if (why) *why = "Can't write " + tmp.string() + ".";
            return false;
        }
        o.write(reinterpret_cast<const char*>(font.data()), static_cast<std::streamsize>(font.size()));
        if (!o) {
            if (why) *why = "Writing " + tmp.string() + " failed.";
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, to, ec);
    if (ec) {
        if (why) *why = "Can't replace " + to.string() + ": " + ec.message() + ".";
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

}  // namespace granjon::otf
