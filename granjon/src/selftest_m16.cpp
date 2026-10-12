// selftest_m16 -- M16: font info + italic angle (M16a), mark positioning
// (M16b), the features editor / GSUB compiler (M16c).
#include "command/Document.hpp"
#include "command/Verbs.hpp"
#include "export/Otf.hpp"
#include "import/FontReader.hpp"
#include "io/Ufo.hpp"
#include "export/OtfParts.hpp"
#include "geom/Shapes.hpp"
#include "model/Presets.hpp"
#include "model/Fea.hpp"
#include "model/Proof.hpp"
#include "model/Checks.hpp"
#include "model/Bounds.hpp"

#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

namespace model   = granjon::model;
namespace command = granjon::command;
namespace otf     = granjon::otf;
namespace reader  = granjon::reader;
namespace io      = granjon::io;

namespace {
using Bytes = std::vector<std::uint8_t>;
unsigned u16(const Bytes& b, std::size_t at) { return (b[at] << 8) | b[at + 1]; }
// A table's bytes out of an sfnt ("" if absent).
Bytes table(const Bytes& f, const std::string& tag) {
    const unsigned n = u16(f, 4);
    for (unsigned t = 0; t < n; ++t) {
        const std::size_t rec = 12 + 16 * static_cast<std::size_t>(t);
        if (std::string(f.begin() + rec, f.begin() + rec + 4) != tag) continue;
        const std::size_t off = (static_cast<std::size_t>(u16(f, rec + 8)) << 16) | u16(f, rec + 10);
        const std::size_t len = (static_cast<std::size_t>(u16(f, rec + 12)) << 16) | u16(f, rec + 14);
        return Bytes(f.begin() + off, f.begin() + off + len);
    }
    return {};
}
std::vector<std::string> feature_tags(const Bytes& layout) {
    std::vector<std::string> out;
    if (layout.size() < 10) return out;
    const std::size_t fl = u16(layout, 6);
    const unsigned n = u16(layout, fl);
    for (unsigned i = 0; i < n; ++i) out.emplace_back(layout.begin() + fl + 2 + 6 * i, layout.begin() + fl + 6 + 6 * i);
    return out;
}
}  // namespace

