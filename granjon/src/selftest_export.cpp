// selftest_export -- M9: the OTF compiler, headless.
//
// The writer is checked by reading its own output back with small, separate
// readers written here (a Type 2 charstring decoder, a cmap format 4 lookup, a
// GPOS pair evaluator), so a wrong offset or a wrong precedence shows up as a
// wrong answer, not just as "bytes were written". Outside validation
// (fontTools, OTS, FreeType, HarfBuzz) is scripts/otf_check.py.
#include "command/Verbs.hpp"
#include "export/Otf.hpp"
#include "export/OtfParts.hpp"
#include "geom/Edit.hpp"
#include "model/Spacing.hpp"

#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace otf   = granjon::otf;
namespace model = granjon::model;

namespace {

using Check = std::function<void(const std::string&, bool, const std::string&)>;

unsigned rd16(const otf::Bytes& b, std::size_t i) { return static_cast<unsigned>(b[i] << 8 | b[i + 1]); }
int      rds16(const otf::Bytes& b, std::size_t i) { return static_cast<std::int16_t>(rd16(b, i)); }
std::uint32_t rd32(const otf::Bytes& b, std::size_t i) { return static_cast<std::uint32_t>(rd16(b, i)) << 16 | rd16(b, i + 2); }

// ── CFF reading: just enough to find CharStrings and decode one ────────────
struct Index { std::vector<std::pair<std::size_t, std::size_t>> items; std::size_t end = 0; };
Index read_index(const otf::Bytes& b, std::size_t at) {
    Index ix;
    const unsigned n = rd16(b, at);
    if (n == 0) { ix.end = at + 2; return ix; }
    const unsigned os = b[at + 2];
    auto off = [&](unsigned i) {
        std::size_t v = 0;
        for (unsigned k = 0; k < os; ++k) v = v << 8 | b[at + 3 + i * os + k];
        return v;
    };
    const std::size_t data = at + 3 + (n + 1) * os - 1;
    for (unsigned i = 0; i < n; ++i) ix.items.push_back({data + off(i), data + off(i + 1)});
    ix.end = data + off(n);
    return ix;
}
// Top DICT operands by operator (ints only; reals skipped).
std::map<int, std::vector<long>> read_dict(const otf::Bytes& b, std::size_t a, std::size_t z) {
    std::map<int, std::vector<long>> d;
    std::vector<long> ops;
    for (std::size_t i = a; i < z;) {
        const unsigned c = b[i];
        if (c <= 21) {
            int op = static_cast<int>(c);
            ++i;
            if (c == 12) { op = 1200 + b[i]; ++i; }
            d[op] = ops;
            ops.clear();
        } else if (c == 28) { ops.push_back(rds16(b, i + 1)); i += 3; }
        else if (c == 29) { ops.push_back(static_cast<std::int32_t>(rd32(b, i + 1))); i += 5; }
        else if (c == 30) { ++i; while (i < z) { const unsigned x = b[i++]; if ((x & 0xF) == 0xF || (x >> 4) == 0xF) break; } ops.push_back(0); }
        else if (c >= 32 && c <= 246) { ops.push_back(static_cast<long>(c) - 139); ++i; }
        else if (c >= 247 && c <= 250) { ops.push_back((static_cast<long>(c) - 247) * 256 + b[i + 1] + 108); i += 2; }
        else if (c >= 251 && c <= 254) { ops.push_back(-(static_cast<long>(c) - 251) * 256 - b[i + 1] - 108); i += 2; }
        else ++i;
    }
    return d;
}
struct Decoded { std::vector<model::Contour> contours; int width_arg = 0; bool has_width = false; bool ok = true; };
Decoded decode_t2(const otf::Bytes& b, std::size_t a, std::size_t z) {
    Decoded out;
    std::vector<int> st;
    double x = 0, y = 0;
    bool first = true;
    auto close = [&] {
        if (out.contours.empty()) return;
        auto& n = out.contours.back().nodes;
        if (n.size() > 1 && n.back().x == n.front().x && n.back().y == n.front().y) {
            n.front().cx1 = n.back().cx1;
            n.front().cy1 = n.back().cy1;
            n.pop_back();
        }
    };
    for (std::size_t i = a; i < z;) {
        const unsigned c = b[i];
        if (c == 28) { st.push_back(rds16(b, i + 1)); i += 3; continue; }
        if (c >= 32 && c <= 246) { st.push_back(static_cast<int>(c) - 139); ++i; continue; }
        if (c >= 247 && c <= 250) { st.push_back((static_cast<int>(c) - 247) * 256 + b[i + 1] + 108); i += 2; continue; }
        if (c >= 251 && c <= 254) { st.push_back(-(static_cast<int>(c) - 251) * 256 - b[i + 1] - 108); i += 2; continue; }
        if (c == 255) { out.ok = false; return out; }
        ++i;
        if (first && (c == 21 || c == 14)) {
            const std::size_t want = c == 21 ? 2 : 0;
            if (st.size() > want) { out.has_width = true; out.width_arg = st.front(); st.erase(st.begin()); }
            first = false;
        }
        if (c == 21) {
            if (st.size() != 2) { out.ok = false; return out; }
            close();
            x += st[0]; y += st[1];
            out.contours.push_back({});
            out.contours.back().nodes.push_back(model::Node::corner(x, y));
        } else if (c == 5) {
            if (st.size() % 2 || out.contours.empty()) { out.ok = false; return out; }
            for (std::size_t k = 0; k < st.size(); k += 2) {
                x += st[k]; y += st[k + 1];
                out.contours.back().nodes.push_back(model::Node::corner(x, y));
            }
        } else if (c == 8) {
            if (st.size() % 6 || out.contours.empty()) { out.ok = false; return out; }
            for (std::size_t k = 0; k < st.size(); k += 6) {
                auto& prev = out.contours.back().nodes.back();
                const double x1 = x + st[k], y1 = y + st[k + 1];
                const double x2 = x1 + st[k + 2], y2 = y1 + st[k + 3];
                x = x2 + st[k + 4]; y = y2 + st[k + 5];
                prev.cx2 = x1; prev.cy2 = y1;
                model::Node n = model::Node::corner(x, y);
                n.cx1 = x2; n.cy1 = y2;
                out.contours.back().nodes.push_back(n);
            }
        } else if (c == 14) {
            close();
            return out;
        } else {
            out.ok = false;
            return out;
        }
        st.clear();
    }
    out.ok = false;   // no endchar
    return out;
}
bool same_drawing(const std::vector<model::Contour>& a, const std::vector<model::Contour>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].nodes.size() != b[i].nodes.size()) return false;
        for (std::size_t k = 0; k < a[i].nodes.size(); ++k) {
            const auto& p = a[i].nodes[k];
            const auto& q = b[i].nodes[k];
            if (p.x != q.x || p.y != q.y || p.cx1 != q.cx1 || p.cy1 != q.cy1 || p.cx2 != q.cx2 || p.cy2 != q.cy2)
                return false;
        }
    }
    return true;
}

