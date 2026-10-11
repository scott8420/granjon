// selftest_presets -- M12: every bundled preset parses, builds a font, and
// what it draws is clean by Granjon's own checks; keys hold; the preset
// survives a save and load; the parser refuses bad lines with a reason.
#include "core/Primer.hpp"
#include "geom/Edit.hpp"
#include "io/Ufo.hpp"
#include "model/Bounds.hpp"
#include "model/Checks.hpp"
#include "model/Presets.hpp"
#include "model/Spacing.hpp"
#include "unicode/CharSet.hpp"
#include "unicode/Embedded.hpp"
#include "model/Coverage.hpp"

#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

namespace model = granjon::model;
namespace geom  = granjon::geom;

void selftest_presets(const std::function<void(const std::string&, bool, const std::string&)>& check_) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_(n, ok, d); };
    std::cout << "M12 presets + primer\n";

    std::vector<std::string> errs;
    model::parse_presets(granjon::unicode::embedded::get("presets/presets.txt"), &errs);
    check("presets: the bundled file parses clean", errs.empty(), errs.empty() ? "" : errs.front());
    const auto& ps = model::presets();
    check("presets: Sans, Serif, Display, Mono in order",
          ps.size() == 4 && ps[0].id == "sans" && ps[1].id == "serif" && ps[2].id == "display" && ps[3].id == "mono");
    check("presets: a shared key table", model::preset_keys().size() >= 20);

    for (const auto& p : ps) {
        const std::string tag = "presets " + p.id + ": ";
        check(tag + "a known char set", granjon::unicode::charset(p.charset) != nullptr, p.charset);
        bool shapes = true;
        for (const auto& n : model::control_glyphs()) shapes = shapes && p.shapes.count(n);
        check(tag + "numbers for all six control glyphs", shapes);

        std::vector<std::string> notes;
        const auto gs = model::starter_glyphs(p, &notes);
        check(tag + "six glyphs drawn, no merge fell back", gs.size() == 6 && notes.empty(),
              notes.empty() ? "" : notes.front());

        model::Font f;
        model::NewFontReport rep;
        model::make_preset_font(f, {p.id, "Test " + p.name, "", true}, &rep);
        check(tag + "no stray writes (a load, not a step)", f.strays() == 0);
        check(tag + "metrics and UPM from the preset",
              f.metrics() == p.metrics && f.info().units_per_em == p.units_per_em);
        check(tag + "the preset noted (lib granjon.preset)",
              f.info().origin.preset == p.id && f.info().origin.stem_v == p.stem_v);
        check(tag + "font guides", f.info().guides.size() == p.guides.size() && !p.guides.empty());
        const auto* cs = granjon::unicode::charset(p.charset);
        check(tag + "the whole set is in the font",
              cs && model::missing_from(f, cs->cps).empty(), std::to_string(rep.added) + " added");
        check(tag + "accents built as components", rep.composites > 20, std::to_string(rep.composites));

        // Every finding on every glyph: the drawn ones should be clean, the
        // empty ones have nothing to find.
        std::string first;
        int found = 0;
        for (const auto& n : f.glyph_order())
            for (const auto& x : model::check_glyph(f, n)) {
                if (first.empty()) first = n + ": " + x.message;
                ++found;
            }
        check(tag + "the Checks report is empty", found == 0, first);

        bool shut = true, cubic_ok = true;
        for (const auto& n : model::control_glyphs()) {
            const auto* g = f.glyph(n);
            if (!g || g->contours.empty()) { shut = false; continue; }
            for (const auto& c : g->contours) shut = shut && c.closed;
            cubic_ok = cubic_ok && g->is_integral();
        }
        check(tag + "control glyphs closed and integral", shut && cubic_ok);

        // Counters: o and O have two contours (outer + hole); n one; H one.
        const auto count = [&](const char* n) { return f.glyph(n) ? f.glyph(n)->contours.size() : 0u; };
        check(tag + "n / H one outline, o / O / p two (a counter)",
              count("n") == 1 && count("H") == 1 && count("o") == 2 && count("O") == 2 && count("p") == 2,
              std::to_string(count("n")) + std::to_string(count("H")) + std::to_string(count("o")) +
              std::to_string(count("O")) + std::to_string(count("p")));

        // Overshoot: o rounds past the x-height and the baseline.
        const auto bo = model::bounds(f, *f.glyph("o"));
        check(tag + "o overshoots both lines by the preset's amount",
              bo && std::lround(bo->y1) == p.metrics.x_height + p.metrics.overshoot &&
              std::lround(bo->y0) == -p.metrics.overshoot);
        const auto bp = model::bounds(f, *f.glyph("p"));
        check(tag + "p's stem reaches the descender", bp && std::lround(bp->y0) == p.metrics.descender);

        if (p.mono > 0) {
            bool same = true;
            for (const auto& n : f.glyph_order()) same = same && f.glyph(n)->advance == p.mono;
            check(tag + "every advance the same", same);
            const auto sb = model::sidebearings(f, *f.glyph("n"));
            check(tag + "n centred in its cell", sb && std::abs(sb->left - sb->right) <= 1);
        } else {
            const auto sb = model::sidebearings(f, *f.glyph("n"));
            const auto& s = p.shapes.at("n");
            check(tag + "n spaced by its numbers",
                  sb && std::lround(sb->left) == s.left && std::lround(sb->right) == s.right);
            check(tag + "keys from the table (b, m, u)",
                  f.metrics_keys().keys.count("b") && f.metrics_keys().keys.at("u").left == "|n");
        }
        bool keys_hold = true;
        for (const auto& n : model::control_glyphs()) keys_hold = keys_hold && !model::check_key(f, n).off();
        check(tag + "the control glyphs match their keys", keys_hold);
    }

    {   // round trip: the preset survives the source format
        model::Font f, g;
        model::make_preset_font(f, {"serif", "Round Trip", "@none", false});
        const auto dir = std::filesystem::temp_directory_path() / "granjon-selftest-preset.granjon";
        std::string err;
        const bool ok = granjon::io::save_ufo(f, dir.string(), &err) && granjon::io::load_ufo(dir.string(), g, &err);
        check("presets: origin + guides survive save / load", ok && g.info().origin == f.info().origin &&
              g.info().guides == f.info().guides && g.metrics_keys() == f.metrics_keys(), err);
        check("presets: '@none' = the control glyphs only", f.glyph_count() == 8, std::to_string(f.glyph_count()));
        std::filesystem::remove_all(dir);
    }
    {   // a blank font is the old New
        model::Font f;
        model::make_preset_font(f, {"", "Plain", "", true});
        check("presets: no preset = a blank font (notdef + space), named",
              f.glyph_count() == 2 && f.info().family == "Plain" && f.info().origin.preset.empty());
    }
    {   // the Primer: every note whole, every figure there, every link followable
        std::vector<std::string> e;
        const auto notes = granjon::core::parse_primer(granjon::unicode::embedded::get("primer/notes.txt"), &e);
        check("primer: the notes parse clean", e.empty(), e.empty() ? "" : e.front());
        const std::vector<std::string> want{"overshoot", "stems", "spacing", "joins", "optical", "order"};
        std::vector<std::string> ids;
        for (const auto& n : notes) ids.push_back(n.id);
        check("primer: six notes in order (overshoot ... drawing order)", ids == want);
        std::string bad;
        for (const auto& n : notes) {
            const auto svg = granjon::core::primer_figure(n.figure);
            if (n.title.empty() || n.paragraphs.size() < 2) bad = n.id + ": title / body";
            else if (svg.find("<svg") == std::string_view::npos) bad = n.id + ": figure " + n.figure;
            else if (n.links.empty()) bad = n.id + ": no links";
            for (const auto& l : n.links)
                if (!granjon::core::parse_link_target(l.target).valid) bad = n.id + ": link " + l.target;
        }
        check("primer: each note has a title, body, a figure that exists, links the app follows", bad.empty(), bad);
        check("primer: a link to a check the report doesn't have is refused",
              !granjon::core::parse_link_target("report:kerning").valid &&
              !granjon::core::parse_link_target("view:nowhere").valid &&
              granjon::core::parse_link_target("tool:measure:o").valid);
        check("primer: *bold* and escaping", granjon::core::primer_markup("a *b* <c> & d") ==
              "a <b>b</b> &lt;c&gt; &amp; d");
        std::vector<std::string> e2;
        granjon::core::parse_primer("stray\n[x]\nlink: no bar\n", &e2);
        check("primer: bad lines refused (text before a note, a link without '|', no title)", e2.size() == 3,
              std::to_string(e2.size()));
    }
    {   // the parser's refusals
        std::vector<std::string> e;
        const auto pf = model::parse_presets("[x]\nupm: lots\nglyph: n 1\nwhat\n[keys]\nkey: b Q=n\n", &e);
        check("presets: bad lines refused with their line number",
              e.size() == 4 && e[0].rfind("line 2:", 0) == 0 && pf.presets.size() == 1 && pf.keys.empty(),
              std::to_string(e.size()));
    }
}