void selftest_m16(const std::function<void(const std::string&, bool, const std::string&)>& check_) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_(n, ok, d); };
    std::cout << "M16a font info\n";
    {
        command::Document doc;
        command::make_fixture(doc.font);
        auto& j = doc.journal;
        auto& f = doc.font;
        model::FontInfo i = f.info();
        i.italic_angle = -12;
        i.designer = "Scott";
        i.designer_url = "https://example.com/scott";
        i.manufacturer = "Granjon Type";
        i.manufacturer_url = "https://example.com";
        i.trademark = "Testface is a trademark";
        i.description = "A test face.";
        i.weight_class = 300;
        i.width_class = 4;
        std::string why;
        check("info: set_info is one step", command::set_info(j, f, i, "Font info", &why) && j.size() == 1, why);
        check("info: nothing changed -> no step", !command::set_info(j, f, i, "Font info") && j.size() == 1);
        auto bad = i;
        bad.italic_angle = -60;
        check("info: an italic angle past 45 refused", !command::set_info(j, f, bad, "x", &why) && !why.empty(), why);
        bad = i;
        bad.width_class = 12;
        check("info: a width class past 9 refused", !command::set_info(j, f, bad, "x", &why), why);
        bad = i;
        bad.vendor = "TOOLONG";
        check("info: a vendor ID over 4 refused", !command::set_info(j, f, bad, "x", &why), why);

        const auto dir = std::filesystem::temp_directory_path() / "granjon_m16a.ufo";
        std::filesystem::remove_all(dir);
        model::Font back;
        check("info: UFO round trip", io::save_ufo(f, dir.string(), &why) && io::load_ufo(dir.string(), back, &why) &&
                                          back.info() == f.info(), why);
        std::filesystem::remove_all(dir);

        for (int ttf = 0; ttf < 2; ++ttf) {
            const auto bytes = ttf ? otf::compile_ttf(f) : otf::compile_otf(f);
            model::Font im;
            reader::ImportReport rep;
            const bool ok = reader::import_font(bytes, im, &rep, &why);
            const auto& r = im.info();
            check(std::string("info: ") + (ttf ? "TTF" : "OTF") + " carries the italic angle, names 7-12, weight / width",
                  ok && r.italic_angle == -12 && r.designer == "Scott" && r.designer_url == i.designer_url &&
                      r.manufacturer == i.manufacturer && r.manufacturer_url == i.manufacturer_url &&
                      r.trademark == i.trademark && r.description == i.description && r.weight_class == 300 &&
                      r.width_class == 4,
                  why + " angle " + std::to_string(r.italic_angle) + " weight " + std::to_string(r.weight_class));
            // hhea caret: rise / run lean with the angle (tan 12 deg * 1000 = 213)
            auto u16 = [&](std::size_t at) { return (bytes[at] << 8) | bytes[at + 1]; };
            const int n = u16(4);
            int caret_rise = 0, caret_run = 0;
            for (int t = 0; t < n; ++t) {
                const std::size_t rec = 12 + 16 * static_cast<std::size_t>(t);
                if (std::string(bytes.begin() + rec, bytes.begin() + rec + 4) != "hhea") continue;
                const std::size_t off = (static_cast<std::size_t>(u16(rec + 8)) << 16) | u16(rec + 10);
                caret_rise = static_cast<std::int16_t>(u16(off + 18));
                caret_run = static_cast<std::int16_t>(u16(off + 20));
            }
            check(std::string("info: ") + (ttf ? "TTF" : "OTF") + " hhea caret slope 1000 / 213",
                  caret_rise == 1000 && caret_run == 213,
                  std::to_string(caret_rise) + " / " + std::to_string(caret_run));
        }
    }

    std::cout << "M16b mark positioning\n";
    {
        command::Document d;
        model::make_preset_font(d.font, {"sans", "Marks", "", true});
        auto& f = d.font;
        auto& j = d.journal;
        command::add_shape(j, f, "acutecomb", granjon::geom::shape_rect({{-40, 560}, {40, 680}}), "R");
        command::add_default_anchors(j, f, f.glyph_order());
        const model::Glyph* ac = f.glyph("acutecomb");
        bool under = false, over = false;
        for (const auto& a : ac->anchors) { under = under || a.name == "_top"; over = over || a.name == "top"; }
        check("marks: default anchors give acutecomb _top and top", under && over);
        otf::OtfReport rep;
        const auto otf_b = otf::compile_otf(f, {}, &rep);
        const auto tags = feature_tags(table(otf_b, "GPOS"));
        check("marks: GPOS has mark and mkmk", std::find(tags.begin(), tags.end(), "mark") != tags.end() &&
                                                   std::find(tags.begin(), tags.end(), "mkmk") != tags.end(),
              std::to_string(tags.size()) + " features");
        check("marks: one class (top), many bases, a stacking mark", rep.mark_classes == 1 && rep.mark_bases > 10 && rep.mkmk_pairs == 1,
              std::to_string(rep.mark_classes) + " / " + std::to_string(rep.mark_bases) + " / " + std::to_string(rep.mkmk_pairs));
        const Bytes gdef = table(otf_b, "GDEF");
        check("marks: GDEF glyph classes written", gdef.size() > 12 && u16(gdef, 0) == 1 && u16(gdef, 4) == 12);
        const auto ttf_b = otf::compile_ttf(f);
        check("marks: the TTF too", !table(ttf_b, "GDEF").empty() && feature_tags(table(ttf_b, "GPOS")).size() >= 2);

        command::Document plain;
        command::make_fixture(plain.font);
        const auto pb = otf::compile_otf(plain.font);
        const auto ptags = feature_tags(table(pb, "GPOS"));
        check("marks: a font without marks -- no GDEF, GPOS kern only", table(pb, "GDEF").empty() && ptags.size() == 1 && ptags[0] == "kern");

        // anchor propagation into a composite with none of its own
        model::Font& ff = plain.font;
        auto& jj = plain.journal;
        command::add_anchor(jj, ff, "H", "top", 350, 700);
        model::Glyph mk;
        command::add_glyph(jj, ff, "dotaccentcomb", {U'̇'});
        command::add_anchor(jj, ff, "dotaccentcomb", "_top", 0, 700);
        command::add_anchor(jj, ff, "dotaccentcomb", "top", 0, 800);
        command::add_glyph(jj, ff, "Hdot", {U'Ḣ'});
        command::add_component(jj, ff, "Hdot", "H", 0, 0);
        command::add_component(jj, ff, "Hdot", "dotaccentcomb", 350, 0);
        const auto an = otf::glyph_anchors(ff, *ff.glyph("Hdot"));
        bool lifted = false;
        for (const auto& a : an) lifted = lifted || (a.name == "top" && a.x == 350 && a.y == 800);
        check("marks: a composite inherits its base's anchors, lifted by a stacking mark", lifted && an.size() == 1,
              std::to_string(an.size()));
    }

    std::cout << "M16c features\n";
    {
        using K = model::FeaRule::Kind;
        const std::string src =
            "# a comment\n"
            "languagesystem DFLT dflt;\n"
            "@lc = [a - c];\n"
            "@sc = [a.sc b.sc c.sc];\n"
            "lookup ALTS { sub a from [a.alt a.sc]; } ALTS;\n"
            "feature smcp {\n  sub @lc by @sc;\n} smcp;\n"
            "feature liga {\n  sub f f i by f_f_i;\n  sub f i by f_i;\n  sub [f f.alt] l by f_l;\n} liga;\n"
            "feature ccmp { sub f_i by f i; } ccmp;\n"
            "feature salt { lookup ALTS; } salt;\n"
            "feature kern { pos a b -20; } kern;\n";
        const auto d = model::parse_fea(src);
        check("fea: parses (classes, ranges, named lookups, four kinds)", d.ok() && d.rules == 6,
              d.errors.empty() ? std::to_string(d.rules) : "line " + std::to_string(d.errors[0].line) + ": " + d.errors[0].text);
        bool kinds = d.lookups.size() == 4;
        if (kinds) kinds = d.lookups[0].kind == K::Alternate && d.lookups[1].kind == K::Single &&
                           d.lookups[2].kind == K::Ligature && d.lookups[3].kind == K::Multiple;
        check("fea: one lookup per kind run, the named one shared", kinds && d.features.size() == 4 &&
                                                                       d.features[3].lookups == std::vector<int>{0});
        check("fea: a class range a - c", d.ok() && d.lookups[1].rules[0].in[0] == std::vector<std::string>{"a", "b", "c"} &&
                                              d.lookups[1].rules[0].out == std::vector<std::string>{"a.sc", "b.sc", "c.sc"});
        check("fea: pos rules skipped with a warning (kern has none left)", d.warnings.size() == 1);
        auto err = [](const std::string& t, const char* want) {
            const auto x = model::parse_fea(t, [](const std::string& n) { return n != "nope"; });
            return !x.ok() && x.errors[0].text.find(want) != std::string::npos;
        };
        check("fea: errors -- reverse chaining, unknown glyph, class sizes, unbalanced, include",
              err("feature calt { rsub a' b by c; } calt;", "reverse") && err("feature liga { sub nope by a; } liga;", "unknown glyph") &&
                  err("feature smcp { sub [a b] by [c d e]; } smcp;", "classes must match") &&
                  err("feature liga { sub a b by c; ", "expected") && err("include(x.fea);", "include"));
        const auto e = model::parse_fea("\n\nfeature liga {\n  sub f i by ;\n} liga;\n");
        check("fea: an error names its line", !e.ok() && e.errors[0].line == 4, e.errors.empty() ? "" : std::to_string(e.errors[0].line));

        // generate, compile, round trip
        command::Document doc;
        model::make_preset_font(doc.font, {"sans", "Fea", "", true});
        auto& f = doc.font;
        auto& j = doc.journal;
        for (const char* n : {"f_i", "f_f_i", "a.sc", "b.sc", "a.ss01", "one.onum", "g.alt", "g.alt2"}) command::add_glyph(j, f, n, {});
        const std::string gen = model::generate_fea(f);
        check("generate: liga, smcp + c2sc, ss01, onum, salt set", gen.find("sub f f i by f_f_i;") != std::string::npos &&
                                                                       gen.find("sub f i by f_i;") != std::string::npos &&
                                                                       gen.find("feature smcp") != std::string::npos &&
                                                                       gen.find("sub A by a.sc;") != std::string::npos &&
                                                                       gen.find("feature ss01") != std::string::npos &&
                                                                       gen.find("sub one by one.onum;") != std::string::npos &&
                                                                       gen.find("sub g from [g.alt g.alt2];") != std::string::npos,
              gen.substr(0, 200));
        check("generate: what it writes parses against the font", model::parse_fea(gen, [&](const std::string& n) { return f.glyph(n) != nullptr; }).ok());
        const std::string mine = "feature liga {\n  sub f l by f_i;\n} liga;\n";
        const std::string both = model::replace_auto_block(mine, gen);
        check("generate: the automatic block goes on top, the hand-written code kept; again replaces it",
              both.rfind(model::kFeaAutoBegin, 0) == 0 && both.find(mine) != std::string::npos &&
                  model::replace_auto_block(both, gen) == both && model::replace_auto_block(both, "") == mine);
        const std::size_t s0 = j.size();
        command::set_features(j, f, gen, "Generate features", false);
        check("set_features: one step", j.size() == s0 + 1 && f.features().text == gen);
        otf::OtfReport rep;
        const auto b = otf::compile_otf(f, {}, &rep);
        const auto gtags = feature_tags(table(b, "GSUB"));
        check("gsub: written, its features", !table(b, "GSUB").empty() && rep.fea_errors == 0 && rep.gsub_features == static_cast<int>(gtags.size()) &&
                                                 std::find(gtags.begin(), gtags.end(), "liga") != gtags.end() &&
                                                 std::find(gtags.begin(), gtags.end(), "salt") != gtags.end(),
              std::to_string(gtags.size()) + " / " + std::to_string(rep.gsub_features));
        const Bytes os2 = table(b, "OS/2");
        check("gsub: usMaxContext = 3 (f f i)", os2.size() >= 96 && u16(os2, 94) == 3, os2.size() >= 96 ? std::to_string(u16(os2, 94)) : "");
        command::set_features(j, f, "feature liga { sub f i by nope; } liga;", "x", false);
        otf::OtfReport r2;
        const auto b2 = otf::compile_otf(f, {}, &r2);
        check("gsub: an error -> no GSUB, a note", table(b2, "GSUB").empty() && r2.fea_errors == 1 && !r2.notes.empty());
        command::set_features(j, f, gen, "x", false);
        const auto dir = std::filesystem::temp_directory_path() / "granjon_m16c.ufo";
        std::filesystem::remove_all(dir);
        model::Font back;
        std::string why;
        check("features: UFO round trip (features.fea)", io::save_ufo(f, dir.string(), &why) && io::load_ufo(dir.string(), back, &why) &&
                                                             back.features().text == gen, why);
        std::filesystem::remove_all(dir);
    }

    std::cout << "s10 print proof (the plan)\n";
    {
        command::Document d;
        model::make_preset_font(d.font, {"sans", "Proof", "", true});
        auto& f = d.font;
        auto& j = d.journal;
        for (const char* n : {"f_i", "a.sc"}) command::add_glyph(j, f, n, {});
        command::set_features(j, f, model::generate_fea(f), "g", false);
        model::Kerning k = f.kerning();
        k.pairs[{"v", "o"}] = -30;
        k.pairs[{"H", "O"}] = 0;   // a zero pair is not shown
        j.run("kern", {model::Font::kKerning}, [&] { f.put(model::Font::kKerning, model::Record{k}); });
        const auto p = model::proof_plan(f, "nov HOH");
        check("proof: names, the sample, the paragraph", p.family == "Proof" && p.version == "1.000" && p.sample == "nov HOH" &&
                                                            !p.paragraph.empty());
        check("proof: spacing lines from the drawn letters and controls",
              !p.spacing.empty() && p.spacing[0].rfind("nnnnn nnonn nnpnn nnvnn", 0) == 0, p.spacing.empty() ? "" : p.spacing[0]);
        check("proof: kerning -- the non-zero, typable pairs", p.kern_pairs == 1 && p.kerning.size() == 1 && p.kerning[0] == "vo",
              std::to_string(p.kern_pairs));
        bool liga = false, smcp = false;
        for (const auto& ft : p.features) { liga = liga || (ft.tag == "liga" && ft.text == "fi"); smcp = smcp || (ft.tag == "smcp" && ft.text == "a"); }
        check("proof: features with typable inputs (liga fi, smcp a)", liga && smcp, std::to_string(p.features.size()));
        check("proof: the glyph pages hold only glyphs that draw (n o H O p v, .notdef)",
              std::find(p.glyphs.begin(), p.glyphs.end(), "n") != p.glyphs.end() &&
                  std::find(p.glyphs.begin(), p.glyphs.end(), "a.sc") == p.glyphs.end() &&
                  std::find(p.glyphs.begin(), p.glyphs.end(), "Agrave") == p.glyphs.end() &&
                  std::find(p.glyphs.begin(), p.glyphs.end(), "_part.stem") == p.glyphs.end(),
              std::to_string(p.glyphs.size()));
        check("proof: what the font lacks is named", p.missing.find('Q') != std::string::npos && p.missing.find('n') == std::string::npos);
    }

    std::cout << "s10 GSUB import\n";
    {
        command::Document d;
        model::make_preset_font(d.font, {"sans", "Gsub", "", true});
        auto& f = d.font;
        auto& j = d.journal;
        for (const char* n : {"f_i", "f_f_i", "a.sc", "b.sc", "a.ss01", "g.alt", "g.alt2"}) command::add_glyph(j, f, n, {});
        command::set_features(j, f, model::generate_fea(f) + "feature ccmp { sub f_i by f i; } ccmp;\n", "g", false);
        for (int ttf = 0; ttf < 2; ++ttf) {
            const auto b = ttf ? otf::compile_ttf(f) : otf::compile_otf(f);
            model::Font back;
            reader::ImportReport rep;
            std::string why;
            const bool ok = reader::import_font(b, back, &rep, &why);
            const std::string& t = back.features().text;
            const auto doc = model::parse_fea(t, [&](const std::string& n) { return back.glyph(n) != nullptr; });
            const std::string tag = ttf ? "TTF" : "OTF";
            check("gsub import (" + tag + "): read into the feature file, and it parses", ok && !t.empty() && doc.ok(),
                  doc.errors.empty() ? why : doc.errors[0].text);
            check("gsub import (" + tag + "): ligature, single, alternate, multiple rules back",
                  t.find("sub f f i by f_f_i;") != std::string::npos && t.find("sub a by a.sc;") != std::string::npos &&
                      t.find("sub g from [g.alt g.alt2];") != std::string::npos && t.find("sub f_i by f i;") != std::string::npos);
            const auto b2 = otf::compile_otf(back);
            check("gsub import (" + tag + "): compiles again to the same features",
                  feature_tags(table(b2, "GSUB")) == feature_tags(table(otf::compile_otf(f), "GSUB")));
        }
    }

    std::cout << "s10 stem and sidebearing warnings\n";
    {
        command::Document d;
        model::make_preset_font(d.font, {"sans", "Drift", "", true});
        auto& f = d.font;
        auto& j = d.journal;
        auto count = [&](const std::string& g, model::CheckId id) {
            int n = 0;
            for (const auto& x : model::check_glyph(f, g)) n += x.check == id;
            return n;
        };
        int clean = 0;
        for (const char* g : {"n", "o", "H", "O", "p", "v"})
            clean += count(g, model::CheckId::Stems) + count(g, model::CheckId::Sidebearings);
        check("drift: a fresh preset has none", clean == 0, std::to_string(clean));
        // f (no metrics key in the presets): a stem 86 wide (the font's 88) at x 62 (n's left side is 64)
        int at = -1;
        command::add_shape(j, f, "f", granjon::geom::shape_rect({{62, 0}, {148, 760}}), "R", &at);
        command::set_advance(j, f, "f", 208);   // right side 60 (n's 58)
        const auto xs = model::check_glyph(f, "f");
        int stems = 0, sides = 0;
        std::vector<model::Finding> fix;
        for (const auto& x : xs) {
            if (x.check == model::CheckId::Stems) { ++stems; fix.push_back(x); }
            if (x.check == model::CheckId::Sidebearings) { ++sides; fix.push_back(x); }
        }
        check("drift: f's 86 stem and its left side are flagged (warnings)", stems == 1 && sides >= 1 &&
                                                                             model::check_info(model::CheckId::Stems).warning,
              std::to_string(stems) + " / " + std::to_string(sides));
        command::fix_findings(j, f, fix);
        const auto sb = model::sidebearings(f, *f.glyph("f"));
        const auto nsb = model::sidebearings(f, *f.glyph("n"));
        check("drift: Fix -- the stem 88, the sides n's", count("f", model::CheckId::Stems) == 0 &&
                                                              count("f", model::CheckId::Sidebearings) == 0 && sb && nsb &&
                                                              std::abs(sb->left - nsb->left) < 0.5,
              sb ? std::to_string(sb->left) + " / " + std::to_string(sb->right) : "");
        // far off is intent, not drift
        command::add_shape(j, f, "i", granjon::geom::shape_rect({{64, 0}, {124, 500}}), "R", &at);
        check("drift: a stem far from the font's (60 vs 88) is left alone", count("i", model::CheckId::Stems) == 0);
        command::add_shape(j, f, "l", granjon::geom::shape_rect({{62, 0}, {150, 760}}), "R", &at);
        check("drift: a glyph with a metrics key (l) is the Keys check's, not this one's", count("l", model::CheckId::Sidebearings) == 0);
    }

    std::cout << "s10 contextual substitution\n";
    {
        using K = model::FeaRule::Kind;
        const std::string src =
            "lookup DOTS { sub a by a.sc; } DOTS;\n"
            "feature calt {\n"
            "  ignore sub b a';\n"
            "  sub [c d] a' [e f] by a.alt;\n"
            "  sub x a' lookup DOTS;\n"
            "  sub f' i' by f_i;\n"
            "} calt;\n";
        const auto d = model::parse_fea(src);
        bool ok = d.ok() && d.features.size() == 1;
        int chain = -1;
        if (ok) for (int li : d.features[0].lookups) if (d.lookups[static_cast<std::size_t>(li)].kind == K::Chain) chain = li;
        ok = ok && chain >= 0;
        const auto* L = ok ? &d.lookups[static_cast<std::size_t>(chain)] : nullptr;
        check("contextual: ignore, by-in-context, a named lookup, a ligature in context -- one chain lookup, 4 rules",
              ok && L->rules.size() == 4, d.errors.empty() ? "" : d.errors[0].text);
        if (L && L->rules.size() == 4) {
            const auto& ig = L->rules[0];
            const auto& by = L->rules[1];
            const auto& nm = L->rules[2];
            const auto& lg = L->rules[3];
            check("contextual: ignore has back context and no lookups", ig.ignore && ig.back.size() == 1 && ig.lookups.empty());
            check("contextual: by -> back / in / ahead and a made single lookup",
                  by.back.size() == 1 && by.in.size() == 1 && by.ahead.size() == 1 && by.lookups.size() == 1 &&
                      d.lookups[static_cast<std::size_t>(by.lookups[0].second)].kind == K::Single &&
                      d.lookups[static_cast<std::size_t>(by.lookups[0].second)].rules[0].out[0] == "a.alt");
            check("contextual: lookup DOTS applied at the marked glyph", nm.lookups.size() == 1 && nm.lookups[0].second == 0);
            check("contextual: a marked run by one glyph -> a ligature lookup at its start",
                  lg.in.size() == 2 && d.lookups[static_cast<std::size_t>(lg.lookups[0].second)].kind == K::Ligature);
        }
        check("contextual: marks must be one run", !model::parse_fea("feature calt { sub a' b c' by d; } calt;").ok());

        // frac, generated + compiled + read back
        command::Document doc;
        model::make_preset_font(doc.font, {"sans", "Frac", "", true});
        auto& f = doc.font;
        auto& j = doc.journal;
        for (const char* fig : {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"}) {
            command::add_glyph(j, f, std::string(fig) + ".numr", {});
            command::add_glyph(j, f, std::string(fig) + ".dnom", {});
        }
        const std::string gen = model::generate_fea(f);
        check("generate: frac from .numr / .dnom (+ the slash: this set has no fraction)", gen.find("feature frac") != std::string::npos &&
                                                                  gen.find("[slash zero.dnom") != std::string::npos &&
                                                                  gen.find("' by [zero.dnom") != std::string::npos,
              gen.substr(gen.find("feature frac") == std::string::npos ? 0 : gen.find("feature frac"), 300));
        command::set_features(j, f, gen, "g", false);
        otf::OtfReport rep;
        const auto b = otf::compile_otf(f, {}, &rep);
        check("frac: compiles", rep.fea_errors == 0 && !table(b, "GSUB").empty());
        model::Font back;
        reader::ImportReport ir;
        std::string why;
        reader::import_font(b, back, &ir, &why);
        const auto bd = model::parse_fea(back.features().text, [&](const std::string& n) { return back.glyph(n) != nullptr; });
        bool has_chain = false;
        for (const auto& L2 : bd.lookups) has_chain = has_chain || L2.kind == K::Chain;
        check("frac: the contextual lookup reads back from GSUB (and parses)", bd.ok() && has_chain &&
                                                                                back.features().text.find("' lookup ") != std::string::npos,
              bd.errors.empty() ? "" : bd.errors[0].text);
        check("frac: and compiles again", otf::compile_otf(back).size() > 0 && feature_tags(table(otf::compile_otf(back), "GSUB")) == feature_tags(table(b, "GSUB")));
    }
}