// ── cmap: the (3,1) format 4 subtable ──────────────────────────────────────
int cmap4_lookup(const otf::Bytes& cmap, unsigned cp) {
    const unsigned n = rd16(cmap, 2);
    std::size_t at = 0;
    for (unsigned i = 0; i < n; ++i)
        if (rd16(cmap, 4 + 8 * i) == 3 && rd16(cmap, 6 + 8 * i) == 1) at = rd32(cmap, 8 + 8 * i);
    if (!at || rd16(cmap, at) != 4) return -1;
    const unsigned segs = rd16(cmap, at + 6) / 2;
    const std::size_t ends = at + 14, starts = ends + 2 * segs + 2, deltas = starts + 2 * segs, ranges = deltas + 2 * segs;
    for (unsigned s = 0; s < segs; ++s) {
        if (cp > rd16(cmap, ends + 2 * s)) continue;
        const unsigned start = rd16(cmap, starts + 2 * s);
        if (cp < start) return 0;
        const unsigned ro = rd16(cmap, ranges + 2 * s), delta = rd16(cmap, deltas + 2 * s);
        if (!ro) return static_cast<int>((cp + delta) & 0xFFFF);
        const unsigned g = rd16(cmap, ranges + 2 * s + ro + 2 * (cp - start));
        return g ? static_cast<int>((g + delta) & 0xFFFF) : 0;
    }
    return 0;
}

