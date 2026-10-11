// selftest_import -- M11: our own fonts read back (OTF and TTF round trips),
// and bad input refused with the open font untouched.
#include "command/Verbs.hpp"
#include "export/Otf.hpp"
#include "geom/Edit.hpp"
#include "import/FontReader.hpp"
#include "model/Spacing.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <string>

namespace model  = granjon::model;
namespace otf    = granjon::otf;
namespace reader = granjon::reader;

void selftest_import(const std::function<void(const std::string&, bool, const std::string&)>& check_) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_(n, ok, d); };
    std::cout << "M11 import\n";
    model::Font src;
    granjon::command::make_fixture(src);
    {   // a curve, a kerning group, a name, a non-default UPM-free tweak
        model::Font::Loading l(src);
        model::Glyph c;
        c.name = "c"; c.unicodes = {U'c'}; c.advance = 500; c.order = 100;
        model::Contour k;
        model::Node a{250, -10, 120, -10, 380, -10, model::Node::Type::Smooth};
        model::Node b{450, 250, 450, 100, 450, 400, model::Node::Type::Smooth};
        model::Node t{250, 510, 380, 510, 120, 510, model::Node::Type::Smooth};
        model::Node lft{50, 250, 50, 400, 50, 100, model::Node::Type::Smooth};
        k.nodes = {a, b, t, lft};
        c.contours = {k};
        c.status = model::GlyphStatus::Drafted;
        src.put("c", model::Record{c});
        model::FontInfo info = src.info();
        info.family = "Selftest Sans";
        info.style = "Bold";
        info.copyright = "(c) nobody";
        src.put(model::Font::kInfo, model::Record{info});
        model::Kerning kk = src.kerning();
        kk.groups["public.kern2.O"] = {"O", "Odotbelow", "c"};
        kk.pairs[{"public.kern1.O", "public.kern2.O"}] = -12;
        kk.pairs[{"n", "c"}] = 9;
        src.put(model::Font::kKerning, model::Record{kk});
    }
    otf::OtfOptions opt;
    opt.time = 1760000000;
    opt.remove_overlaps = false;   // the round trips check the reader against outlines as drawn
    auto kern_same = [&](const model::Font& a, const model::Font& b, std::string& bad) {
        for (const auto& la : a.glyph_order())
            for (const auto& ra : a.glyph_order()) {
                const int x = model::kern_lookup(a.kerning(), la, ra).value, y = model::kern_lookup(b.kerning(), la, ra).value;
                if (x != y) { bad = la + "/" + ra + " " + std::to_string(x) + " != " + std::to_string(y); return false; }
            }
        return true;
    };

    {   // OTF round trip: exact
        const auto bytes = otf::compile_otf(src, opt);
        model::Font back;
        reader::ImportReport rep;
        std::string why;
        check("import: our OTF reads back", reader::import_font(bytes, back, &rep, &why) && rep.format == "CFF", why);
        check("import: names, style, UPM", back.info().family == "Selftest Sans" && back.info().style == "Bold" &&
              back.info().copyright == "(c) nobody" && back.info().units_per_em == 1000);
        check("import: metrics (typo ascender / descender, x-height, cap height from OS/2)",
              back.metrics().ascender == src.metrics().ascender && back.metrics().descender == src.metrics().descender &&
              back.metrics().x_height == src.metrics().x_height && back.metrics().cap_height == src.metrics().cap_height);
        bool same = true;
        std::string bad;
        for (const auto& n : src.glyph_order()) {
            const model::Glyph* g = src.glyph(n);
            const model::Glyph* h = back.glyph(n);
            if (!h) { same = false; bad = n + " missing"; break; }
            const auto want = granjon::geom::all_contours(src, *g);   // components flattened
            if (h->contours.size() != want.size()) { same = false; bad = n; break; }
            for (std::size_t i = 0; i < want.size() && same; ++i)
                for (std::size_t k = 0; k < want[i].nodes.size(); ++k) {
                    const auto& p = want[i].nodes[k];
                    const auto& q = h->contours[i].nodes[k];
                    if (p.x != q.x || p.y != q.y || p.cx1 != q.cx1 || p.cy1 != q.cy1 || p.cx2 != q.cx2 || p.cy2 != q.cy2) { same = false; bad = n; }
                }
            if (h->advance != g->advance || h->unicodes != g->unicodes) { same = false; bad = n + " advance / unicodes"; }
        }
        check("import: OTF outlines, advances, code points back exactly (components flattened)", same, bad);
        check("import: smooth nodes recognised", back.glyph("c")->contours[0].nodes[1].type == model::Node::Type::Smooth);
        check("import: OTF kerning kerns every pair as before", kern_same(src, back, bad), bad);
        check("import: groups came back as groups", !back.kerning().groups.empty(), std::to_string(back.kerning().groups.size()));
    }
    {   // TTF round trip: shape within the conversion tolerance, model direction, kerning exact
        const auto bytes = otf::compile_ttf(src, opt);
        model::Font back;
        reader::ImportReport rep;
        std::string why;
        check("import: our TTF reads back", reader::import_font(bytes, back, &rep, &why) && rep.format == "TrueType", why);
        bool dir = true, close = true;
        std::string bad;
        for (const auto& n : back.glyph_order()) {
            const auto& cs = back.glyph(n)->contours;
            const model::Glyph* g = src.glyph(n);
            // As drawn: the source's own direction comes back (the fixture's O
            // has a wrong-way counter on purpose -- it must come back wrong-way).
            if (g && granjon::geom::wrong_way(cs) != granjon::geom::wrong_way(granjon::geom::all_contours(src, *g))) { dir = false; bad = n; }
            if (g && n != ".notdef") {
                const auto want = granjon::geom::all_contours(src, *g);
                double a1 = 0, a2 = 0;
                for (const auto& c : want) a1 += granjon::geom::signed_area(c);
                for (const auto& c : cs) a2 += granjon::geom::signed_area(c);
                if (std::abs(a1 - a2) > std::max(4.0, std::abs(a1) * 0.005)) { close = false; bad = n + " " + std::to_string(a1) + " vs " + std::to_string(a2); }
            }
        }
        check("import: TTF contours turned to the model's direction (as drawn in the source)", dir, bad);
        check("import: TTF shapes within the conversion tolerance", close, bad);
        check("import: TTF kerning kerns every pair as before", kern_same(src, back, bad), bad);
    }
    {   // bad input: refused, the open font untouched
        model::Font keep;
        granjon::command::make_fixture(keep);
        const auto before = keep.snapshot();
        std::string why;
        auto bytes = otf::compile_otf(src, opt);
        bytes.resize(bytes.size() / 3);
        check("import: a truncated font is refused, with a reason", !reader::import_font(bytes, keep, nullptr, &why) && !why.empty(), why);
        std::vector<std::uint8_t> junk(400, 0x41);
        check("import: not a font is refused", !reader::import_font(junk, keep, nullptr, &why), why);
        std::vector<std::uint8_t> woff = {'w', 'O', 'F', '2', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        check("import: WOFF2 says what it is", !reader::import_font(woff, keep, nullptr, &why) && why.find("WOFF") != std::string::npos, why);
        check("import: a failed import changes nothing", keep.snapshot() == before);
    }
}
