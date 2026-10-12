// reader::FontReader -- M11: the whole import. Reads everything into fresh
// records first; only a complete read replaces the open font (as load_ufo).
#include "export/OtfData.hpp"
#include "import/FontReader.hpp"
#include "import/ReaderParts.hpp"
#include "model/Bounds.hpp"
#include "unicode/Agl.hpp"
#include "unicode/UcdTable.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <unordered_map>

namespace granjon::reader {

namespace {

std::string utf16be(Span s) {
    std::string o;
    for (std::size_t i = 0; i + 1 < s.n; i += 2) {
        char32_t c = s.u16(i);
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < s.n) {
            const char32_t lo = s.u16(i + 2);
            if (lo >= 0xDC00 && lo < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
        }
        o += unicode::utf8(c);
    }
    return o;
}

// name ID -> string: Windows (3, 1 / 10) English first, then any Windows, then Mac Roman.
std::map<int, std::string> read_names(Span name) {
    std::map<int, std::string> out;
    std::map<int, int> rank;
    if (name.empty()) return out;
    const unsigned n = name.u16(2), store = name.u16(4);
    for (unsigned i = 0; i < n; ++i) {
        const std::size_t r = 6 + 12 * static_cast<std::size_t>(i);
        const unsigned pid = name.u16(r), eid = name.u16(r + 2), lid = name.u16(r + 4), nid = name.u16(r + 6);
        const unsigned len = name.u16(r + 8), off = name.u16(r + 10);
        int score = 0;
        std::string v;
        const Span s = name.sub(store + off, len);
        if (pid == 3 && (eid == 1 || eid == 10)) { v = utf16be(s); score = lid == 0x409 ? 3 : 2; }
        else if (pid == 0) { v = utf16be(s); score = 2; }
        else if (pid == 1 && eid == 0) { for (std::size_t k = 0; k < s.n; ++k) v += unicode::utf8(s.u8(k)); score = 1; }
        else continue;
        const int id = static_cast<int>(nid);
        if (!v.empty() && score > rank[id]) { out[id] = v; rank[id] = score; }
    }
    return out;
}

// Code point -> GID, from the best Unicode cmap subtable.
std::map<char32_t, int> read_cmap(Span cmap, std::vector<std::string>& notes) {
    std::map<char32_t, int> out;
    if (cmap.empty()) { notes.push_back("No cmap: no glyph has a code point."); return out; }
    const unsigned n = cmap.u16(2);
    std::size_t best = 0;
    int best_rank = -1;
    for (unsigned i = 0; i < n; ++i) {
        const unsigned pid = cmap.u16(4 + 8 * i), eid = cmap.u16(6 + 8 * i);
        const std::uint32_t off = cmap.u32(8 + 8 * i);
        const unsigned fmt = cmap.u16(off);
        int rank = -1;
        if ((pid == 3 && eid == 10) || (pid == 0 && (eid == 4 || eid == 6))) rank = fmt == 12 ? 5 : 2;
        else if ((pid == 3 && eid == 1) || pid == 0) rank = fmt == 4 ? 4 : fmt == 12 ? 5 : 1;
        else if (pid == 3 && eid == 0) rank = 0;   // symbol
        if (rank > best_rank && (fmt == 0 || fmt == 4 || fmt == 6 || fmt == 12)) { best_rank = rank; best = off; }
    }
    if (best_rank < 0) { notes.push_back("No Unicode cmap this reader knows: no code points."); return out; }
    const Span t = cmap.from(best);
    const unsigned fmt = t.u16(0);
    if (fmt == 4) {
        const unsigned segs = t.u16(6) / 2;
        const std::size_t ends = 14, starts = ends + 2 * segs + 2, deltas = starts + 2 * segs, ranges = deltas + 2 * segs;
        for (unsigned s = 0; s < segs; ++s) {
            const unsigned e = t.u16(ends + 2 * s), st = t.u16(starts + 2 * s);
            const unsigned delta = t.u16(deltas + 2 * s), ro = t.u16(ranges + 2 * s);
            for (unsigned c = st; c <= e && c != 0xFFFF; ++c) {
                unsigned g;
                if (!ro) g = (c + delta) & 0xFFFF;
                else {
                    const unsigned gi = t.u16(ranges + 2 * s + ro + 2 * (c - st));
                    g = gi ? (gi + delta) & 0xFFFF : 0;
                }
                if (g) out.emplace(c, static_cast<int>(g));
            }
        }
    } else if (fmt == 12) {
        const std::uint32_t ng = t.u32(12);
        for (std::uint32_t i = 0; i < ng; ++i) {
            const std::uint32_t a = t.u32(16 + 12 * i), b = t.u32(20 + 12 * i), g = t.u32(24 + 12 * i);
            if (b < a || b - a > 0x10FFFF) throw BadFont("a cmap group runs backwards");
            for (std::uint32_t c = a; c <= b; ++c) if (g + (c - a)) out.emplace(c, static_cast<int>(g + (c - a)));
        }
    } else if (fmt == 6) {
        const unsigned first = t.u16(6), cnt = t.u16(8);
        for (unsigned i = 0; i < cnt; ++i) if (const unsigned g = t.u16(10 + 2 * i)) out.emplace(first + i, static_cast<int>(g));
    } else if (fmt == 0) {
        for (unsigned c = 0; c < 256; ++c) if (const unsigned g = t.u8(6 + c)) out.emplace(c, static_cast<int>(g));
    }
    return out;
}

std::vector<std::string> post_names(Span post, int count) {
    std::vector<std::string> out(static_cast<std::size_t>(count));
    if (post.empty()) return out;
    const std::uint32_t ver = post.u32(0);
    if (ver == 0x00010000) {
        for (int g = 0; g < count && g < otf::kMacStandardCount; ++g) out[static_cast<std::size_t>(g)] = otf::kMacStandardNames[g];
    } else if (ver == 0x00020000) {
        const unsigned n = post.u16(32);
        std::vector<std::string> extra;
        std::size_t at = 34 + 2 * static_cast<std::size_t>(n);
        while (at < post.n) {
            const unsigned l = post.u8(at);
            extra.emplace_back(reinterpret_cast<const char*>(post.sub(at + 1, l).p), l);
            at += 1 + l;
        }
        for (unsigned g = 0; g < n && static_cast<int>(g) < count; ++g) {
            const unsigned ix = post.u16(34 + 2 * g);
            if (ix < static_cast<unsigned>(otf::kMacStandardCount)) out[g] = otf::kMacStandardNames[ix];
            else if (ix - otf::kMacStandardCount < extra.size()) out[g] = extra[ix - otf::kMacStandardCount];
        }
    }
    return out;
}

}  // namespace

bool import_font(const std::vector<std::uint8_t>& data, model::Font& into, ImportReport* out, std::string* why) {
    const auto t0 = std::chrono::steady_clock::now();
    ImportReport rep;
    try {
        const Span font(data);
        const Sfnt sf = read_sfnt(font);
        const Span head = sf.get("head"), hhea = sf.get("hhea"), maxp = sf.get("maxp"), hmtx = sf.get("hmtx");
        if (head.empty() || hhea.empty() || maxp.empty() || hmtx.empty()) throw BadFont("a required table is missing (head / hhea / maxp / hmtx)");
        if (!sf.get("CFF2").empty() || !sf.get("fvar").empty())
            rep.notes.push_back("A variable font: its default instance was read (variations are v3).");
        const int count = static_cast<int>(maxp.u16(4));
        if (count == 0) throw BadFont("the font has no glyphs");

        // outlines
        std::vector<ReadGlyph> outlines(static_cast<std::size_t>(count));
        std::vector<std::string> names;
        const Span cff = sf.get("CFF ");
        if (!cff.empty()) {
            rep.format = "CFF";
            CffResult r = read_cff(cff);
            for (std::size_t g = 0; g < outlines.size() && g < r.glyphs.size(); ++g) {
                for (auto& c : r.glyphs[g]) {
                    c.closed = true;
                    if (c.nodes.size() < 2 && !(c.nodes.size() == 1 && (c.nodes[0].has_in() || c.nodes[0].has_out()))) continue;
                    infer_smooth(c);
                    outlines[g].contours.push_back(std::move(c));
                }
            }
            names = r.names;
            rep.notes.insert(rep.notes.end(), r.notes.begin(), r.notes.end());
        } else if (!sf.get("glyf").empty()) {
            rep.format = "TrueType";
            outlines = read_glyf(sf.get("glyf"), sf.get("loca"), head.i16(50), count, rep.notes);
        } else if (!sf.get("CFF2").empty()) {
            throw BadFont("CFF2 outlines (variable OpenType) -- not read yet");
        } else {
            throw BadFont("no outlines (no CFF or glyf table)");
        }
        names.resize(static_cast<std::size_t>(count));

        // names: CFF charset / post, else from the cmap, else glyphNNNNN; unique and valid
        const auto cmap = read_cmap(sf.get("cmap"), rep.notes);
        std::vector<std::vector<char32_t>> cps(static_cast<std::size_t>(count));
        for (const auto& [cp, g] : cmap) if (g < count) cps[static_cast<std::size_t>(g)].push_back(cp);
        if (rep.format == "TrueType") {
            const auto pn = post_names(sf.get("post"), count);
            for (int g = 0; g < count; ++g) names[static_cast<std::size_t>(g)] = pn[static_cast<std::size_t>(g)];
        }
        std::set<std::string> used;
        int made = 0;
        for (int g = 0; g < count; ++g) {
            std::string& n = names[static_cast<std::size_t>(g)];
            if (g == 0) n = ".notdef";
            if (!model::valid_glyph_name(n) || used.count(n)) {
                const auto& c = cps[static_cast<std::size_t>(g)];
                std::string base = !c.empty() ? unicode::nice_name(c.front()) : "";
                if (base.empty() || !model::valid_glyph_name(base)) {
                    char buf[16];
                    std::snprintf(buf, sizeof buf, "glyph%05d", g);
                    base = buf;
                }
                std::string m = base;
                for (int k = 1; used.count(m); ++k) m = base + "." + std::to_string(k);
                n = m;
                ++made;
            }
            used.insert(n);
        }
        if (made) rep.notes.push_back(std::to_string(made) + " glyph name(s) were made up (missing, invalid or repeated in the file).");

        // advances
        const unsigned nh = hhea.u16(34);
        std::vector<int> adv(static_cast<std::size_t>(count), 0);
        for (int g = 0; g < count; ++g) {
            const unsigned k = std::min<unsigned>(static_cast<unsigned>(g), nh ? nh - 1 : 0);
            adv[static_cast<std::size_t>(g)] = static_cast<int>(hmtx.u16(4 * k));
        }

        // info + metrics
        const auto nm = read_names(sf.get("name"));
        auto pick = [&](std::initializer_list<int> ids) {
            for (int id : ids) if (auto it = nm.find(id); it != nm.end() && !it->second.empty()) return it->second;
            return std::string{};
        };
        model::FontInfo info;
        info.family = pick({16, 1});
        if (info.family.empty()) info.family = "Untitled";
        info.style = pick({17, 2});
        if (info.style.empty()) info.style = "Regular";
        info.copyright = pick({0});
        info.license = pick({13});
        info.trademark = pick({7});
        info.manufacturer = pick({8});
        info.designer = pick({9});
        info.description = pick({10});
        info.manufacturer_url = pick({11});
        info.designer_url = pick({12});
        if (const Span post = sf.get("post"); post.n >= 8) info.italic_angle = std::round(post.i32(4) / 65536.0 * 100) / 100;
        info.units_per_em = static_cast<int>(head.u16(18));
        if (info.units_per_em < 16 || info.units_per_em > 16384) throw BadFont("units per em out of range");
        const double rev = head.i32(4) / 65536.0;
        info.version_major = static_cast<int>(std::floor(rev));
        info.version_minor = std::clamp(static_cast<int>(std::lround((rev - std::floor(rev)) * 1000)), 0, 999);
        const Span os2 = sf.get("OS/2");
        model::Metrics m;
        m.ascender = hhea.i16(4);
        m.descender = hhea.i16(6);
        int os2_xh = 0, os2_cap = 0;
        if (os2.n >= 78) {
            std::string v(reinterpret_cast<const char*>(os2.sub(58, 4).p), 4);
            while (!v.empty() && (v.back() == ' ' || v.back() == '\0')) v.pop_back();
            info.vendor = v;
            info.weight_class = os2.u16(4);
            info.width_class = std::clamp<int>(os2.u16(6), 1, 9);
            const int ta = os2.i16(68), td = os2.i16(70);
            if (ta > 0) { m.ascender = ta; m.descender = td; }
            if (os2.u16(0) >= 2 && os2.n >= 90) { os2_xh = os2.i16(86); os2_cap = os2.i16(88); }
        }

        // glyphs
        std::map<std::string, model::Glyph> glyphs;
        for (int g = 0; g < count; ++g) {
            auto& o = outlines[static_cast<std::size_t>(g)];
            model::Glyph gl;
            gl.name = names[static_cast<std::size_t>(g)];
            gl.unicodes = cps[static_cast<std::size_t>(g)];
            gl.advance = adv[static_cast<std::size_t>(g)];
            gl.contours = std::move(o.contours);
            for (std::size_t k = 0; k < o.components.size(); ++k) {
                const int bg = o.component_gids[k];
                if (bg < 0 || bg >= count || bg == g) continue;
                model::Component c = o.components[k];
                c.base = names[static_cast<std::size_t>(bg)];
                gl.components.push_back(c);
            }
            gl.order = g;
            gl.snap();
            gl.status = (gl.contours.empty() && gl.components.empty()) ? model::GlyphStatus::Empty : model::GlyphStatus::Drafted;
            rep.contours += static_cast<int>(gl.contours.size());
            rep.components += static_cast<int>(gl.components.size());
            glyphs.emplace(gl.name, std::move(gl));
        }

        // x-height / cap height / overshoot: OS/2, else measured
        model::Font probe;   // bounds() resolves components through a font
        {
            model::Font::Loading l(probe);
            for (const auto& [n, gl] : glyphs) probe.put(n, model::Record{gl});
        }
        auto top_of = [&](char32_t c) -> std::optional<double> {
            for (const auto& [n, gl] : glyphs)
                if (std::find(gl.unicodes.begin(), gl.unicodes.end(), c) != gl.unicodes.end())
                    if (auto b = model::bounds(probe, gl)) return b->y1;
            return std::nullopt;
        };
        m.x_height = os2_xh > 0 ? os2_xh : static_cast<int>(std::lround(top_of(U'x').value_or(info.units_per_em * 0.5)));
        m.cap_height = os2_cap > 0 ? os2_cap : static_cast<int>(std::lround(top_of(U'H').value_or(info.units_per_em * 0.7)));
        if (const auto o = top_of(U'O')) {
            const int os = static_cast<int>(std::lround(*o)) - m.cap_height;
            m.overshoot = os > 0 && os < info.units_per_em / 15 ? os : m.overshoot;
        }

        // kerning
        const model::Kerning kerning = read_kerning(sf.get("GPOS"), sf.get("kern"), names, rep.notes);
        rep.kern_pairs = static_cast<int>(kerning.pairs.size());
        rep.kern_groups = static_cast<int>(kerning.groups.size());
        model::Features features;
        features.text = read_gsub(sf.get("GSUB"), names, rep.notes);   // s10
        if (!sf.get("fpgm").empty() || !sf.get("prep").empty()) rep.notes.push_back("TrueType hints were dropped.");

        rep.glyphs = count;
        rep.encoded = static_cast<int>(cmap.size());

        // All read: now replace the open font.
        model::Font::Loading load(into);
        for (const auto& [id, r] : into.snapshot()) into.put(id, std::nullopt);
        into.put(model::Font::kInfo, model::Record{info});
        into.put(model::Font::kMetrics, model::Record{m});
        into.put(model::Font::kKerning, model::Record{kerning});
        into.put(model::Font::kMetricsKeys, model::Record{model::MetricsKeys{}});
        into.put(model::Font::kSkips, model::Record{model::CheckSkips{}});
        into.put(model::Font::kFeatures, model::Record{features});
        for (auto& [n, gl] : glyphs) into.put(n, model::Record{gl});
    } catch (const BadFont& e) {
        if (why) *why = std::string("Can't read this font: ") + e.what() + ".";
        return false;
    }
    rep.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (out) *out = std::move(rep);
    return true;
}

bool import_font_file(const std::filesystem::path& path, model::Font& into, ImportReport* rep, std::string* why) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { if (why) *why = "Can't open " + path.string() + "."; return false; }
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 12) { if (why) *why = path.filename().string() + " is not a font."; return false; }
    return import_font(data, into, rep, why);
}

}  // namespace granjon::reader