// ── GPOS: the kern lookup, evaluated for one pair ──────────────────────────
int coverage_index(const otf::Bytes& b, std::size_t at, unsigned g) {
    if (rd16(b, at) != 1) return -1;
    const unsigned n = rd16(b, at + 2);
    for (unsigned i = 0; i < n; ++i) if (rd16(b, at + 4 + 2 * i) == g) return static_cast<int>(i);
    return -1;
}
unsigned class_of(const otf::Bytes& b, std::size_t at, unsigned g) {
    if (rd16(b, at) != 2) return 0;
    const unsigned n = rd16(b, at + 2);
    for (unsigned i = 0; i < n; ++i) {
        const std::size_t r = at + 4 + 6 * i;
        if (g >= rd16(b, r) && g <= rd16(b, r + 2)) return rd16(b, r + 4);
    }
    return 0;
}
int gpos_kern(const otf::Bytes& gpos, unsigned g1, unsigned g2, int* subtables = nullptr, bool* ext = nullptr) {
    if (gpos.empty()) return 0;
    const std::size_t ll = rd16(gpos, 8);
    const std::size_t lk = ll + rd16(gpos, ll + 2);
    const unsigned type = rd16(gpos, lk), n = rd16(gpos, lk + 4);
    if (subtables) *subtables = static_cast<int>(n);
    if (ext) *ext = type == 9;
    for (unsigned s = 0; s < n; ++s) {
        std::size_t st = lk + rd16(gpos, lk + 6 + 2 * s);
        if (type == 9) st += rd32(gpos, st + 4);
        const unsigned fmt = rd16(gpos, st);
        const int ci = coverage_index(gpos, st + rd16(gpos, st + 2), g1);
        if (ci < 0) continue;
        if (fmt == 1) {
            const std::size_t ps = st + rd16(gpos, st + 10 + 2 * static_cast<unsigned>(ci));
            const unsigned m = rd16(gpos, ps);
            for (unsigned k = 0; k < m; ++k)
                if (rd16(gpos, ps + 2 + 4 * k) == g2) return rds16(gpos, ps + 4 + 4 * k);
        } else if (fmt == 2) {
            const unsigned c1 = class_of(gpos, st + rd16(gpos, st + 8), g1);
            const unsigned c2 = class_of(gpos, st + rd16(gpos, st + 10), g2);
            const unsigned c2n = rd16(gpos, st + 14);
            return rds16(gpos, st + 16 + 2 * (c1 * c2n + c2));
        }
    }
    return 0;
}

}  // namespace

void selftest_export(const Check& check_) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_(n, ok, d); };
    std::cout << "M9 export OTF\n";
    model::Font f;
    granjon::command::make_fixture(f);
    {   // something curved, something with a supplementary code point
        model::Font::Loading load(f);
        model::Glyph c;
        c.name = "c"; c.unicodes = {U'c'}; c.advance = 500; c.order = 100;
        model::Contour k;
        model::Node a = model::Node::corner(250, -10); a.cx1 = 120; a.cy1 = -10; a.cx2 = 380; a.cy2 = -10; a.type = model::Node::Type::Smooth;
        model::Node b = model::Node::corner(450, 250); b.cx1 = 450; b.cy1 = 100; b.cx2 = 450; b.cy2 = 400;
        model::Node t = model::Node::corner(250, 510); t.cx1 = 380; t.cy1 = 510; t.cx2 = 120; t.cy2 = 510;
        model::Node l = model::Node::corner(50, 250);  l.cx1 = 50; l.cy1 = 400; l.cx2 = 50; l.cy2 = 100;
        k.nodes = {a, b, t, l};
        c.contours = {k};
        f.put("c", model::Record{c});
        model::Glyph emoji;
        emoji.name = "u1F600"; emoji.unicodes = {U'\U0001F600'}; emoji.advance = 1000; emoji.order = 101;
        emoji.contours = {k};
        f.put("u1F600", model::Record{emoji});
        model::Glyph o = *f.glyph("o");   // two code points on one glyph
        o.unicodes = {U'o', U'\u043E'};
        f.put("o", model::Record{o});
        model::Glyph odd;   // a name that does not spell its code point
        odd.name = "myA"; odd.unicodes = {U'A'}; odd.advance = 600; odd.order = 102;
        f.put("myA", model::Record{odd});
    }

    otf::OtfReport rep;
    otf::OtfOptions opt;
    opt.time = 1760000000;
    const otf::Bytes font = otf::compile_otf(f, opt, &rep);
    check("otf: compiles, starts OTTO", font.size() > 12 && rd32(font, 0) == 0x4F54544F, std::to_string(font.size()) + " bytes");
    check("otf: deterministic for a fixed time", otf::compile_otf(f, opt) == font);
    check("otf: whole-file checksum is 0xB1B0AFBA", otf::checksum(font) == 0xB1B0AFBAu);
    bool all = true;
    std::string missing;
    for (const char* t : {"head", "hhea", "maxp", "OS/2", "name", "cmap", "post", "hmtx", "CFF ", "GPOS"})
        if (otf::table(font, t).empty()) { all = false; missing += t; missing += ' '; }
    check("otf: every table present", all, missing);
    {
        const unsigned n = rd16(font, 4);
        bool sorted = true, aligned = true, sums = true;
        for (unsigned i = 0; i < n; ++i) {
            const std::size_t r = 12 + 16 * i;
            if (i && std::string(font.begin() + static_cast<long>(r - 16), font.begin() + static_cast<long>(r - 12)) >=
                         std::string(font.begin() + static_cast<long>(r), font.begin() + static_cast<long>(r + 4)))
                sorted = false;
            if (rd32(font, r + 8) % 4) aligned = false;
            const std::string tag(font.begin() + static_cast<long>(r), font.begin() + static_cast<long>(r + 4));
            otf::Bytes t = otf::table(font, tag);
            if (tag == "head") { t[8] = t[9] = t[10] = t[11] = 0; }
            if (otf::checksum(t) != rd32(font, r + 4)) sums = false;
        }
        check("otf: directory sorted, tables 4-aligned, checksums right", sorted && aligned && sums);
    }

    const auto maxp = otf::table(font, "maxp");
    const auto order = f.glyph_order();
    check("otf: maxp counts every glyph, .notdef first", rd16(maxp, 4) == f.glyph_count() && rep.glyphs == static_cast<int>(f.glyph_count()),
          std::to_string(rd16(maxp, 4)));
    check("otf: head unitsPerEm and magic", rd16(otf::table(font, "head"), 18) == 1000 && rd32(otf::table(font, "head"), 12) == 0x5F0F3CF5);

    // Prepared, for GIDs and the flattened drawing.
    otf::OtfReport r2;
    const otf::Prepared p = otf::prepare(f, opt, r2);
    check("otf: .notdef is GID 0", p.glyphs.front().name == ".notdef");
    check("otf: a component is flattened (Odotbelow draws O)", rep.decomposed == 1 &&
          p.glyphs[static_cast<std::size_t>(p.gid.at("Odotbelow"))].contours == f.glyph("O")->contours);
    check("otf: production names (myA -> uni0041, c stays, u1F600 stays)",
          p.glyphs[static_cast<std::size_t>(p.gid.at("myA"))].name == "uni0041" &&
          p.glyphs[static_cast<std::size_t>(p.gid.at("c"))].name == "c" &&
          p.glyphs[static_cast<std::size_t>(p.gid.at("u1F600"))].name == "u1F600" && rep.renamed == 1);

    // CFF: decode every charstring back into contours.
    {
        const auto cff = otf::table(font, "CFF ");
        const Index name = read_index(cff, cff[2]);
        const Index top = read_index(cff, name.end);
        const auto td = read_dict(cff, top.items[0].first, top.items[0].second);
        const Index cs = read_index(cff, static_cast<std::size_t>(td.at(17).at(0)));
        const auto pv = td.at(18);
        const auto pd = read_dict(cff, static_cast<std::size_t>(pv[1]), static_cast<std::size_t>(pv[1] + pv[0]));
        const int dflt = static_cast<int>(pd.count(20) ? pd.at(20).at(0) : 0), nominal = static_cast<int>(pd.count(21) ? pd.at(21).at(0) : 0);
        check("otf: CFF name is the PostScript name",
              std::string(cff.begin() + static_cast<long>(name.items[0].first), cff.begin() + static_cast<long>(name.items[0].second)) == "Untitled-Regular");
        check("otf: CFF CharStrings count = glyphs", cs.items.size() == p.glyphs.size());
        bool draw = true, width = true, decoded = true;
        std::string bad;
        for (std::size_t i = 0; i < p.glyphs.size() && i < cs.items.size(); ++i) {
            const Decoded d = decode_t2(cff, cs.items[i].first, cs.items[i].second);
            if (!d.ok) { decoded = false; bad = p.glyphs[i].name; continue; }
            const int w = d.has_width ? nominal + d.width_arg : dflt;
            if (w != p.glyphs[i].advance) { width = false; bad = p.glyphs[i].name; }
            if (!same_drawing(d.contours, p.glyphs[i].contours)) { draw = false; bad = p.glyphs[i].name; }
        }
        check("otf: every charstring decodes", decoded, bad);
        check("otf: charstring widths = advances", width, bad);
        check("otf: charstrings redraw the outlines exactly (curves included)", draw, bad);
    }

    // cmap.
    {
        const auto cmap = otf::table(font, "cmap");
        bool ok = true;
        for (const auto& [cp, gid] : p.cmap)
            if (cp <= 0xFFFF && cmap4_lookup(cmap, static_cast<unsigned>(cp)) != gid) ok = false;
        check("otf: cmap format 4 maps every BMP code point", ok);
        check("otf: unmapped code point -> 0", cmap4_lookup(cmap, 0x4E00) == 0);
        check("otf: a supplementary code point brings format 12", rd16(cmap, 2) == 4);
        check("otf: two code points on one glyph (o, Cyrillic o)",
              cmap4_lookup(cmap, 'o') == p.gid.at("o") && cmap4_lookup(cmap, 0x043E) == p.gid.at("o"));
    }

    // GPOS against model::kern_lookup, every glyph pair.
    auto gpos_matches = [&](const model::Font& font_, const otf::Bytes& compiled, std::string& bad) {
        const otf::Prepared pp = otf::prepare(font_, opt, r2);
        const auto gpos = otf::table(compiled, "GPOS");
        for (const auto& [l, gl] : pp.gid)
            for (const auto& [r, gr] : pp.gid) {
                const int want = model::kern_lookup(font_.kerning(), l, r).value;
                const int got = gpos_kern(gpos, static_cast<unsigned>(gl), static_cast<unsigned>(gr));
                if (want != got) { bad = l + "/" + r + " want " + std::to_string(want) + " got " + std::to_string(got); return false; }
            }
        return true;
    };
    {
        std::string bad;
        check("otf: GPOS kerns every pair as the strip does (fixture)", gpos_matches(f, font, bad), bad);
        // Exceptions over classes, a zero exception, glyph/group and group/glyph.
        model::Kerning k = f.kerning();
        k.groups["public.kern2.O"] = {"O", "Odotbelow", "c"};
        k.groups["public.kern1.H"] = {"H", "n"};
        k.pairs[{"public.kern1.H", "public.kern2.O"}] = -30;
        k.pairs[{"n", "c"}] = 0;                       // a zero exception stops the class
        k.pairs[{"H", "public.kern2.O"}] = -22;        // glyph / group
        k.pairs[{"public.kern1.O", "o"}] = -8;         // group / glyph
        k.pairs[{"Odotbelow", "o"}] = 12;              // glyph / glyph wins
        {
            model::Font::Loading load(f);
            f.put(model::Font::kKerning, model::Record{k});
        }
        otf::OtfReport kr;
        const otf::Bytes kf = otf::compile_otf(f, opt, &kr);
        check("otf: GPOS precedence -- exceptions, zero, glyph/group, group/glyph", gpos_matches(f, kf, bad), bad);
        check("otf: a zero exception is written (it stops the class)",
              gpos_kern(otf::table(kf, "GPOS"), static_cast<unsigned>(p.gid.at("n")), static_cast<unsigned>(p.gid.at("c"))) == 0 &&
              model::kern_lookup(k, "H", "c").value == -22);
    }
    {   // Big classes: the class matrix splits, the lookup goes Extension.
        model::Font big;
        model::make_new_font(big);
        model::Kerning k;
        {
            model::Font::Loading load(big);
            for (int i = 0; i < 400; ++i) {
                model::Glyph g;
                g.name = "g" + std::to_string(i);
                g.advance = 500;
                g.order = 10 + i;
                big.put(g.name, model::Record{g});
                k.groups["public.kern1.L" + std::to_string(i)] = {g.name};
                k.groups["public.kern2.R" + std::to_string(i)] = {g.name};
            }
            for (int i = 0; i < 400; ++i)
                for (int j = 0; j < 400; ++j) k.pairs[{"public.kern1.L" + std::to_string(i), "public.kern2.R" + std::to_string(j)}] = (i * 31 + j) % 90 - 45;
            k.pairs[{"g3", "g10"}] = 77;
            big.put(model::Font::kKerning, model::Record{k});
        }
        otf::OtfReport br;
        const otf::Bytes bf = otf::compile_otf(big, opt, &br);
        int subtables = 0;
        bool ext = false;
        const auto gpos = otf::table(bf, "GPOS");
        gpos_kern(gpos, 1, 1, &subtables, &ext);
        check("otf: a big class matrix splits into subtables under 64 KB", subtables > 2 && br.gpos_subtables == subtables,
              std::to_string(subtables) + " subtables, " + std::to_string(gpos.size()) + " bytes");
        check("otf: and the lookup goes Extension", ext);
        std::string bad;
        check("otf: the split font still kerns every pair right", gpos_matches(big, bf, bad), bad);
    }

    // Names.
    check("otf: PostScript name strips spaces and brackets", otf::postscript_name("My Font (Test)", "Bold Italic") == "MyFontTest-BoldItalic");
    check("otf: weight classes", otf::weight_class("Light") == 300 && otf::weight_class("SemiBold") == 600 &&
                                 otf::weight_class("ExtraBold") == 800 && otf::weight_class("Bold Italic") == 700 &&
                                 otf::weight_class("Regular") == 400);
    {
        const auto os2 = otf::table(font, "OS/2");
        const std::uint32_t ur1 = rd32(os2, 42), ur2 = rd32(os2, 46);
        check("otf: OS/2 v4, 96 bytes, Basic Latin + Cyrillic + non-BMP range bits", rd16(os2, 0) == 4 && os2.size() == 96 &&
              (ur1 & 1u) && (ur1 & (1u << 9)) && (ur2 & (1u << (57 - 32))));
        check("otf: USE_TYPO_METRICS and REGULAR set", (rd16(os2, 62) & 0xC0) == 0xC0);
    }

    // The file.
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "granjon_selftest_otf";
        fs::remove_all(dir);
        fs::create_directories(dir);
        std::string why;
        const fs::path to = dir / otf::default_file_name(f.info());
        check("otf: export writes the file", otf::export_otf(f, to, opt, nullptr, &why) && fs::file_size(to) > 0, why);
        check("otf: no temp file left", !fs::exists(to.string() + ".exporting"));
        check("otf: an unwritable place is refused, with a reason", !otf::export_otf(f, dir / "no" / "x.otf", opt, nullptr, &why) && !why.empty(), why);
        fs::remove_all(dir);
    }
}
