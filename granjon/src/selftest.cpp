// granjon_selftest -- the headless core harness.
//
// A seed is only trustworthy if its paradigms are EXERCISED, not just described
// (CANON: "a claim in the doc isn't a fact until a consumer exercises it"). This
// runs the GTK-free core with no display, so a future you can trust the pump and
// the mapping before wiring any widget on top:
//   * Recents  -- move-to-front + trim + the JSON round-trip with stale-prune;
//   * TextMap  -- forward(selection) -> inverse(map_range) round-trips byte-exact
//                 across single-line / multi-line / cross-paragraph / UTF-8, and
//                 chrome lines are never landed on.
//
//   cmake --build build --target granjon_selftest && ./build/granjon_selftest

#include "core/Recents.hpp"
#include "core/Settings.hpp"
#include "core/Shortcuts.hpp"
#include "core/Stones.hpp"
#include "core/Undo.hpp"
#include "core/TextMap.hpp"
#include "command/Document.hpp"
#include "command/Verbs.hpp"
#include "geom/Edit.hpp"
#include "model/Bounds.hpp"
#include "io/Glif.hpp"
#include "io/Plist.hpp"
#include "io/Ufo.hpp"
#include "model/Find.hpp"
#include "model/Font.hpp"
#include "model/Composite.hpp"
#include "model/Coverage.hpp"
#include "model/Spacing.hpp"
#include "model/Checks.hpp"
#include "model/Profiles.hpp"
#include "unicode/Agl.hpp"
#include "unicode/Blocks.hpp"
#include "unicode/CharSet.hpp"
#include "unicode/UcdTable.hpp"
#include <cmath>
#include <functional>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string>
#include <vector>

// Per-milestone parts in their own files (the split ARC asks for, begun at M9).
void selftest_export(const std::function<void(const std::string&, bool, const std::string&)>& check);
void selftest_edit(const std::function<void(const std::string&, bool, const std::string&)>& check);
void selftest_import(const std::function<void(const std::string&, bool, const std::string&)>& check);
void selftest_presets(const std::function<void(const std::string&, bool, const std::string&)>& check);
void selftest_image(const std::function<void(const std::string&, bool, const std::string&)>& check);
void selftest_draw(const std::function<void(const std::string&, bool, const std::string&)>& check);

namespace core = granjon::core;
namespace geom = granjon::geom;

namespace {
int g_pass = 0, g_fail = 0;
void check(const std::string& name, bool ok, const std::string& detail = "") {
    if (ok) { ++g_pass; std::cout << "  PASS  " << name; }
    else    { ++g_fail; std::cout << "  FAIL  " << name; }
    if (!detail.empty()) std::cout << "  (" << detail << ")";
    std::cout << "\n";
}

// The FORWARD mapping, mirroring what a viewer's selection() would do: a
// (al,ac)-(cl,cc) mark -> V byte endpoints -> codepoint range + quote. Returns
// the byte endpoints too, so the round-trip through map_range can be exact.
struct Fwd { int cp_start, cp_end, bstart, bend; std::string quote, V; };
Fwd forward(const std::vector<core::FlatLine>& flat, int al, int ac, int cl, int cc) {
    std::vector<int> off;
    std::string V = core::visible_text(flat, off);
    int bstart = off[static_cast<std::size_t>(al)] +
                 std::min(ac, static_cast<int>(flat[static_cast<std::size_t>(al)].text.size()));
    int bend   = off[static_cast<std::size_t>(cl)] +
                 std::min(cc, static_cast<int>(flat[static_cast<std::size_t>(cl)].text.size()));
    std::string quote = V.substr(static_cast<std::size_t>(bstart),
                                 static_cast<std::size_t>(bend - bstart));
    int cps = core::utf8_length(V.substr(0, static_cast<std::size_t>(bstart)));
    int cpe = cps + core::utf8_length(quote);
    return {cps, cpe, bstart, bend, quote, V};
}

// Assert map_range inverts forward(): endpoints mapped back through byte_off
// reproduce the forward byte endpoints and reconstruct the same quote.
void roundtrip(const std::vector<core::FlatLine>& flat,
               int al, int ac, int cl, int cc, const std::string& what) {
    Fwd f = forward(flat, al, ac, cl, cc);
    std::vector<int> off;
    (void)core::visible_text(flat, off);
    core::LineCol s, e;
    const bool ok = core::map_range(flat, f.cp_start, f.cp_end, s, e);
    check("textmap: " + what + " maps", ok);
    if (!ok) return;
    const int rb0 = off[static_cast<std::size_t>(s.line)] + s.col;
    const int rb1 = off[static_cast<std::size_t>(e.line)] + e.col;
    check("textmap: " + what + " round-trips byte-exact",
          rb0 == f.bstart && rb1 == f.bend,
          std::to_string(rb0) + "," + std::to_string(rb1) +
          " vs " + std::to_string(f.bstart) + "," + std::to_string(f.bend));
    check("textmap: " + what + " reconstructs quote",
          f.V.substr(static_cast<std::size_t>(rb0),
                     static_cast<std::size_t>(rb1 - rb0)) == f.quote);
}
}  // namespace

int main() {
    std::cout << "granjon_selftest -- undo journal, font store + verbs, recents, textmap, shortcuts\n";

    // ── Undo -- the one door, two pictures, gestures, merging (s046) ──────────
    {
        std::cout << "\n-- undo --\n";
        using core::Stones;
        using core::Stone;
        using Clock = core::StoneJournal::Clock;
        using SnapMap = std::vector<std::pair<std::string, Stone>>;
        const auto snap = [](const Stones& s) {
            SnapMap out;
            for (const auto& id : s.ids()) out.emplace_back(id, *s.get(id));
            return out;
        };
        const auto names = [](const Stones& s) {
            std::string out;
            for (const auto& id : s.ids()) out += s.get(id)->name + ",";
            return out;
        };
        // A fixture: three stones, LOADED (not a user choice -- not undoable).
        struct Fix {
            Stones s;
            core::StoneJournal j{s};
            std::vector<std::string> sel;
            Fix() {
                s.set_door([this] { return j.open(); });
                j.set_selection_source([this] { return sel; });
                Stones::Loading load(s);
                for (const char* n : {"Ruby", "Jade", "Onyx"}) {
                    const auto id = s.fresh_id();
                    s.put(id, Stone{n, 3, s.pos_after({})});
                }
            }
        };

        // THE GATE. Every verb the page offers, from a fresh fixture: nothing
        // written with the door shut; at least one step; undoing them all puts
        // the model back exactly; redoing them all reproduces the result.
        for (const auto& v : core::stones::verbs()) {
            Fix f;
            const auto first = f.s.ids().front();
            const auto before = snap(f.s);
            v.run(f.j, f.s, first);
            const auto after = snap(f.s);
            const auto steps = f.j.size();
            check("gate/" + v.name + ": through the one door", f.s.strays() == 0,
                  std::to_string(f.s.strays()) + " stray write(s)");
            check("gate/" + v.name + ": is a step", steps > 0 && !(after == before));
            for (std::size_t i = 0; i < steps; ++i) f.j.undo();
            check("gate/" + v.name + ": undo puts it back exactly", snap(f.s) == before);
            for (std::size_t i = 0; i < steps; ++i) f.j.redo();
            check("gate/" + v.name + ": redo reproduces it", snap(f.s) == after);
        }
        {   // ...and the gate bites: a write around the door is counted.
            Fix f;
            const auto id = f.s.ids().front();
            f.s.put(id, Stone{"sneaky", 3, 0.5});
            check("gate: a write around the door is a stray", f.s.strays() == 1);
        }

        {   // Delete comes back whole, in its place, under its id, selected.
            Fix f;
            const auto ids = f.s.ids();
            f.sel = {ids[1]};
            core::stones::remove(f.j, f.s, ids[1]);
            f.sel = {ids[2]};      // the UI selects the neighbour...
            f.j.reselect(f.sel);   // ...and tells the step
            check("delete: gone", names(f.s) == "Ruby,Onyx,", names(f.s));
            check("delete: the label names it", f.j.undo_label() == "Delete “Jade”", f.j.undo_label());
            const auto r = f.j.undo();
            check("delete: undo -- back in its place", names(f.s) == "Ruby,Jade,Onyx,", names(f.s));
            check("delete: undo -- the same id", f.s.ids()[1] == ids[1]);
            check("delete: undo hands back its selection", r.selection == std::vector<std::string>{ids[1]});
            const auto r2 = f.j.redo();
            check("delete: redo -- gone again, the neighbour selected",
                  names(f.s) == "Ruby,Onyx," && r2.selection == std::vector<std::string>{ids[2]});
        }

        {   // Created records: undo removes, redo brings back the same id.
            Fix f;
            const auto first = f.s.ids()[0];
            const auto made = core::stones::add(f.j, f.s, "Opal", first);
            check("add: just after the one given", names(f.s) == "Ruby,Opal,Jade,Onyx,", names(f.s));
            f.j.undo();
            check("add: undone", !f.s.get(made));
            f.j.redo();
            check("add: redone, same id, same place", f.s.get(made) && names(f.s) == "Ruby,Opal,Jade,Onyx,");
        }

        {   // A no-op is not a step.
            Fix f;
            const auto id = f.s.ids()[0];
            core::stones::rename(f.j, f.s, id, "Ruby");
            core::stones::remove(f.j, f.s, "nope");
            core::stones::move_up(f.j, f.s, id);   // already first
            check("no-op: nothing kept", f.j.size() == 0 && !f.j.can_undo());
        }

        {   // Merging: arrows on the slider fold into one step inside the window.
            Fix f;
            const auto id = f.s.ids()[0];
            const auto t0 = Clock::now();
            core::stones::resize(f.j, f.s, id, 4);
            f.j.run("Size", {id}, [&] { auto v = *f.s.get(id); v.size = 5; f.s.put(id, v); }, "size:" + id,
                    t0 + std::chrono::milliseconds(400));
            f.j.run("Size", {id}, [&] { auto v = *f.s.get(id); v.size = 6; f.s.put(id, v); }, "size:" + id,
                    t0 + std::chrono::milliseconds(800));
            check("merge: three presses, one step", f.j.size() == 1, std::to_string(f.j.size()));
            f.j.run("Size", {id}, [&] { auto v = *f.s.get(id); v.size = 7; f.s.put(id, v); }, "size:" + id,
                    t0 + std::chrono::seconds(10));
            check("merge: after the window, a new step", f.j.size() == 2);
            f.j.undo();
            f.j.undo();
            check("merge: two undos, back to the start", f.s.get(id)->size == 3);
            // Another stone's key never merges into this one's step.
            Fix g;
            const auto a = g.s.ids()[0], b = g.s.ids()[1];
            core::stones::resize(g.j, g.s, a, 5);
            core::stones::resize(g.j, g.s, b, 5);
            check("merge: a different key is a different step", g.j.size() == 2);
            // Nor into a step that was undone.
            core::stones::resize(g.j, g.s, b, 6);
            g.j.undo();
            core::stones::resize(g.j, g.s, b, 7);
            check("merge: never into an undone step", g.j.size() == 2 && g.j.can_undo() && !g.j.can_redo());
            // Back to where it started inside the window: the step goes away.
            Fix h;
            const auto c = h.s.ids()[0];
            core::stones::resize(h.j, h.s, c, 4);
            core::stones::resize(h.j, h.s, c, 3);
            check("merge: there and back again is no step", h.j.size() == 0);
        }

        {   // A gesture is one step, however many writes the drag made.
            Fix f;
            const auto id = f.s.ids()[0];
            core::stones::drag_begin(f.j, f.s, id);
            for (int n = 4; n <= 9; ++n) core::stones::drag_to(f.j, f.s, id, n);
            check("gesture: open while dragging", f.j.in_gesture() && f.j.size() == 0);
            core::stones::drag_end(f.j);
            check("gesture: a six-write drag is one step", f.j.size() == 1 && f.s.get(id)->size == 9);
            f.j.undo();
            check("gesture: one undo, back to the start", f.s.get(id)->size == 3);
            // Escape mid-drag: back, and nothing kept.
            core::stones::drag_begin(f.j, f.s, id);
            core::stones::drag_to(f.j, f.s, id, 7);
            f.j.cancel();
            check("gesture: cancel puts it back and keeps nothing",
                  f.s.get(id)->size == 3 && !f.j.in_gesture() && f.j.can_redo());
            // Ctrl+Z mid-drag: the drag becomes a step, then is undone.
            core::stones::drag_begin(f.j, f.s, id);
            core::stones::drag_to(f.j, f.s, id, 2);
            f.j.undo();
            check("gesture: undo mid-drag closes the drag, then undoes it",
                  !f.j.in_gesture() && f.s.get(id)->size == 3 && f.j.can_redo());
            check("gesture: no stray writes", f.s.strays() == 0);
        }

        {   // A new step forgets the redo branch; Move keeps both stones in one step.
            Fix f;
            const auto ids = f.s.ids();
            core::stones::move_down(f.j, f.s, ids[0]);
            check("move: one step for both stones", f.j.size() == 1 && names(f.s) == "Jade,Ruby,Onyx,", names(f.s));
            f.j.undo();
            check("move: undone", names(f.s) == "Ruby,Jade,Onyx,");
            core::stones::rename(f.j, f.s, ids[2], "Coal");
            check("redo branch: a new step forgets it", !f.j.can_redo() && f.j.size() == 1);
        }

        {   // The cap: the oldest step falls off; the newest still undoes.
            Fix f;
            const auto id = f.s.ids()[0];
            for (std::size_t i = 0; i < core::StoneJournal::kMax + 20; ++i)
                core::stones::rename(f.j, f.s, id, "n" + std::to_string(i));
            check("cap: kMax steps kept", f.j.size() == core::StoneJournal::kMax);
            f.j.undo();
            check("cap: the newest still undoes", f.s.get(id)->name == "n" + std::to_string(core::StoneJournal::kMax + 18));
        }

        {   // on_change: the UI hears every journal change (labels, greying).
            Fix f;
            int heard = 0;
            f.j.set_on_change([&] { ++heard; });
            core::stones::rename(f.j, f.s, f.s.ids()[0], "X");
            f.j.undo();
            f.j.redo();
            f.j.clear();
            check("on_change: step, undo, redo, clear -- four", heard == 4, std::to_string(heard));
        }
    }


    // ── io -- plist, GLIF, UFO round trips (Granjon M2) ─────────────────────
    {
        std::cout << "\n-- io --\n";
        namespace model = granjon::model;
        namespace io    = granjon::io;
        namespace fs    = std::filesystem;
        using model::Node;

        {   // plist: write -> read -> write is byte-identical, keys sorted.
            io::PValue d = io::PValue::dictionary();
            d.set("zeta", io::PValue::str("a < b & \"c\""));
            d.set("alpha", io::PValue::integer(-250));
            d.set("mid", io::PValue::real(12.5));
            d.set("flag", io::PValue::boolean(true));
            io::PValue arr = io::PValue::array();
            arr.arr = {io::PValue::str("x"), io::PValue::dictionary()};
            d.set("list", arr);
            const std::string a = io::write_plist(d);
            std::string err;
            const auto back = io::read_plist(a, &err);
            check("plist: reads back", back.has_value(), err);
            check("plist: byte-identical rewrite", back && io::write_plist(*back) == a);
            check("plist: keys sorted", a.find("<key>alpha</key>") < a.find("<key>zeta</key>"));
            check("plist: escaping survives", back && back->get_string("zeta") == std::string("a < b & \"c\""));
            check("plist: junk refused", !io::read_plist("<plist><dict><key>a</key></dict></plist>", &err));
        }

        // A glyph with every feature: curves, a smooth and a symmetric node,
        // an open contour, a component, anchors, note, status, fractions.
        model::Glyph rich;
        rich.name = "Odieresis";
        rich.unicodes = {0xD6, 0xF6};
        rich.advance = 760;
        rich.note = "check <the> overshoot & dots";
        rich.status = model::GlyphStatus::Review;
        rich.anchors = {{"top", 380, 720}, {"_bottom", 380.5, 0}};
        rich.components = {{"dieresiscomb", 1, 0, 0, 1, 120, 0}, {"O", -1, 0, 0, 1, 760, 0}};
        {
            Node a = Node::corner(380, -12), b = Node::corner(700, 350), c = Node::corner(380, 712),
                 d = Node::corner(60, 350);
            a.cx1 = 200; a.cy1 = -12; a.cx2 = 560; a.cy2 = -12; a.type = Node::Type::Smooth;
            b.cx1 = 700; b.cy1 = 150; b.cx2 = 700; b.cy2 = 550; b.type = Node::Type::Symmetric;
            c.cx1 = 560; c.cy1 = 712; c.cx2 = 200; c.cy2 = 712; c.type = Node::Type::Smooth;
            d.cx1 = 60;  d.cy1 = 550; d.cx2 = 60;  d.cy2 = 150.25; d.type = Node::Type::Cusp;
            model::Contour outer{{a, b, c, d}, true};
            model::Contour box{{Node::corner(10, 10), Node::corner(10, 20), Node::corner(20, 20)}, true};
            model::Contour open{{Node::corner(0, 0), Node::corner(50, 0)}, false};
            rich.contours = {outer, box, open};
        }
        {
            const std::string x = io::write_glif(rich);
            std::string err;
            const auto back = io::read_glif(x, &err);
            check("glif: reads back", back.has_value(), err);
            check("glif: round trip is exact (types, handles, open, components, anchors, note, status)",
                  back && *back == rich);
            check("glif: rewrite is byte-identical", back && io::write_glif(*back) == x);
            check("glif: start point first, closing off-curves last",
                  x.find("<point x=\"380\" y=\"-12\" type=\"curve\" smooth=\"yes\"/>") <
                  x.find("<point x=\"700\" y=\"350\""));
        }
        {   // Foreign GLIF: starts on off-curves; a TrueType qcurve.
            const std::string q =
                "<glyph name=\"q\" format=\"2\"><advance width=\"100\"/><outline><contour>"
                "<point x=\"0\" y=\"100\"/>"
                "<point x=\"0\" y=\"0\" type=\"qcurve\"/>"
                "<point x=\"100\" y=\"0\" type=\"line\"/>"
                "</contour></outline></glyph>";
            std::string err;
            const auto g = io::read_glif(q, &err);
            check("glif: qcurve contour reads", g && g->contours.size() == 1 && g->contours[0].nodes.size() == 2, err);
            if (g && g->contours[0].nodes.size() == 2) {
                const auto& n0 = g->contours[0].nodes[0];   // (0,0), reached by the quadratic from (100,0)
                const auto& n1 = g->contours[0].nodes[1];   // (100,0)
                // Quadratic (100,0) -q(0,100)-> (0,0): cubic c1 = p0 + 2/3(q-p0), c2 = p1 + 2/3(q-p1).
                check("glif: quadratic promoted to cubic exactly",
                      std::abs(n1.cx2 - 100.0 / 3) < 1e-9 && std::abs(n1.cy2 - 200.0 / 3) < 1e-9 &&
                      std::abs(n0.cx1 - 0) < 1e-9 && std::abs(n0.cy1 - 200.0 / 3) < 1e-9);
            }
            check("glif: not a glyph refused", !io::read_glif("<glyph format=\"2\"/>", &err) && !err.empty());
        }
        {   // File names (UFO 3 rule).
            std::vector<std::string> taken;
            check("names: uppercase marked", io::glif_file_name("A", taken) == "A_.glif");
            check("names: leading dot", io::glif_file_name(".notdef", taken) == "_notdef.glif");
            check("names: reserved", io::glif_file_name("con", taken) == "_con.glif");
            check("names: illegal chars", io::glif_file_name("a/b", taken) == "a_b.glif");
            taken = {"a_.glif"};
            check("names: case-insensitive clash gets a number", io::glif_file_name("A", taken) == "A_1.glif");
        }
        {   // The whole font: save -> load -> equal; save twice -> byte-identical.
            const fs::path dir = fs::temp_directory_path() / "granjon_selftest_io";
            fs::remove_all(dir);
            fs::create_directories(dir);
            model::Font f;
            granjon::command::make_fixture(f);
            {
                model::Font::Loading load(f);
                model::Glyph g = rich;
                g.order = 50;
                f.put(g.name, model::Record{g});
                model::Glyph dc;
                dc.name = "dieresiscomb"; dc.unicodes = {0x308}; dc.order = 51;
                f.put(dc.name, model::Record{dc});
                auto info = f.info();
                info.copyright = "(c) 2026 Scott"; info.license = "OFL"; info.vendor = "GRNJ";
                f.put(model::Font::kInfo, model::Record{info});
                f.put(model::Font::kFeatures, model::Record{model::Features{"feature liga { sub f i by fi; } liga;\n"}});
            }
            std::string err;
            const auto a = dir / "Test.granjon";
            check("ufo: saves", io::save_ufo(f, a.string(), &err), err);
            model::Font g;
            check("ufo: loads", io::load_ufo(a.string(), g, &err), err);
            check("ufo: same glyph order", g.glyph_order() == f.glyph_order());
            // Orders are renumbered 1..n on load; compare everything else.
            const auto norm = [](const model::Font& x) {
                auto s = x.snapshot();
                for (auto& [id, r] : s)
                    if (auto* gl = std::get_if<model::Glyph>(&r)) gl->order = 0;
                return s;
            };
            check("ufo: every record survives", norm(g) == norm(f));
            check("ufo: loading is not a stray", g.strays() == 0);
            const auto b = dir / "Again.granjon";
            io::save_ufo(g, b.string(), &err);
            bool same = true;
            std::string differ;
            for (const auto& e : fs::recursive_directory_iterator(a)) {
                if (!e.is_regular_file()) continue;
                const auto rel = fs::relative(e.path(), a);
                std::ifstream x(e.path(), std::ios::binary), y(b / rel, std::ios::binary);
                std::string sx((std::istreambuf_iterator<char>(x)), {}), sy((std::istreambuf_iterator<char>(y)), {});
                if (sx != sy) { same = false; differ = rel.string(); }
            }
            check("ufo: save -> load -> save is byte-identical", same, differ);
            check("ufo: expected files", fs::exists(a / "metainfo.plist") && fs::exists(a / "layercontents.plist") &&
                                         fs::exists(a / "glyphs" / "contents.plist") &&
                                         fs::exists(a / "glyphs" / "_notdef.glif") && fs::exists(a / "features.fea"));
            // Re-save after a delete: the stale .glif must go (the swap-in).
            {
                model::Font::Loading load(g);
                g.put("H", std::nullopt);
            }
            io::save_ufo(g, a.string(), &err);
            check("ufo: a deleted glyph's file is gone after save", !fs::exists(a / "glyphs" / "H_.glif"));
            check("ufo: no temp folders left", !fs::exists(a.string() + ".saving") && !fs::exists(a.string() + ".previous"));
            // A failed load leaves the open font alone.
            model::Font keep;
            granjon::command::make_fixture(keep);
            const auto before = keep.snapshot();
            check("ufo: not a folder is refused, with a reason", !io::load_ufo((dir / "nope").string(), keep, &err) && !err.empty(), err);
            fs::create_directories(dir / "Broken.granjon");
            check("ufo: no metainfo is refused", !io::load_ufo((dir / "Broken.granjon").string(), keep, &err), err);
            check("ufo: a failed load changes nothing", keep.snapshot() == before);
            fs::remove_all(dir);
        }
        {   // Document: modified tracks the saved state through undo / redo.
            namespace fs = std::filesystem;
            granjon::command::Document d;
            check("doc: fresh is unmodified", !d.modified());
            granjon::command::set_family(d.journal, d.font, "Pica");
            check("doc: an edit modifies", d.modified());
            d.journal.undo();
            check("doc: undo back to the saved state is unmodified", !d.modified());
            d.journal.redo();
            const auto p = fs::temp_directory_path() / "granjon_selftest_doc.granjon";
            std::string err;
            check("doc: save", d.save(p.string(), &err) && !d.modified() && d.path == p.string(), err);
            d.journal.undo();
            check("doc: undo past the save is modified", d.modified());
            d.journal.redo();
            check("doc: redo to the save is unmodified", !d.modified());
            granjon::command::set_metric(d.journal, d.font, granjon::model::Metrics::Field::XHeight, 480);
            granjon::command::set_metric(d.journal, d.font, granjon::model::Metrics::Field::XHeight, 470);   // merges
            d.save({}, &err);
            granjon::command::set_metric(d.journal, d.font, granjon::model::Metrics::Field::XHeight, 460);   // merges into the saved step
            check("doc: a merge into the saved step still modifies", d.modified());
            granjon::command::Document e;
            check("doc: open", e.open(p.string(), &err) && !e.modified() && e.journal.size() == 0 &&
                               e.font.info().family == "Pica" && e.font.metrics().x_height == 470, err);
            e.new_font();
            check("doc: new font", e.path.empty() && !e.modified() && e.font.glyph_count() == 2);
            fs::remove_all(p);
        }
    }

    std::cout << "-----------------------------------------------\n";

    // -- Settings (M4b): typed, tolerant, round-trips ---------------------------
    {
        namespace stdfs = std::filesystem;
        const std::string file = (stdfs::temp_directory_path() / "granjon_selftest_settings" / "settings.json").string();
        stdfs::remove_all(stdfs::path(file).parent_path());
        check("settings: no file -> defaults", core::load_settings(file) == core::Settings{});
        core::Settings s;
        s.reopen_last = false;
        s.last_font = "/fonts/My-Termes-Regular.granjon";
        s.open_folder = "/fonts";
        s.editor_fill = false;
        s.context_upper = "O";
        s.sidebearing_mod = "ctrl";
        s.overview_cell = 120;
        s.overview_charset = "latin-1";
        s.editor_grid = true;
        s.editor_snap = false;
        s.handles_relative = false;
        s.navigator_open = {"Basic Latin", "Unencoded"};
        check("settings: save makes the folder", core::save_settings(file, s));
        check("settings: round-trip", core::load_settings(file) == s);
        check("settings: no temp file left", !stdfs::exists(file + ".tmp"));
        {
            std::ofstream f(file, std::ios::trunc);
            f << R"({"editor": {"fill": "yes", "sidebearing_modifier": "super", "context_lower": ""},
                     "overview": {"cell": 9000}, "future": {"thing": 1}})";
        }
        const auto t = core::load_settings(file);
        check("settings: wrong type -> default", t.editor_fill == true);
        check("settings: unknown modifier -> default", t.sidebearing_mod == "alt");
        check("settings: empty context -> default", t.context_lower == "n");
        check("settings: out-of-range cell -> default", t.overview_cell == 84);
        check("settings: missing section -> defaults", t.reopen_last && t.last_font.empty());
        { std::ofstream f(file, std::ios::trunc); f << "{ not json"; }
        check("settings: garbage -> defaults, no throw", core::load_settings(file) == core::Settings{});
        stdfs::remove_all(stdfs::path(file).parent_path());
    }

    // -- Recents: list ops + JSON round-trip with prune -----------------------
    {
        std::vector<std::string> l;
        core::recents_add(l, "/a");
        core::recents_add(l, "/b");
        core::recents_add(l, "/c");
        check("recents: newest first", l.size() == 3 && l.front() == "/c");
        core::recents_add(l, "/a");
        check("recents: re-add moves to front (no dup)",
              l.size() == 3 && l.front() == "/a");
        core::recents_add(l, "/x/");
        check("recents: trailing slash normalised",
              std::find(l.begin(), l.end(), "/x") != l.end() &&
              std::find(l.begin(), l.end(), "/x/") == l.end());
        std::vector<std::string> cap;
        for (int i = 0; i < 20; ++i)
            core::recents_add(cap, "/p" + std::to_string(i), 8);
        check("recents: trim to max (8)", cap.size() == 8 && cap.front() == "/p19");

        const std::string base =
            (std::filesystem::temp_directory_path() / "granjon_selftest_recents").string();
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
        const std::string live = base + "/live";
        std::filesystem::create_directories(live, ec);
        const std::string file = base + "/recent.json";

        std::vector<std::string> to_save = {live, base + "/GONE"};
        check("recents: save ok", core::save_recents(file, to_save));
        auto loaded = core::load_recents(file);
        check("recents: load prunes the missing path",
              loaded.size() == 1 && loaded.front() == live);
        check("recents: missing file -> empty",
              core::load_recents(base + "/nope.json").empty());
    }

    // -- TextMap: forward/inverse round-trip ----------------------------------
    {
        // A scene as a viewer flattens it:
        //   0 "The Cellar"     title  (non-prose)
        //   1 ""               gap    (non-prose)
        //   2 "She opened the" prose, para_start   <- para 1, wrap 1
        //   3 "old iron door." prose               <- para 1, wrap 2
        //   4 "Caf\u00e9 dust." prose, para_start  <- para 2 (UTF-8: é)
        std::vector<core::FlatLine> flat = {
            {"The Cellar",     false, false},
            {"",               false, false},
            {"She opened the", true,  true },
            {"old iron door.", true,  false},
            {"Caf\xC3\xA9 dust.", true, true },
        };
        roundtrip(flat, 2, 0, 2, 14, "single line");
        roundtrip(flat, 2, 4, 3, 8,  "multi-line (soft wrap)");
        roundtrip(flat, 2, 0, 4, 10, "cross-paragraph");
        roundtrip(flat, 4, 0, 4, 6,  "UTF-8 span (accented)");

        // Chrome is never landed on: a range covering all prose starts on line 2.
        std::vector<int> off;
        (void)core::visible_text(flat, off);
        core::LineCol s, e;
        core::map_range(flat, 0, 3, s, e);
        check("textmap: start snaps to prose, never chrome", s.line == 2);

        // Degenerate / empty inputs fail cleanly.
        check("textmap: empty range rejected", !core::map_range(flat, 5, 5, s, e));
        std::vector<core::FlatLine> no_prose = {{"Title", false, false}};
        check("textmap: no prose -> false", !core::map_range(no_prose, 0, 3, s, e));
    }
    // -- Shortcuts: the registry paradigm, exercised ---------------------------
    // A seed's claim isn't a fact until a consumer exercises it. The registry's
    // whole promise is that a key and its advertised row can't drift, because
    // both read one list -- so the guard that matters is pure and lives here:
    // no two chords may collide. (App wires from this same list; ShortcutsDialog
    // renders from it.)
    {
        namespace sc = granjon::core;

        check("shortcuts: format_accel renders modifiers + key",
              sc::format_accel("<Ctrl><Shift>n") == "Ctrl+Shift+N",
              sc::format_accel("<Ctrl><Shift>n"));
        check("shortcuts: format_accel maps a named key to its glyph",
              sc::format_accel("<Ctrl>question") == "Ctrl+?",
              sc::format_accel("<Ctrl>question"));

        {
            sc::ShortcutSpec derived; derived.accels = {"<Ctrl>question", "<Ctrl>slash"};
            check("shortcuts: display_keys derives + joins accels",
                  derived.display_keys() == "Ctrl+?  /  Ctrl+/",
                  derived.display_keys());
            sc::ShortcutSpec doc; doc.keys = "Click a recent";
            check("shortcuts: display_keys prefers an explicit key string",
                  doc.display_keys() == "Click a recent");
        }

        check("shortcuts: registry is populated",
              !sc::shortcut_registry().empty(),
              std::to_string(sc::shortcut_registry().size()) + " specs");
        check("shortcuts: registry is accel-collision-free",
              sc::find_accel_collisions().empty(),
              sc::find_accel_collisions().empty()
                  ? "clean" : sc::find_accel_collisions().front());

        // Sections authored A-Z + contiguous: the dialog walks linearly and
        // starts a heading on change, so a stray out-of-order row would split a
        // section into two headings.
        {
            std::vector<std::string> secs;
            for (const auto& r : sc::shortcut_registry())
                if (secs.empty() || secs.back() != r.section) secs.push_back(r.section);
            std::vector<std::string> sorted = secs;
            std::sort(sorted.begin(), sorted.end());
            check("shortcuts: sections are contiguous and A-Z ordered", secs == sorted);
        }

        // The doc-only half of the paradigm: a gesture row documents without
        // binding (no accels), but must still show keys.
        {
            bool ok = true, saw_doc_only = false;
            for (const auto& r : sc::shortcut_registry())
                if (r.action.empty()) {
                    saw_doc_only = true;
                    if (!r.accels.empty() || r.keys.empty()) ok = false;
                }
            check("shortcuts: a doc-only row binds nothing but still displays keys",
                  ok && saw_doc_only);
        }
    }


    // ── Font -- the store, the verbs, the one-door gate (Granjon M1) ─────────
    {
        std::cout << "\n-- font --\n";
        namespace model = granjon::model;
        namespace cmd   = granjon::command;
        namespace io    = granjon::io;
        using model::Font;
        using model::FontJournal;
        struct Fix {
            Font        f;
            FontJournal j{f};
            Fix() { f.set_door([this] { return j.open(); }); cmd::make_fixture(f); }
        };

        {   // A new font: .notdef and space, nothing undoable, no strays.
            cmd::Document d;
            check("new font: .notdef + space", d.font.glyph_order() == std::vector<std::string>{".notdef", "space"});
            check("new font: loading is not a step", d.journal.size() == 0 && !d.journal.can_undo());
            check("new font: loading is not a stray", d.font.strays() == 0);
            check("new font: space maps U+0020", d.font.glyph_for(U' ') && d.font.glyph_for(U' ')->name == "space");
        }

        // THE GATE: every verb in the table, on a fresh fixture -- through the
        // door, one step, undo restores exactly, redo re-applies exactly.
        for (const auto& v : cmd::verbs()) {
            Fix x;
            const auto before = x.f.snapshot();
            v.run(x.j, x.f);
            const auto after = x.f.snapshot();
            check("verb " + v.name + ": no stray write", x.f.strays() == 0, std::to_string(x.f.strays()));
            check("verb " + v.name + ": changed something, as one step", after != before && x.j.size() == 1,
                  std::to_string(x.j.size()) + " step(s)");
            x.j.undo();
            check("verb " + v.name + ": undo restores", x.f.snapshot() == before);
            x.j.redo();
            check("verb " + v.name + ": redo re-applies", x.f.snapshot() == after);
        }

        {   // The gate catches a write that skips the door.
            Fix x;
            model::Glyph g = *x.f.glyph("H");
            g.advance = 1;
            x.f.put("H", model::Record{g});
            check("gate: a write outside the door is a stray", x.f.strays() == 1);
        }

        {   // Rename carries references: component, kerning pair, group member.
            Fix x;
            cmd::rename_glyph(x.j, x.f, "O", "O.alt");
            const auto& k = x.f.kerning();
            check("rename: component follows", x.f.glyph("Odotbelow")->components[0].base == "O.alt");
            check("rename: kerning pair follows", k.pairs.count({"H", "O.alt"}) == 1 && k.pairs.count({"H", "O"}) == 0);
            const auto& grp = k.groups.at("public.kern1.O");
            check("rename: group member follows", std::find(grp.begin(), grp.end(), "O.alt") != grp.end());
            check("rename: keeps its place in the order", x.f.glyph_order()[3] == "O.alt", x.f.glyph_order()[3]);
            x.j.undo();
            check("rename: undo brings every reference back",
                  x.f.glyph("O") && !x.f.glyph("O.alt") && x.f.glyph("Odotbelow")->components[0].base == "O");
        }

        {   // Refusals are not steps, and they say why.
            Fix x;
            std::string why;
            check("refuse: delete a glyph used as a component",
                  !cmd::remove_glyph(x.j, x.f, "O", &why) && x.j.size() == 0 && !why.empty(), why);
            why.clear();
            check("refuse: a bad glyph name", !cmd::add_glyph(x.j, x.f, "9lives", {}, {}, &why) && !why.empty());
            check("refuse: a duplicate glyph name", !cmd::add_glyph(x.j, x.f, "H") && x.j.size() == 0);
            check("refuse: a value set to itself is not a step", !cmd::set_advance(x.j, x.f, "H", 700) && x.j.size() == 0);
        }

        {   // Delete + undo: back in the same slot.
            Fix x;
            const auto order = x.f.glyph_order();
            cmd::remove_glyph(x.j, x.f, "n");
            x.j.undo();
            check("delete/undo: same slot in the glyph order", x.f.glyph_order() == order);
        }

        {   // A drag is one step, and lands on whole units.
            Fix x;
            cmd::drag_begin(x.j, x.f, {"o"});
            for (int i = 1; i <= 10; ++i) cmd::drag_translate(x.j, x.f, i * 1.37, -i * 0.61);
            check("drag: mid-drag coordinates are fractional", !x.f.glyph("o")->is_integral());
            cmd::drag_end(x.j, x.f);
            check("drag: one step for the whole drag", x.j.size() == 1);
            check("drag: snapped to integer units on release", x.f.glyph("o")->is_integral());
            const auto& n0 = x.f.glyph("o")->contours[0].nodes[0];
            check("drag: moved by the rounded total", n0.x == 64 && n0.y == -16,
                  std::to_string(n0.x) + "," + std::to_string(n0.y));
            cmd::drag_begin(x.j, x.f, {"o"});
            cmd::drag_translate(x.j, x.f, 50, 50);
            cmd::drag_cancel(x.j);
            check("drag: Escape puts it back, keeps nothing", x.j.size() == 1 && x.f.glyph("o")->contours[0].nodes[0].x == 64);
        }

        // ── M4: the editor's geometry and drags ─────────────────────────────
        {   // Curvz's segment maths, lifted.
            model::Contour c;
            c.nodes = {model::Node::corner(0, 0), model::Node::corner(100, 0), model::Node::corner(100, 100)};
            c.closed = true;
            check("geom: a closed 3-node contour has 3 segments", geom::segment_count(c) == 3);
            c.closed = false;
            check("geom: an open one has 2", geom::segment_count(c) == 2);
            const auto seg = geom::segment(c, 0);
            check("geom: a line segment's midpoint", seg.at(0.5) == geom::Vec2(50, 0));
            check("geom: nearest point on a line", seg.at(seg.nearest_t({30, 40})).dist({30, 0}) < 1e-4);
            const auto halves = seg.split(0.25);
            check("geom: split meets at t", halves.first.p3 == seg.at(0.25) && halves.second.p0 == seg.at(0.25));
        }
        {   // Node-type rules on a handle drag.
            using T = model::Node::Type;
            auto make = [](T t) {
                model::Contour c;
                c.closed = false;
                c.nodes = {model::Node::corner(0, 0), {100, 0, 70, 0, 130, 0, t}, model::Node::corner(200, 0)};
                return c;
            };
            auto smooth = make(T::Smooth);
            geom::move_handle_out(smooth, 1, {100 + 30, 40});
            const auto& s1 = smooth.nodes[1];
            const double in_len = std::hypot(s1.cx1 - 100, s1.cy1);
            check("smooth: the other handle stays collinear",
                  std::abs((s1.cx1 - 100) * 40 - s1.cy1 * 30) < 1e-6 && s1.cy1 < 0);
            check("smooth: ... and keeps its length", std::abs(in_len - 30) < 1e-6);
            auto sym = make(T::Symmetric);
            geom::move_handle_in(sym, 1, {80, 20});
            check("symmetric: the other handle mirrors", sym.nodes[1].cx2 == 120 && sym.nodes[1].cy2 == -20);
            auto cusp = make(T::Cusp);
            geom::move_handle_out(cusp, 1, {160, 25});
            check("cusp: a handle drag changes length only",
                  cusp.nodes[1].cy2 == 0 && cusp.nodes[1].cx2 == 160 && cusp.nodes[1].cx1 == 70);
            auto corner = make(T::Corner);
            geom::move_handle_out(corner, 1, {150, 50});
            check("corner: the other handle stays put", corner.nodes[1].cx1 == 70 && corner.nodes[1].cy1 == 0);
        }
        {   // Picking: nodes first, then handles, anchors, segments; the marquee.
            model::Glyph g;
            model::Contour c;
            c.nodes = {model::Node::corner(0, 0), {100, 0, 100, 0, 100, 40, model::Node::Type::Corner},
                       model::Node::corner(100, 100), model::Node::corner(0, 100)};
            g.contours = {c};
            g.anchors = {{"top", 50, 120}};
            check("hit: a node", geom::hit_test(g, {101, 1}, 5).kind == geom::Hit::Kind::Node);
            check("hit: a handle", geom::hit_test(g, {102, 40}, 5).kind == geom::Hit::Kind::HandleOut);
            check("hit: no handles when hidden", geom::hit_test(g, {102, 40}, 5, false).kind == geom::Hit::Kind::Segment);
            check("hit: an anchor", geom::hit_test(g, {50, 118}, 5).kind == geom::Hit::Kind::Anchor);
            const auto seg = geom::hit_test(g, {50, 2}, 5);
            check("hit: a segment, and which", seg.kind == geom::Hit::Kind::Segment && seg.index == 0);
            check("hit: nothing far away", geom::hit_test(g, {50, 50}, 5).kind == geom::Hit::Kind::None);
            const auto box = geom::points_in(g, {-5, -5}, {105, 50});
            check("marquee: two nodes", box.size() == 2 && box.count({0, 0, geom::PointRef::Part::Node}));
            const auto all = geom::points_in(g, {-5, -5}, {105, 130});
            check("marquee: nodes and the anchor", all.size() == 5);
            model::Glyph moved = g;
            geom::move_points(moved, g, {{0, 1, geom::PointRef::Part::Node}, {-1, 0, geom::PointRef::Part::Anchor}}, {10, 5});
            const auto& n1 = moved.contours[0].nodes[1];
            check("move: a picked node carries its handle", n1.x == 110 && n1.y == 5 && n1.cx2 == 110 && n1.cy2 == 45);
            check("move: the anchor too", moved.anchors[0].x == 60 && moved.anchors[0].y == 125);
            check("move: the rest stays", moved.contours[0].nodes[0] == g.contours[0].nodes[0]);
        }
        {   // M5b: node types, free handles, the grid, snapping.
            using T = model::Node::Type;
            model::Contour c;
            c.closed = false;
            c.nodes = {model::Node::corner(0, 0), {100, 0, 70, 10, 140, 20, T::Corner}, model::Node::corner(200, 0)};
            auto s = c;
            geom::set_node_type(s, 1, T::Smooth);
            const auto& n = s.nodes[1];
            check("type: corner -> smooth lines the handles up",
                  std::abs((n.cx1 - 100) * (n.cy2 - 0) - (n.cy1 - 0) * (n.cx2 - 100)) < 1e-6 && n.type == T::Smooth);
            check("type: ... keeping their lengths", std::abs(std::hypot(n.cx1 - 100, n.cy1) - std::hypot(30, 10)) < 1e-6 &&
                  std::abs(std::hypot(n.cx2 - 100, n.cy2) - std::hypot(40, 20)) < 1e-6);
            auto bare = c;
            bare.nodes[1] = model::Node::corner(100, 50);
            geom::set_node_type(bare, 1, T::Symmetric);
            check("type: a bare node going symmetric gets seeded handles", bare.nodes[1].has_in() && bare.nodes[1].has_out() &&
                  std::abs((bare.nodes[1].cx1 + bare.nodes[1].cx2) / 2 - 100) < 1e-9);
            auto corner = s;
            geom::set_node_type(corner, 1, T::Corner);
            check("type: -> corner leaves the handles", corner.nodes[1].cx1 == s.nodes[1].cx1 && corner.nodes[1].type == T::Corner);

            model::Glyph g;
            g.contours = {s};
            model::Glyph moved = g;
            geom::move_points(moved, g, {{0, 1, geom::PointRef::Part::Out}}, {0, 30}, true);
            check("free handle: the other handle stays", moved.contours[0].nodes[1].cx1 == g.contours[0].nodes[1].cx1 &&
                  moved.contours[0].nodes[1].cy1 == g.contours[0].nodes[1].cy1);
            check("free handle: the node turns Corner", moved.contours[0].nodes[1].type == T::Corner);

            check("grid: tenths when zoomed in", geom::grid_step(100) == 0.1);
            check("grid: whole units at 10 px/unit", geom::grid_step(10) == 1);
            check("grid: 20s at 0.5 px/unit", geom::grid_step(0.5) == 20);
            std::optional<double> hit;
            check("snap: to a target in reach", geom::snap_axis(497, {500, 0}, 10, 5, &hit) == 500 && hit && *hit == 500);
            check("snap: the grid when no target is near", geom::snap_axis(203, {500}, 10, 5, &hit) == 200 && hit);
            check("snap: nothing in reach", geom::snap_axis(204.5, {}, 0, 3, &hit) == 204.5 && !hit);
            check("axis lock: the larger component", geom::axis_lock({30, -4}) == geom::Vec2(30, 0) &&
                  geom::axis_lock({3, -40}) == geom::Vec2(0, -40));

            Fix x;
            const geom::Points pts{{0, 0, geom::PointRef::Part::Node}, {0, 1, geom::PointRef::Part::Node}};
            check("set_node_type: one step", cmd::set_node_type(x.j, x.f, "o", pts, T::Symmetric) && x.j.size() == 1 &&
                  x.f.glyph("o")->contours[0].nodes[0].type == T::Symmetric);
            check("set_node_type: again is no step", !cmd::set_node_type(x.j, x.f, "o", pts, T::Symmetric));
        }
        {   // M5c: guides, alignment, measuring.
            std::string why;
            Fix x;
            const auto o0 = *x.f.glyph("o");
            check("guides: the fixture's", o0.guides.size() == 2 && x.f.glyph("H")->guides.size() == 1);
            check("guide: add to the font", cmd::add_guide(x.j, x.f, "", {300, 0, 90, "centre"}) &&
                  x.f.info().guides.size() == 1 && x.f.info().guides[0].vertical());
            check("guide: set rounds outside a drag", cmd::set_guide(x.j, x.f, "o", 0, {0, 260.4, 0, "middle"}) &&
                  x.f.glyph("o")->guides[0].y == 260 && x.f.glyph("o")->guides[0].name == "middle");
            const std::size_t before = x.j.size();
            cmd::guide_drag_begin(x.j, x.f, "o", 1);
            cmd::set_guide(x.j, x.f, "o", 1, {0, 117.6, 0, {}});
            check("guide drag: fractional mid-drag", x.f.glyph("o")->guides[1].y == 117.6);
            cmd::set_guide(x.j, x.f, "o", 1, {0, 131.4, 0, {}});
            cmd::drag_end(x.j, x.f);
            check("guide drag: one step, whole units", x.j.size() == before + 1 && x.f.glyph("o")->guides[1].y == 131);
            int at = -1;
            check("guide: to the whole font", cmd::move_guide_owner(x.j, x.f, "H", true, 0, &at) &&
                  x.f.glyph("H")->guides.empty() && x.f.info().guides.size() == 2 && at == 1);
            x.j.undo();
            check("guide: ... undone, both records", x.f.glyph("H")->guides.size() == 1 && x.f.info().guides.size() == 1);
            check("guide: removed", cmd::remove_guide(x.j, x.f, "n", 0) && x.f.glyph("n")->guides.empty());
            check("guide: a bad index is refused", !cmd::remove_guide(x.j, x.f, "n", 3));

            // GLIF + fontinfo round-trip.
            model::Glyph g = o0;
            g.guides = {{0, 480, 0, "xh"}, {270, 0, 90, {}}, {100, 200, 45, "diag"}};
            const auto back = io::read_glif(io::write_glif(g));
            check("glif: guidelines round-trip", back && back->guides == g.guides);
            namespace stdfs = std::filesystem;
            const auto dir = stdfs::temp_directory_path() / "granjon_selftest_guides.granjon";
            stdfs::remove_all(dir);
            std::string err;
            check("ufo: saves font guides", io::save_ufo(x.f, dir.string(), &err), err);
            Font y;
            check("ufo: reads them back", io::load_ufo(dir.string(), y, &err) && y.info().guides == x.f.info().guides &&
                  y.glyph("o")->guides == x.f.glyph("o")->guides, err);
            stdfs::remove_all(dir);

            // Align / distribute (H's nodes).
            using P = geom::PointRef::Part;
            const geom::Points three{{0, 0, P::Node}, {1, 0, P::Node}, {2, 0, P::Node}};   // x 80, 530, 170
            check("align: left", cmd::align_points(x.j, x.f, "H", three, cmd::Align::Left));
            const auto& H = *x.f.glyph("H");
            check("align: all at the leftmost x", H.contours[0].nodes[0].x == 80 && H.contours[1].nodes[0].x == 80 &&
                  H.contours[2].nodes[0].x == 80);
            x.j.undo();
            check("distribute: across", cmd::align_points(x.j, x.f, "H", three, cmd::Align::DistributeH));
            const auto& H2 = *x.f.glyph("H");
            check("distribute: evenly between the outer two", H2.contours[0].nodes[0].x == 80 &&
                  H2.contours[2].nodes[0].x == 305 && H2.contours[1].nodes[0].x == 530,
                  std::to_string(H2.contours[2].nodes[0].x));
            check("align: one point is no alignment", !cmd::align_points(x.j, x.f, "H", {{0, 0, P::Node}}, cmd::Align::Left));

            // Measuring.
            check("guide distance: horizontal", geom::guide_distance({0, 100, 0, {}}, {37, 130}) == 30);
            check("guide distance: vertical", std::abs(geom::guide_distance({50, 0, 90, {}}, {20, 999}) - 30) < 1e-9);
            const auto cs = geom::all_contours(x.f, *x.f.glyph("n"));   // stems 70..150, 410..490
            const auto stem = geom::stem_at(cs, {100, 250}, true);
            check("stem: across the left stem", stem && std::abs(stem->first - 70) < 1e-6 && std::abs(stem->second - 150) < 1e-6);
            check("stem: in the counter, between the stems", geom::stem_at(cs, {300, 250}, true).has_value());
            check("stem: outside the drawing", !geom::stem_at(cs, {600, 250}, true));
            check("crossings: four through the n", geom::crossings(cs, true, 250).size() == 4);
        }
        {   // M5d: contour direction.
            Fix x;
            const auto& O = *x.f.glyph("O");
            check("direction: an outer box is counter-clockwise", geom::signed_area(O.contours[0]) > 0);
            check("direction: depth -- outer 0, counter 1", geom::depth(O.contours, 0) == 0 && geom::depth(O.contours, 1) == 1);
            check("direction: the fixture's counter runs the wrong way", geom::wrong_way(O.contours) == std::vector<int>{1});
            model::Contour c = O.contours[0];
            const auto start = c.nodes[0];
            geom::reverse(c);
            check("reverse: clockwise now", geom::signed_area(c) < 0);
            check("reverse: the start node stays first", c.nodes[0].x == start.x && c.nodes[0].y == start.y);
            check("reverse: next becomes the old last", c.nodes[1] == O.contours[0].nodes[3]);
            geom::reverse(c);
            check("reverse: twice is the original", c == O.contours[0]);
            model::Contour curve;   // handles swap with the order
            curve.nodes = {{0, 0, 0, 0, 30, 0, model::Node::Type::Corner}, {100, 50, 100, 20, 100, 50, model::Node::Type::Corner},
                           {0, 100, 0, 100, 0, 100, model::Node::Type::Corner}};
            auto rc = curve;
            geom::reverse(rc);
            // The old segment 0 -> 1 (handles (30,0), (100,20)) now runs 1 -> 0, which is
            // index 2 -> index 0 after the start is kept: out (100,20), in (30,0).
            check("reverse: the curve keeps its shape", rc.nodes[2].cx2 == 100 && rc.nodes[2].cy2 == 20 &&
                  rc.nodes[0].cx1 == 30 && rc.nodes[0].cy1 == 0 &&
                  std::abs(geom::signed_area(rc) + geom::signed_area(curve)) < 1e-6);
            std::size_t n = 0;
            check("correct: one step fixes O's counter", cmd::correct_directions(x.j, x.f, x.f.glyph_order(), &n) && n == 1 &&
                  x.j.size() == 1 && geom::wrong_way(x.f.glyph("O")->contours).empty());
            check("correct: again, nothing to do", !cmd::correct_directions(x.j, x.f, x.f.glyph_order()));
            check("reverse_contours: one step", cmd::reverse_contours(x.j, x.f, "o", {0}) && x.j.size() == 2 &&
                  geom::signed_area(x.f.glyph("o")->contours[0]) < 0);
            check("reverse_contours: a bad index is refused", !cmd::reverse_contours(x.j, x.f, "o", {9}));
        }
        {   // M5e: make first -- a rotation; shape, handles, direction kept.
            Fix x;
            const model::Contour c0 = x.f.glyph("o")->contours[0];
            const auto n = static_cast<int>(c0.nodes.size());
            model::Contour c = c0;
            check("set_start: rotates", geom::set_start(c, 2) && c.nodes[0] == c0.nodes[2] &&
                  c.nodes[n - 2] == c0.nodes[0] && c.nodes.size() == c0.nodes.size());
            check("set_start: direction kept", std::abs(geom::signed_area(c) - geom::signed_area(c0)) < 1e-6);
            check("set_start: the start itself is nothing to do", !geom::set_start(c, 0));
            model::Contour open = c0;
            open.closed = false;
            check("set_start: an open contour is refused", !geom::set_start(open, 1));
            std::string why;
            check("make_first: one step, the node leads", cmd::make_first(x.j, x.f, "o", 0, 1, &why) &&
                  x.j.size() == 1 && x.f.glyph("o")->contours[0].nodes[0] == c0.nodes[1]);
            check("make_first: the start again is refused, with a reason",
                  !cmd::make_first(x.j, x.f, "o", 0, 0, &why) && !why.empty() && x.j.size() == 1);
            check("make_first: a bad index is refused", !cmd::make_first(x.j, x.f, "o", 0, 99) &&
                  !cmd::make_first(x.j, x.f, "o", 7, 1));
            x.j.undo();
            check("make_first: undo restores the start", x.f.glyph("o")->contours[0] == c0);
        }
        {   // M6: the strip text, kerning lookup, layout.
            Fix x;
            using V = std::vector<std::string>;
            std::vector<std::string> miss;
            check("strip: characters map", model::parse_strip(x.f, "nHo") == V{"n", "H", "o"});
            check("strip: /names, a space ends one", model::parse_strip(x.f, "/n/o /H") == V{"n", "o", "H"});
            check("strip: /name then a character", model::parse_strip(x.f, "/Odotbelow H") == V{"Odotbelow", "H"});
            check("strip: unknowns skipped and listed",
                  model::parse_strip(x.f, "nx/zzz", &miss) == V{"n"} && miss == V{"x", "/zzz"});
            const auto& k = x.f.kerning();
            check("kern_group: member / none", model::kern_group(k, "O", 1) == "public.kern1.O" &&
                  model::kern_group(k, "H", 1).empty() && model::kern_group(k, "O", 2).empty());
            const auto oh = model::kern_lookup(k, "O", "H");
            check("kern_lookup: through the group", oh.found && oh.value == -10 && oh.key.first == "public.kern1.O");
            check("kern_lookup: a group member too", model::kern_lookup(k, "Odotbelow", "H").value == -10);
            check("kern_lookup: glyph pair", model::kern_lookup(k, "H", "O").value == -15);
            check("kern_lookup: none", !model::kern_lookup(k, "n", "o").found);
            check("kern_target: the entry in effect", model::kern_target(k, "O", "H", false) ==
                  model::KernKey{"public.kern1.O", "H"});
            check("kern_target: exception", model::kern_target(k, "O", "H", true) == model::KernKey{"O", "H"});
            check("kern_target: no groups, itself", model::kern_target(k, "n", "o", false) == model::KernKey{"n", "o"});
            check("kern_kind", model::kern_kind({"public.kern1.O", "H"}) == "group / glyph" &&
                  model::kern_kind({"O", "H"}) == "glyph / glyph");
            const auto lay = model::layout_strip(x.f, {"H", "O", "H"});
            check("layout: advances + kerning", lay.size() == 3 && lay[0].x == 0 && lay[1].kern == -15 &&
                  lay[1].x == 685 && lay[2].kern == -10 && lay[2].x == 685 + 760 - 10);
        }
        {   // M6: kern_pair writes the entry in effect / an exception; 0 removes.
            Fix x;
            check("kern_pair: group entry, one step", cmd::kern_pair(x.j, x.f, "Odotbelow", "H", -30) &&
                  x.j.size() == 1 && model::kern_lookup(x.f.kerning(), "O", "H").value == -30);
            check("kern_pair: same value, nothing", !cmd::kern_pair(x.j, x.f, "O", "H", -30));
            check("kern_pair: Alt -> exception, the group untouched", cmd::kern_pair(x.j, x.f, "O", "H", -5, true) &&
                  model::kern_lookup(x.f.kerning(), "O", "H").value == -5 &&
                  model::kern_lookup(x.f.kerning(), "Odotbelow", "H").value == -30);
            check("kern_pair: the exception is now in effect",
                  model::kern_target(x.f.kerning(), "O", "H", false) == model::KernKey{"O", "H"});
            check("kern_pair: 0 removes", cmd::kern_pair(x.j, x.f, "O", "H", 0) &&
                  x.f.kerning().pairs.count({"O", "H"}) == 0 &&
                  model::kern_lookup(x.f.kerning(), "O", "H").value == -30);
            check("kern_pair: new pair, no groups", cmd::kern_pair(x.j, x.f, "n", "o", 12) &&
                  x.f.kerning().pairs.at({"n", "o"}) == 12);
        }
        {   // M6: metrics keys -- expressions, rules, evaluation, apply.
            Fix x;
            auto e = model::parse_key_expr("60");
            check("key expr: number", e.valid && e.ref.empty() && e.number == 60);
            e = model::parse_key_expr("=n");
            check("key expr: =n", e.valid && e.ref == "n" && e.side == 0);
            e = model::parse_key_expr("|n");
            check("key expr: |n", e.valid && e.ref == "n" && e.side == '|');
            e = model::parse_key_expr("n.R");
            check("key expr: n.R", e.valid && e.ref == "n" && e.side == 'R');
            e = model::parse_key_expr("=n+10");
            check("key expr: =n+10", e.valid && e.ref == "n" && e.number == 10);
            e = model::parse_key_expr("o.alt - 4.5");
            check("key expr: dotted name, minus", e.valid && e.ref == "o.alt" && e.number == -4.5);
            check("key expr: bad ones", !model::parse_key_expr("n+").valid && !model::parse_key_expr("|n.L").valid &&
                  !model::parse_key_expr("").valid);
            model::MetricsKeys::Key r;
            std::string why;
            check("rule: L=R=60", model::parse_key_rule("L=R=60", "n", &r) && r.left == "60" && r.right == "60" &&
                  r.width.empty());
            check("rule: =o (both sides)", model::parse_key_rule("=o", "n", &r) && r.left == "o" && r.right == "o");
            check("rule: L=n R=|o W=500", model::parse_key_rule("L=n R=|o W=500", "H", &r) && r.left == "n" &&
                  r.right == "|o" && r.width == "500");
            check("rule: n: L=R=60", model::parse_key_rule("n: L=R=60", "n", &r) && r.left == "60");
            check("rule: another glyph's rule refused", !model::parse_key_rule("m: L=R=60", "n", &r, &why) && !why.empty());
            check("rule: m=n", model::parse_key_rule("m=n", "m", &r) && r.left == "n" && r.right == "n");
            check("rule: b.L=l.L", model::parse_key_rule("b.L=l.L", "b", &r) && r.left == "l.L" && r.right.empty());
            check("rule: garbage refused", !model::parse_key_rule("Q=5", "n", &r) && !model::parse_key_rule("L=", "n", &r));
            check("rule: empty = no key", model::parse_key_rule("  ", "n", &r) && r == model::MetricsKeys::Key{});
            for (const char* t : {"L=R=60", "=o", "L=n R=|o W=500", "=|n", "W=n+10"}) {
                model::MetricsKeys::Key a, b;
                model::parse_key_rule(t, "H", &a);
                model::parse_key_rule(model::format_key_rule(a), "H", &b);
                check(std::string("rule: round trip ") + t, a == b && model::format_key_rule(a) == t);
            }
            check("rule: a stored '=60' shows as L=R=60", model::format_key_rule({"=60", "=60", ""}) == "L=R=60");
            check("rule: L==60 stores 60", model::parse_key_rule("L==60", "n", &r) && r.left == "60");
            check("eval: n's left", model::eval_key_side(x.f, "n", 'L') == 70.0);
            check("eval: opposite + offset", model::eval_key_side(x.f, "|o+10", 'L') == 60.0);
            check("eval: width", model::eval_key_side(x.f, "n", 'W') == 560.0);
            check("eval: missing glyph", !model::eval_key_side(x.f, "zz", 'L'));
            const auto c = model::check_key(x.f, "n");
            check("check: the fixture's n is off (60 asked, 70 drawn)", c.has_key && c.left_off && c.right_off &&
                  !c.width_off && c.left == 60.0);
            check("check: no key", !model::check_key(x.f, "o").has_key);
            // A chain: o follows n; n is 60 / 60.
            check("set_metrics_key: one step", cmd::set_metrics_key(x.j, x.f, "o", {"n", "n", ""}) && x.j.size() == 1);
            check("set_metrics_key: same, nothing", !cmd::set_metrics_key(x.j, x.f, "o", {"n", "n", ""}));
            const auto ord = model::key_order(x.f);
            check("key_order: referenced first", ord.size() == 2 && ord[0] == "n" && ord[1] == "o");
            std::size_t n = 0;
            check("apply_metrics_keys: one step", cmd::apply_metrics_keys(x.j, x.f, {}, &n) && x.j.size() == 2 && n >= 1);
            const auto sn = model::sidebearings(x.f, *x.f.glyph("n"));
            const auto so = model::sidebearings(x.f, *x.f.glyph("o"));
            check("apply: n is 60 / 60", sn && sn->left == 60 && sn->right == 60);
            check("apply: o followed the new n", so && so->left == 60 && so->right == 60);
            check("apply: the drawing kept its width", x.f.glyph("n")->advance == 560 - 20);
            check("apply: again, nothing", !cmd::apply_metrics_keys(x.j, x.f, {}) && !model::check_key(x.f, "o").off());
            x.j.undo();
            check("apply: undo restores", model::check_key(x.f, "n").off());
            check("set_metrics_key: empty removes", cmd::set_metrics_key(x.j, x.f, "o", {}) &&
                  !x.f.metrics_keys().keys.count("o"));
            // A cycle is listed, not a hang.
            cmd::set_metrics_key(x.j, x.f, "H", {"O", "", ""});
            cmd::set_metrics_key(x.j, x.f, "O", {"H", "", ""});
            std::vector<std::string> cyc;
            model::key_order(x.f, &cyc);
            check("key_order: a cycle is reported", !cyc.empty());
        }
        {   // M7: checks -- each one found, fixed in one step, skipped, saved.
            Fix x;
            auto has = [](const std::vector<model::Finding>& xs, model::CheckId id) {
                return std::count_if(xs.begin(), xs.end(), [&](const model::Finding& f) { return f.check == id; });
            };
            const auto fo = model::check_glyph(x.f, "O");
            check("checks: O's counter runs the wrong way", has(fo, model::CheckId::Direction) == 1 &&
                  fo[0].contour == 1 && fo[0].fixable && fo[0].key() == "direction|O|1");
            check("checks: n is off its key", has(model::check_glyph(x.f, "n"), model::CheckId::Keys) == 1);
            check("checks: H is clean", model::check_glyph(x.f, "H").empty());
            check("checks: o's corners 10 off the lines are not near misses",
                  has(model::check_glyph(x.f, "o"), model::CheckId::Zones) == 0);
            // q: a box 2 units off the baseline, a round bottom ON the baseline
            // with a right top, an open stroke, an off-grid x.
            model::Glyph q;
            q.name = "q"; q.unicodes = {U'q'}; q.advance = 600; q.order = 50;
            {
                model::Contour box;
                for (auto [px, py] : {std::pair{50.0, 2.0}, {150.0, 2.0}, {150.0, 500.0}, {50.0, 500.0}})
                    box.nodes.push_back(model::Node::corner(px, py));
                model::Contour round;   // counter-clockwise: bottom, right, top, left
                using T = model::Node::Type;
                round.nodes = {{350, 0, 280, 0, 420, 0, T::Smooth}, {550, 256, 550, 120, 550, 390, T::Smooth},
                               {350, 512, 420, 512, 280, 512, T::Smooth}, {200.5, 256, 200.5, 390, 200.5, 120, T::Smooth}};
                model::Contour open;
                open.closed = false;
                open.nodes = {model::Node::corner(0, 600), model::Node::corner(100, 650)};
                q.contours = {box, round, open};
                model::Font::Loading load(x.f);
                x.f.put("q", model::Record{q});
            }
            const auto fq = model::check_glyph(x.f, "q");
            check("checks: zones -- two nodes 2 above the baseline", has(fq, model::CheckId::Zones) == 2);
            const auto ov = std::find_if(fq.begin(), fq.end(), [](const model::Finding& f) {
                return f.check == model::CheckId::Overshoot; });
            check("checks: overshoot -- the round bottom sits on the baseline",
                  has(fq, model::CheckId::Overshoot) == 1 && ov != fq.end() && ov->target == -12 && ov->index == 0);
            check("checks: open contour", has(fq, model::CheckId::Open) == 1);
            check("checks: off the grid", has(fq, model::CheckId::Origin) == 1);
            std::size_t n = 0;
            check("fix_findings: one step", cmd::fix_findings(x.j, x.f, fq, &n) && x.j.size() == 1 && n == fq.size());
            const auto after = model::check_glyph(x.f, "q");
            check("fix_findings: q is clean", after.empty(), std::to_string(after.size()) + " left");
            const auto& rq = x.f.glyph("q")->contours[1].nodes[0];
            check("fix_findings: the round bottom moved with its handles", rq.y == -12 && rq.cy1 == -12 && rq.cy2 == -12);
            check("fix_findings: the box sits on the baseline", x.f.glyph("q")->contours[0].nodes[0].y == 0);
            x.j.undo();
            check("fix_findings: undo restores", *x.f.glyph("q") == q);
            check("fix_findings: nothing fixable, nothing done",
                  !cmd::fix_findings(x.j, x.f, {model::check_glyph(x.f, "H")}));
            // Skips.
            check("skip_findings: one step", cmd::skip_findings(x.j, x.f, fo) && model::is_skipped(x.f, fo[0]));
            check("skip_findings: again, nothing", !cmd::skip_findings(x.j, x.f, fo));
            cmd::rename_glyph(x.j, x.f, "O", "O.alt");
            check("skips follow a rename", x.f.skips().keys.count("direction|O.alt|1") == 1 &&
                  x.f.skips().keys.count("direction|O|1") == 0);
            const auto dir = std::filesystem::temp_directory_path() / "granjon_selftest_skips.granjon";
            std::filesystem::remove_all(dir);
            std::string err;
            model::Font back;
            check("skips survive a save", io::save_ufo(x.f, dir.string(), &err) && io::load_ufo(dir.string(), back, &err) &&
                  back.skips() == x.f.skips(), err);
            std::filesystem::remove_all(dir);
            check("skip_findings: unskip", cmd::skip_findings(x.j, x.f, model::check_glyph(x.f, "O.alt"), false) &&
                  x.f.skips().keys.empty());
        }
        {   // M8: edge profiles and the pair readouts.
            Fix x;
            const auto ph = model::edge_profile(x.f, *x.f.glyph("H"));
            bool stems = true, below = true;
            for (std::size_t i = 0; i < ph.left.size(); ++i) {
                if (ph.y(i) > 0 && ph.y(i) < 700) stems = stems && ph.left[i] == 80 && ph.right[i] == 620;
                if (ph.y(i) < 0) below = below && !ph.ink(i);
            }
            check("profile: H's stems, nothing below the baseline", stems && below && ph.step == 10);
            auto near = [](double a, double b) { return std::abs(a - b) < 0.5; };
            const auto hh = model::read_pair(x.f, "H", "H");
            check("pair H H: contact 160, optical 160, cap band", hh.ok && near(hh.contact, 160) &&
                  near(hh.optical, 160) && hh.band == 'H' && !hh.disagree, std::to_string(hh.contact));
            const auto nn = model::read_pair(x.f, "n", "n");
            check("pair n n: 140 / 140, x band, control n n", near(nn.contact, 140) && near(nn.optical, 140) &&
                  nn.band == 'x' && nn.control == "n n" && nn.proposed == 0);
            const auto no = model::read_pair(x.f, "n", "o");
            check("pair n o: 120, proposed +20 to match n n", near(no.optical, 120) && no.proposed == 20,
                  no.proposed ? std::to_string(*no.proposed) : "none");
            const auto col = model::read_pair_at(x.f, "H", "H", -200);
            check("pair H H at -200: a collision reads negative", near(col.contact, -40));
            // T o: the bar reaches over the o -- contact sees it, optical does not.
            model::Glyph t;
            t.name = "T"; t.unicodes = {U'T'}; t.advance = 700; t.order = 60;
            {
                auto box = [](double x0, double y0, double x1, double y1) {
                    model::Contour c;
                    for (auto [px, py] : {std::pair{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}})
                        c.nodes.push_back(model::Node::corner(px, py));
                    return c;
                };
                t.contours = {box(0, 600, 700, 700), box(300, 0, 400, 600)};
                model::Font::Loading load(x.f);
                x.f.put("T", model::Record{t});
            }
            const auto to = model::read_pair(x.f, "T", "o");
            check("pair T o: contact is the bar's corner to the o (2D)", to.ok && to.contact > 100 && to.contact < 125 &&
                  to.contact_ly > 590, std::to_string(to.contact));
            check("pair T o: optical sees the stem, the readouts disagree", near(to.optical, 350) && to.disagree &&
                  to.band == 'x');
            check("pair: a missing glyph reads nothing", !model::read_pair(x.f, "T", "zz").ok);
        }
        {   // A point drag is one step on whole units.
            Fix x;
            const geom::Points pts{{0, 0, geom::PointRef::Part::Node}};
            const auto n0 = x.f.glyph("o")->contours[0].nodes[0];
            cmd::drag_begin(x.j, x.f, {"o"}, "Move points");
            for (int i = 1; i <= 6; ++i) cmd::drag_points(x.j, x.f, pts, i * 2.3, i * -1.1);
            check("drag_points: fractional mid-drag", !x.f.glyph("o")->is_integral());
            cmd::drag_end(x.j, x.f);
            const auto& m0 = x.f.glyph("o")->contours[0].nodes[0];
            check("drag_points: one step, snapped", x.j.size() == 1 && x.f.glyph("o")->is_integral());
            check("drag_points: moved from the press, not cumulatively",
                  m0.x == n0.x + 14 && m0.y == n0.y - 7, std::to_string(m0.x) + "," + std::to_string(m0.y));
            check("drag_points: only the picked node moved",
                  x.f.glyph("o")->contours[0].nodes[1] == x.f.glyph("o")->contours[0].nodes[1]);
            x.j.undo();
            check("drag_points: undone", x.f.glyph("o")->contours[0].nodes[0] == n0);
        }
        {   // Sidebearings: the left edge moves the origin under a fixed drawing.
            const auto lsb = [](const model::Glyph& g) {
                double m = 1e9;
                for (const auto& c : g.contours) for (const auto& n : c.nodes) m = std::min(m, n.x);
                return m;
            };
            const auto rsb = [&](const model::Glyph& g) {
                double m = -1e9;
                for (const auto& c : g.contours) for (const auto& n : c.nodes) m = std::max(m, n.x);
                return g.advance - m;
            };
            Fix x;
            const double l0 = lsb(*x.f.glyph("n")), r0 = rsb(*x.f.glyph("n")), a0 = x.f.glyph("n")->advance;
            cmd::drag_begin(x.j, x.f, {"n"}, "Left sidebearing");
            cmd::drag_sidebearing(x.j, x.f, cmd::Side::Left, 12.6, false);
            cmd::drag_end(x.j, x.f);
            const auto& n = *x.f.glyph("n");
            check("lsb: the origin moved right -- the LSB shrank by the rounded drag", lsb(n) == l0 - 13,
                  std::to_string(lsb(n)));
            check("lsb: the RSB holds", rsb(n) == r0, std::to_string(rsb(n)));
            check("lsb: the advance took up the difference", n.advance == a0 - 13);
            cmd::drag_begin(x.j, x.f, {"n"}, "Left sidebearing");
            cmd::drag_sidebearing(x.j, x.f, cmd::Side::Left, 20, true);
            cmd::drag_end(x.j, x.f);
            check("lsb+modifier: the drawing slides inside a fixed advance",
                  lsb(*x.f.glyph("n")) == l0 - 13 + 20 && x.f.glyph("n")->advance == a0 - 13);
            cmd::drag_begin(x.j, x.f, {"n"}, "Width");
            cmd::drag_sidebearing(x.j, x.f, cmd::Side::Right, -30.4, false);
            cmd::drag_end(x.j, x.f);
            check("rsb: the advance follows", x.f.glyph("n")->advance == a0 - 13 - 30);
            check("sidebearings: three drags, three steps", x.j.size() == 3);
            x.j.undo(); x.j.undo(); x.j.undo();
            check("sidebearings: all undone", lsb(*x.f.glyph("n")) == l0 && x.f.glyph("n")->advance == a0);
        }
        {   // Arrow nudges in a row merge.
            Fix x;
            const geom::Points pts{{0, 0, geom::PointRef::Part::Node}, {0, 1, geom::PointRef::Part::Node}};
            const auto n0 = x.f.glyph("H")->contours[0].nodes[0];
            cmd::nudge(x.j, x.f, "H", pts, 1, 0);
            cmd::nudge(x.j, x.f, "H", pts, 1, 0);
            cmd::nudge(x.j, x.f, "H", pts, 0, 10);
            check("nudge: three presses, one step", x.j.size() == 1, std::to_string(x.j.size()));
            const auto& m0 = x.f.glyph("H")->contours[0].nodes[0];
            check("nudge: moved 2, 10", m0.x == n0.x + 2 && m0.y == n0.y + 10);
            check("nudge: nothing picked, nothing kept", !cmd::nudge(x.j, x.f, "H", {}, 1, 0));
            x.j.undo();
            check("nudge: one undo", x.f.glyph("H")->contours[0].nodes[0] == n0);
        }

        // ── M5: composites, anchors, components ─────────────────────────────
        {
            // A small Latin font: e, i, dotlessi, E, c, acutecomb (+ .case), the
            // spacing cedilla (no combining one) -- no anchors yet.
            Font f;
            FontJournal j{f};
            f.set_door([&] { return j.open(); });
            model::make_new_font(f);
            {
                Font::Loading load(f);
                double order = 10;
                auto box = [](double x0, double y0, double x1, double y1) {
                    model::Contour c;
                    for (auto [x, y] : {std::pair{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}})
                        c.nodes.push_back(model::Node::corner(x, y));
                    return c;
                };
                auto put = [&](const std::string& n, char32_t cp, double adv, std::vector<model::Contour> cs) {
                    model::Glyph g;
                    g.name = n; g.advance = adv; g.order = order++; g.contours = std::move(cs);
                    if (cp) g.unicodes = {cp};
                    g.status = model::GlyphStatus::Drafted;
                    f.put(n, model::Record{g});
                };
                put("e", U'e', 500, {box(40, -10, 460, 510)});
                put("i", U'i', 260, {box(80, 0, 180, 500), box(80, 600, 180, 700)});
                put("dotlessi", 0x0131, 260, {box(80, 0, 180, 500)});
                put("E", U'E', 600, {box(60, 0, 540, 700)});
                put("c", U'c', 480, {box(40, -10, 440, 510)});
                put("acutecomb", 0x0301, 0, {box(-60, 540, 40, 680)});
                put("acutecomb.case", 0, 0, {box(-60, 740, 40, 860)});
                put("cedilla", 0x00B8, 300, {box(100, -200, 200, 0)});
            }
            using model::plan_composite;
            const auto ea = plan_composite(f, 0x00E9);
            check("composite: é = e + acutecomb", ea.ok() && ea.components.size() == 2 &&
                  ea.components[0].base == "e" && ea.components[1].base == "acutecomb");
            check("composite: no anchors -> mark centred on the base's drawing",
                  ea.components[1].dx == 250 - (-10) && ea.components[1].dy == 0,
                  std::to_string(ea.components[1].dx));
            check("composite: the base's width", ea.advance == 500);
            const auto Ea = plan_composite(f, 0x00C9);
            check("composite: É takes the .case mark, no lift",
                  Ea.ok() && Ea.components[1].base == "acutecomb.case" && Ea.components[1].dy == 0);
            const auto ia = plan_composite(f, 0x00ED);
            check("composite: í is built on dotlessi", ia.ok() && ia.components[0].base == "dotlessi");
            const auto cc = plan_composite(f, 0x00E7);
            check("composite: ç uses the spacing cedilla", cc.ok() && cc.components[1].base == "cedilla");
            const auto et = plan_composite(f, 0x1EBD);   // ẽ: no tilde in this font
            check("composite: a missing mark is named, nothing built",
                  !et.ok() && et.components.empty() && !et.missing.empty() &&
                  et.missing[0].find("U+0303") != std::string::npos, et.missing.empty() ? "" : et.missing[0]);
            check("composite: A does not decompose", plan_composite(f, U'A').components.empty() &&
                  plan_composite(f, U'A').missing.empty());

            // Default anchors, then the placement follows them.
            check("default anchors: one step for the lot",
                  cmd::add_default_anchors(j, f, {"e", "E", "acutecomb", "acutecomb.case", "cedilla", "i"}) && j.size() == 1);
            const auto* e = f.glyph("e");
            check("default anchors: e gets top + bottom", e->anchors.size() == 2 && e->anchors[0].name == "top" &&
                  e->anchors[0].x == 250 && e->anchors[0].y == 510);
            const auto* ac = f.glyph("acutecomb");
            check("default anchors: the mark gets _top at x-height", ac->anchors.size() == 2 &&
                  ac->anchors[0].name == "_top" && ac->anchors[0].y == f.metrics().x_height && ac->anchors[0].x == -10);
            check("default anchors: the case mark's _top is at cap height",
                  f.glyph("acutecomb.case")->anchors[0].y == f.metrics().cap_height);
            check("default anchors: a below mark gets _bottom",
                  f.glyph("cedilla")->anchors[0].name == "_bottom");
            check("default anchors: again, nothing to add", !cmd::add_default_anchors(j, f, {"e"}));
            const auto ea2 = plan_composite(f, 0x00E9);
            check("composite: _top meets top", ea2.ok() && ea2.components[1].dx == 260 &&
                  ea2.components[1].dy == 510 - 500, std::to_string(ea2.components[1].dx) + "," +
                  std::to_string(ea2.components[1].dy));

            // Create missing builds composites, one step.
            auto news = model::glyphs_for(f, {0x00E9, 0x00C9, 0x1EBD});
            check("plan_composites: two of three can be built", model::plan_composites(f, news) == 2);
            const std::size_t before = j.size();
            cmd::add_glyphs(j, f, news, "Create 3 missing");
            const auto* eac = f.glyph("eacute");
            check("create missing: one step", j.size() == before + 1);
            check("create missing: é is a composite, Drafted, e's width",
                  eac && eac->components.size() == 2 && eac->status == model::GlyphStatus::Drafted && eac->advance == 500);
            check("create missing: ẽ is empty", f.glyph_for(0x1EBD) && f.glyph_for(0x1EBD)->components.empty());
            j.undo();
            check("glyph_for: the cache follows an undo", !f.glyph_for(0x00E9) && !f.glyph("eacute"));
            j.redo();
            check("glyph_for: ... and a redo", f.glyph_for(0x00E9) && f.glyph_for(0x00E9)->name == "eacute");

            // Components: no cycles; decompose keeps the drawing.
            std::string why;
            check("component: a glyph can't contain itself", !cmd::add_component(j, f, "e", "eacute", 0, 0, &why) &&
                  !why.empty(), why);
            check("component: a missing base is refused", !cmd::add_component(j, f, "e", "nope"));
            const auto box_before = model::bounds(f, *f.glyph("eacute"));
            check("decompose: one step", cmd::decompose(j, f, "eacute"));
            const auto* dec = f.glyph("eacute");
            check("decompose: plain contours now", dec->components.empty() && dec->contours.size() == 2);
            check("decompose: the same drawing", model::bounds(f, *dec) == box_before);
            j.undo();
            check("decompose: undone", f.glyph("eacute")->components.size() == 2);

            // Anchors: names checked, duplicates refused, delete takes the pick.
            check("anchor: a bad name is refused", !cmd::add_anchor(j, f, "c", "top anchor", 0, 0));
            check("anchor: added", cmd::add_anchor(j, f, "c", "top", 240, 510));
            check("anchor: a duplicate is refused", !cmd::add_anchor(j, f, "c", "top", 0, 0));
            check("anchor: renamed", cmd::rename_anchor(j, f, "c", 0, "top.alt") && f.glyph("c")->anchors[0].name == "top.alt");
            check("delete: an anchor and a component in one step",
                  cmd::delete_points(j, f, "eacute", {{-1, 1, geom::PointRef::Part::Component},
                                                      {-1, 0, geom::PointRef::Part::Anchor}}) ||
                  cmd::delete_points(j, f, "eacute", {{-1, 1, geom::PointRef::Part::Component}}));
            check("delete: the mark is gone", f.glyph("eacute")->components.size() == 1);
            {   // M5f: nodes delete now (one step, undone whole).
                const auto before = *f.glyph("e");
                check("delete: a node goes, in one step",
                      cmd::delete_points(j, f, "e", {{0, 0, geom::PointRef::Part::Node}}, &why) &&
                      f.glyph("e")->contours[0].nodes.size() + 1 == before.contours[0].nodes.size());
                j.undo();
                check("delete: undone whole", *f.glyph("e") == before);
            }
            check("position: typed coordinates", cmd::set_position(j, f, "c", {-1, 0, geom::PointRef::Part::Anchor}, 250.4, 520) &&
                  f.glyph("c")->anchors[0].x == 250 && f.glyph("c")->anchors[0].y == 520);
            check("position: the component offset", cmd::set_position(j, f, "eacute", {-1, 0, geom::PointRef::Part::Component}, 5, 0) &&
                  f.glyph("eacute")->components[0].dx == 5);

            // Picking a component: inside its drawing.
            const auto* ce = f.glyph("eacute");
            check("pick: inside the component", geom::component_at(f, *ce, {200, 200}) == 0);
            check("pick: outside it", geom::component_at(f, *ce, {700, 200}) == -1);
        }
        {   // Status on load: a foreign glyph with a drawing is Drafted; Granjon's Empty stays Empty.
            const std::string foreign = R"(<?xml version="1.0" encoding="UTF-8"?>
<glyph name="a" format="2"><advance width="500"/><outline><contour>
<point x="0" y="0" type="line"/><point x="100" y="0" type="line"/><point x="100" y="100" type="line"/>
</contour></outline></glyph>)";
            const auto a = io::read_glif(foreign);
            check("load: a foreign drawn glyph is Drafted", a && a->status == model::GlyphStatus::Drafted);
            model::Glyph g = *a;
            g.status = model::GlyphStatus::Empty;
            const auto back = io::read_glif(io::write_glif(g));
            check("load: a drawn glyph Granjon marked Empty stays Empty", back && back->status == model::GlyphStatus::Empty);
            const std::string blank = R"(<?xml version="1.0" encoding="UTF-8"?><glyph name="b" format="2"><advance width="500"/></glyph>)";
            check("load: an undrawn foreign glyph is Empty", io::read_glif(blank)->status == model::GlyphStatus::Empty);
        }

        {   // Field edits merge inside the window; a different field does not.
            Fix x;
            using Clock = FontJournal::Clock;
            using F = model::Metrics::Field;
            const auto t0 = Clock::now();
            x.j.run("x", {Font::kMetrics}, [&] { auto m = x.f.metrics(); m.x_height = 490; x.f.put(Font::kMetrics, model::Record{m}); }, "metric:x-height", t0);
            x.j.run("x", {Font::kMetrics}, [&] { auto m = x.f.metrics(); m.x_height = 480; x.f.put(Font::kMetrics, model::Record{m}); }, "metric:x-height", t0 + std::chrono::milliseconds(300));
            check("merge: two x-height presses are one step", x.j.size() == 1);
            cmd::set_metric(x.j, x.f, F::CapHeight, 690);
            check("merge: another field is its own step", x.j.size() == 2);
            x.j.undo(); x.j.undo();
            check("merge: one undo takes both presses back", x.f.metrics().x_height == 500);
        }

        {   // Bounds: exact cubic extrema, components resolved through the font.
            Fix x;
            const auto b = model::bounds(x.f, *x.f.glyph("o"));
            check("bounds: a box", b && *b == (model::Rect{50, -10, 490, 510}));
            const auto sb = model::sidebearings(x.f, *x.f.glyph("o"));
            check("bounds: sidebearings", sb && sb->left == 50 && sb->right == 50);
            const auto bc = model::bounds(x.f, *x.f.glyph("Odotbelow"));
            check("bounds: through a component", bc && *bc == (model::Rect{60, -12, 700, 712}));
            check("bounds: empty drawing", !model::bounds(x.f, *x.f.glyph("space")));
            model::Glyph arc;   // one cubic bulging right: extremum at t=0.5, x = 75
            model::Node a = model::Node::corner(0, 0), c = model::Node::corner(0, 100);
            a.cx2 = 100; a.cy2 = 0;
            c.cx1 = 100; c.cy1 = 100;
            arc.contours = {{{a, c}, true}};
            const auto ba = model::bounds(x.f, arc);
            check("bounds: cubic extremum solved, not the handle", ba && std::abs(ba->x1 - 75) < 1e-9,
                  ba ? std::to_string(ba->x1) : "none");
        }

        {   // Unicode helpers.
            namespace uni = granjon::unicode;
            std::vector<char32_t> v;
            check("cps: U+ list", uni::parse_cps("U+0041 U+00C1", v) && v == std::vector<char32_t>{0x41, 0xC1});
            check("cps: bare hex, commas", uni::parse_cps("0416,1e0c", v) && v == std::vector<char32_t>{0x416, 0x1E0C});
            check("cps: a literal character", uni::parse_cps("\xC3\xA9", v) && v == std::vector<char32_t>{0xE9});
            check("cps: empty is none", uni::parse_cps("  ", v) && v.empty());
            check("cps: junk refused", !uni::parse_cps("U+ZZZZ", v) && !uni::parse_cps("D800", v));
            check("blocks: sections", uni::section_of({0x416}) == "Cyrillic" && uni::section_of({}) == "Unencoded" &&
                                      uni::section_rank("Basic Latin") < uni::section_rank("Cyrillic"));
        }

        {   // Find: one query, any reference, ranked.
            Fix x;
            const auto top = [&](const std::string& q) {
                const auto r = model::find_glyphs(x.f, q);
                return r.hits.empty() ? std::string("(none)") : r.hits.front().name;
            };
            check("find: exact name", top("H") == "H");
            check("find: exact case first ('o' before 'O')", top("o") == "o", top("o"));
            check("find: prefix", top("Odot") == "Odotbelow");
            check("find: substring", top("dotbel") == "Odotbelow");
            check("find: the character", top("\xE1\xBB\x8C") == "Odotbelow");   // U+1ECC
            check("find: U+ form", top("U+006E") == "n");
            check("find: 0x form", top("0x48") == "H");
            check("find: bare hex (4+)", top("1ecc") == "Odotbelow");
            check("find: /escape is exact", top("/O") == "O" && model::find_glyphs(x.f, "/Od").hits.empty());
            check("find: empty query", model::find_glyphs(x.f, "  ").empty_query);
            check("find: no match", model::find_glyphs(x.f, "zzz").hits.empty());
            const auto creates = [&](const std::string& q, char32_t cp, const std::string& name) {
                const auto r = model::find_glyphs(x.f, q);
                return !r.create.empty() && r.create.front().cp == cp && r.create.front().name == name;
            };
            check("find: a missing character is offered under its AGL name", creates("\xC3\xA9", 0xE9, "eacute"));
            check("find: a missing letter is named for itself", creates("A", U'A', "A"));
            check("find: no AGL name -> uniXXXX", creates("U+0416", 0x416, "uni0416"));
            check("find: a mapped codepoint is not offered", model::find_glyphs(x.f, "U+0048").create.empty());
            check("find: 'ff' is a name, not a codepoint", model::find_glyphs(x.f, "ff").create.empty() ||
                                                           model::find_glyphs(x.f, "ff").create.front().cp != 0xFF);
            check("find: an AGL name nothing has is offered as typed", creates("eacute", 0xE9, "eacute"));
            check("find: uni name offered as typed", creates("uni0436", 0x436, "uni0436"));
            check("find: an AGL name finds the glyph mapping it", top("Ocircumflexdotbelow") != "(none)" ||
                                                                  top("uni1ECC") == "Odotbelow", top("uni1ECC"));
            const auto dot = model::find_glyphs(x.f, "dot below");
            bool by_name = false;
            for (const auto& h : dot.hits) by_name = by_name || (h.name == "Odotbelow" && std::string(h.why) == "unicode name");
            check("find: by Unicode name ('dot below' -> Odotbelow)", by_name);
            const auto zhe = model::find_glyphs(x.f, "cyrillic zhe");
            check("find: Unicode name offers characters to create",
                  !zhe.create.empty() && (zhe.create.front().cp == 0x416 || zhe.create.front().cp == 0x436) &&
                  zhe.create.size() <= model::FindResult::kMaxCreate);
        }

        {   // M3: the bundled UCD, AGL names, char sets, coverage.
            namespace uni = granjon::unicode;
            check("ucd: a name", uni::name_of(0xE9) == "LATIN SMALL LETTER E WITH ACUTE", uni::name_of(0xE9));
            check("ucd: a control takes its Unicode 1 name", uni::name_of(0x0A) == "LINE FEED (LF)", uni::name_of(0x0A));
            check("ucd: CJK names are algorithmic", uni::name_of(0x4E00) == "CJK UNIFIED IDEOGRAPH-4E00");
            check("ucd: Hangul names are composed", uni::name_of(0xAC00) == "HANGUL SYLLABLE GA" &&
                                                    uni::name_of(0xD7A3) == "HANGUL SYLLABLE HIH", uni::name_of(0xD7A3));
            check("ucd: unassigned has no name", uni::name_of(0x0378).empty() && !uni::is_assigned(0x0378));
            check("ucd: a Unicode 17 character is known", uni::is_assigned(0x1CEDD) || uni::is_assigned(0x20C1),
                  uni::name_of(0x20C1));
            check("ucd: categories", uni::category(U'A') == "Lu" && uni::category(0x301) == "Mn" &&
                                     uni::category(0x378) == "Cn" && uni::category(0x4E01) == "Lo");
            bool compat = true;
            check("ucd: canonical decomposition", uni::decomposition(0xE9, &compat) == std::vector<char32_t>{U'e', 0x301} && !compat);
            check("ucd: compatibility decomposition", uni::decomposition(0xFB01, &compat) == std::vector<char32_t>{U'f', U'i'} && compat);
            check("ucd: no decomposition", uni::decomposition(U'A').empty());
            check("ucd: case", uni::other_case(U'A') == U'a' && uni::other_case(U'a') == U'A' &&
                               uni::other_case(0x416) == 0x436 && uni::other_case(U'1') == 0);
            const auto ced = uni::find_by_name("cedilla", 500);
            check("ucd: find 'cedilla'", std::find(ced.begin(), ced.end(), 0xE7) != ced.end() &&
                                         std::find(ced.begin(), ced.end(), 0x15F) != ced.end(), std::to_string(ced.size()));
            const auto zhe = uni::find_by_name("cyrillic zhe", 10);
            check("ucd: 'cyrillic zhe' ranks the letters first", zhe.size() >= 2 &&
                  ((zhe[0] == 0x416 && zhe[1] == 0x436) || (zhe[0] == 0x436 && zhe[1] == 0x416)));
            const auto exact = uni::find_by_name("latin small letter a", 3);
            check("ucd: an exact name ranks first", !exact.empty() && exact[0] == U'a');
            check("blocks: from the UCD", uni::block_of(0x1F600).name == "Emoticons" &&
                                          uni::block_of(0x2C60).name == "Latin Extended-C" &&
                                          uni::blocks().size() > 300, std::to_string(uni::blocks().size()));
            check("blocks: sections sort in code point order", uni::section_rank("Latin-1 Supplement") <
                  uni::section_rank("Greek and Coptic") && uni::section_rank("Emoticons") < uni::section_rank("Unencoded"));

            check("agl: nice names", uni::nice_name(0xE9) == "eacute" && uni::nice_name(U'A') == "A" &&
                                     uni::nice_name(0x416) == "uni0416" && uni::nice_name(0x1F600) == "u1F600");
            check("agl: names -> characters",
                  uni::codepoints_for_name("eacute") == std::vector<char32_t>{0xE9} &&
                  uni::codepoints_for_name("eacute.sc") == std::vector<char32_t>{0xE9} &&
                  uni::codepoints_for_name("f_i") == std::vector<char32_t>{U'f', U'i'} &&
                  uni::codepoints_for_name("uni0416") == std::vector<char32_t>{0x416} &&
                  uni::codepoints_for_name("uni00410042") == std::vector<char32_t>{0x41, 0x42} &&
                  uni::codepoints_for_name("u1F600") == std::vector<char32_t>{0x1F600} &&
                  uni::codepoints_for_name("uni0416abc").empty() && uni::codepoints_for_name("foo").empty() &&
                  uni::codepoints_for_name("uniD800").empty());

            check("charsets: five bundled", uni::charsets().size() == 5);
            const auto* l1 = uni::charset("latin-1");
            check("charsets: Latin-1 is 191", l1 && l1->cps.size() == 191 && l1->name == "Latin-1");
            const auto* al2 = uni::charset("adobe-latin-2");
            const auto* al3 = uni::charset("adobe-latin-3");
            check("charsets: Adobe Latin 2 within 3", al2 && al3 && al2->cps.size() < al3->cps.size() &&
                  std::includes(al3->cps.begin(), al3->cps.end(), al2->cps.begin(), al2->cps.end()));
            check("charsets: GF Latin Core / WGL4 loaded", uni::charset("gf-latin-core") && uni::charset("wgl4") &&
                  uni::charset("wgl4")->cps.size() == 651);
            bool named = true;
            for (const auto& cs : uni::charsets())
                for (char32_t c : cs.cps) named = named && uni::is_assigned(c);
            check("charsets: every member is an assigned character", named);

            Fix x;
            const auto miss = model::missing_from(x.f, l1->cps);
            check("coverage: the fixture's mapped letters are not missing",
                  std::find(miss.begin(), miss.end(), U'H') == miss.end() &&
                  std::find(miss.begin(), miss.end(), U'A') != miss.end() && miss.size() + std::count_if(l1->cps.begin(), l1->cps.end(), [&](char32_t c) {
                      return model::mapped(x.f).count(c) > 0; }) == 191,
                  std::to_string(miss.size()));
            auto todo = model::glyphs_for(x.f, miss);
            check("coverage: new glyphs get AGL names", std::any_of(todo.begin(), todo.end(), [](const model::NewGlyph& g) {
                      return g.name == "eacute" && g.unicodes == std::vector<char32_t>{0xE9}; }));
            const std::size_t before = x.f.glyph_order().size();
            check("coverage: create missing is one step",
                  granjon::command::add_glyphs(x.j, x.f, todo, "Create missing") &&
                  x.f.glyph_order().size() == before + todo.size() && x.j.undo_label() == "Create missing");
            check("coverage: then nothing is missing", model::missing_from(x.f, l1->cps).empty());
            x.j.undo();
            check("coverage: one undo takes them all back", x.f.glyph_order().size() == before &&
                  model::missing_from(x.f, l1->cps).size() == miss.size());
            granjon::command::add_glyph(x.j, x.f, "A", {U'A'});
            granjon::command::add_glyph(x.j, x.f, "h.sc");
            granjon::command::add_glyph(x.j, x.f, "H.sc");
            char32_t oc = 1;
            check("case: n -> nothing (no N)", model::other_case_glyph(x.f, "n", &oc).empty() && oc == U'N');
            check("case: H <-> h? none; o <-> O", model::other_case_glyph(x.f, "o") == "O" &&
                                                  model::other_case_glyph(x.f, "O") == "o");
            check("case: A -> a missing, A from a", model::other_case_glyph(x.f, "A").empty());
            granjon::command::add_glyph(x.j, x.f, "h", {U'h'});
            check("case: suffixed partners", model::other_case_glyph(x.f, "h.sc") == "H.sc" &&
                                             model::other_case_glyph(x.f, "H.sc") == "h.sc" &&
                                             model::other_case_glyph(x.f, "H") == "h");
            check("case: no case", model::other_case_glyph(x.f, "Odotbelow") == "" &&
                                   (model::other_case_glyph(x.f, "Odotbelow", &oc), oc == 0x1ECD));
            std::string why;
            check("coverage: a taken name refuses the whole batch",
                  !granjon::command::add_glyphs(x.j, x.f, {{"H", {0x10}}, {"zz", {}}}, "", &why) && !why.empty() &&
                  !x.f.glyph("zz"));
        }

        {   // Glyph helpers.
            model::Glyph g;
            g.advance = 500.4;
            g.contours = {{{model::Node::corner(1.5, -2.5)}}};
            g.snap();
            check("snap: rounds half away from zero, no negative zero",
                  g.advance == 500 && g.contours[0].nodes[0].x == 2 && g.contours[0].nodes[0].y == -3);
            check("names: AGL-safe", model::valid_glyph_name("uni0416") && model::valid_glyph_name(".notdef") &&
                                     model::valid_glyph_name("a.sc") && !model::valid_glyph_name(".hidden") &&
                                     !model::valid_glyph_name("a b") && !model::valid_glyph_name("@info"));
        }
    }


    // ── io -- plist, GLIF, UFO round trips (Granjon M2) ─────────────────────
    {
        std::cout << "\n-- io --\n";
        namespace model = granjon::model;
        namespace io    = granjon::io;
        namespace fs    = std::filesystem;
        using model::Node;

        {   // plist: write -> read -> write is byte-identical, keys sorted.
            io::PValue d = io::PValue::dictionary();
            d.set("zeta", io::PValue::str("a < b & \"c\""));
            d.set("alpha", io::PValue::integer(-250));
            d.set("mid", io::PValue::real(12.5));
            d.set("flag", io::PValue::boolean(true));
            io::PValue arr = io::PValue::array();
            arr.arr = {io::PValue::str("x"), io::PValue::dictionary()};
            d.set("list", arr);
            const std::string a = io::write_plist(d);
            std::string err;
            const auto back = io::read_plist(a, &err);
            check("plist: reads back", back.has_value(), err);
            check("plist: byte-identical rewrite", back && io::write_plist(*back) == a);
            check("plist: keys sorted", a.find("<key>alpha</key>") < a.find("<key>zeta</key>"));
            check("plist: escaping survives", back && back->get_string("zeta") == std::string("a < b & \"c\""));
            check("plist: junk refused", !io::read_plist("<plist><dict><key>a</key></dict></plist>", &err));
        }

        // A glyph with every feature: curves, a smooth and a symmetric node,
        // an open contour, a component, anchors, note, status, fractions.
        model::Glyph rich;
        rich.name = "Odieresis";
        rich.unicodes = {0xD6, 0xF6};
        rich.advance = 760;
        rich.note = "check <the> overshoot & dots";
        rich.status = model::GlyphStatus::Review;
        rich.anchors = {{"top", 380, 720}, {"_bottom", 380.5, 0}};
        rich.components = {{"dieresiscomb", 1, 0, 0, 1, 120, 0}, {"O", -1, 0, 0, 1, 760, 0}};
        {
            Node a = Node::corner(380, -12), b = Node::corner(700, 350), c = Node::corner(380, 712),
                 d = Node::corner(60, 350);
            a.cx1 = 200; a.cy1 = -12; a.cx2 = 560; a.cy2 = -12; a.type = Node::Type::Smooth;
            b.cx1 = 700; b.cy1 = 150; b.cx2 = 700; b.cy2 = 550; b.type = Node::Type::Symmetric;
            c.cx1 = 560; c.cy1 = 712; c.cx2 = 200; c.cy2 = 712; c.type = Node::Type::Smooth;
            d.cx1 = 60;  d.cy1 = 550; d.cx2 = 60;  d.cy2 = 150.25; d.type = Node::Type::Cusp;
            model::Contour outer{{a, b, c, d}, true};
            model::Contour box{{Node::corner(10, 10), Node::corner(10, 20), Node::corner(20, 20)}, true};
            model::Contour open{{Node::corner(0, 0), Node::corner(50, 0)}, false};
            rich.contours = {outer, box, open};
        }
        {
            const std::string x = io::write_glif(rich);
            std::string err;
            const auto back = io::read_glif(x, &err);
            check("glif: reads back", back.has_value(), err);
            check("glif: round trip is exact (types, handles, open, components, anchors, note, status)",
                  back && *back == rich);
            check("glif: rewrite is byte-identical", back && io::write_glif(*back) == x);
            check("glif: start point first, closing off-curves last",
                  x.find("<point x=\"380\" y=\"-12\" type=\"curve\" smooth=\"yes\"/>") <
                  x.find("<point x=\"700\" y=\"350\""));
        }
        {   // Foreign GLIF: starts on off-curves; a TrueType qcurve.
            const std::string q =
                "<glyph name=\"q\" format=\"2\"><advance width=\"100\"/><outline><contour>"
                "<point x=\"0\" y=\"100\"/>"
                "<point x=\"0\" y=\"0\" type=\"qcurve\"/>"
                "<point x=\"100\" y=\"0\" type=\"line\"/>"
                "</contour></outline></glyph>";
            std::string err;
            const auto g = io::read_glif(q, &err);
            check("glif: qcurve contour reads", g && g->contours.size() == 1 && g->contours[0].nodes.size() == 2, err);
            if (g && g->contours[0].nodes.size() == 2) {
                const auto& n0 = g->contours[0].nodes[0];   // (0,0), reached by the quadratic from (100,0)
                const auto& n1 = g->contours[0].nodes[1];   // (100,0)
                // Quadratic (100,0) -q(0,100)-> (0,0): cubic c1 = p0 + 2/3(q-p0), c2 = p1 + 2/3(q-p1).
                check("glif: quadratic promoted to cubic exactly",
                      std::abs(n1.cx2 - 100.0 / 3) < 1e-9 && std::abs(n1.cy2 - 200.0 / 3) < 1e-9 &&
                      std::abs(n0.cx1 - 0) < 1e-9 && std::abs(n0.cy1 - 200.0 / 3) < 1e-9);
            }
            check("glif: not a glyph refused", !io::read_glif("<glyph format=\"2\"/>", &err) && !err.empty());
        }
        {   // File names (UFO 3 rule).
            std::vector<std::string> taken;
            check("names: uppercase marked", io::glif_file_name("A", taken) == "A_.glif");
            check("names: leading dot", io::glif_file_name(".notdef", taken) == "_notdef.glif");
            check("names: reserved", io::glif_file_name("con", taken) == "_con.glif");
            check("names: illegal chars", io::glif_file_name("a/b", taken) == "a_b.glif");
            taken = {"a_.glif"};
            check("names: case-insensitive clash gets a number", io::glif_file_name("A", taken) == "A_1.glif");
        }
        {   // The whole font: save -> load -> equal; save twice -> byte-identical.
            const fs::path dir = fs::temp_directory_path() / "granjon_selftest_io";
            fs::remove_all(dir);
            fs::create_directories(dir);
            model::Font f;
            granjon::command::make_fixture(f);
            {
                model::Font::Loading load(f);
                model::Glyph g = rich;
                g.order = 50;
                f.put(g.name, model::Record{g});
                model::Glyph dc;
                dc.name = "dieresiscomb"; dc.unicodes = {0x308}; dc.order = 51;
                f.put(dc.name, model::Record{dc});
                auto info = f.info();
                info.copyright = "(c) 2026 Scott"; info.license = "OFL"; info.vendor = "GRNJ";
                f.put(model::Font::kInfo, model::Record{info});
                f.put(model::Font::kFeatures, model::Record{model::Features{"feature liga { sub f i by fi; } liga;\n"}});
            }
            std::string err;
            const auto a = dir / "Test.granjon";
            check("ufo: saves", io::save_ufo(f, a.string(), &err), err);
            model::Font g;
            check("ufo: loads", io::load_ufo(a.string(), g, &err), err);
            check("ufo: same glyph order", g.glyph_order() == f.glyph_order());
            // Orders are renumbered 1..n on load; compare everything else.
            const auto norm = [](const model::Font& x) {
                auto s = x.snapshot();
                for (auto& [id, r] : s)
                    if (auto* gl = std::get_if<model::Glyph>(&r)) gl->order = 0;
                return s;
            };
            check("ufo: every record survives", norm(g) == norm(f));
            check("ufo: loading is not a stray", g.strays() == 0);
            const auto b = dir / "Again.granjon";
            io::save_ufo(g, b.string(), &err);
            bool same = true;
            std::string differ;
            for (const auto& e : fs::recursive_directory_iterator(a)) {
                if (!e.is_regular_file()) continue;
                const auto rel = fs::relative(e.path(), a);
                std::ifstream x(e.path(), std::ios::binary), y(b / rel, std::ios::binary);
                std::string sx((std::istreambuf_iterator<char>(x)), {}), sy((std::istreambuf_iterator<char>(y)), {});
                if (sx != sy) { same = false; differ = rel.string(); }
            }
            check("ufo: save -> load -> save is byte-identical", same, differ);
            check("ufo: expected files", fs::exists(a / "metainfo.plist") && fs::exists(a / "layercontents.plist") &&
                                         fs::exists(a / "glyphs" / "contents.plist") &&
                                         fs::exists(a / "glyphs" / "_notdef.glif") && fs::exists(a / "features.fea"));
            // Re-save after a delete: the stale .glif must go (the swap-in).
            {
                model::Font::Loading load(g);
                g.put("H", std::nullopt);
            }
            io::save_ufo(g, a.string(), &err);
            check("ufo: a deleted glyph's file is gone after save", !fs::exists(a / "glyphs" / "H_.glif"));
            check("ufo: no temp folders left", !fs::exists(a.string() + ".saving") && !fs::exists(a.string() + ".previous"));
            // A failed load leaves the open font alone.
            model::Font keep;
            granjon::command::make_fixture(keep);
            const auto before = keep.snapshot();
            check("ufo: not a folder is refused, with a reason", !io::load_ufo((dir / "nope").string(), keep, &err) && !err.empty(), err);
            fs::create_directories(dir / "Broken.granjon");
            check("ufo: no metainfo is refused", !io::load_ufo((dir / "Broken.granjon").string(), keep, &err), err);
            check("ufo: a failed load changes nothing", keep.snapshot() == before);
            fs::remove_all(dir);
        }
    }

    selftest_export([](const std::string& n, bool ok, const std::string& d) { check(n, ok, d); });
    selftest_edit([](const std::string& n, bool ok, const std::string& d) { check(n, ok, d); });
    selftest_import([](const std::string& n, bool ok, const std::string& d) { check(n, ok, d); });
    selftest_presets([](const std::string& n, bool ok, const std::string& d) { check(n, ok, d); });
    selftest_image([](const std::string& n, bool ok, const std::string& d) { check(n, ok, d); });
    selftest_draw([](const std::string& n, bool ok, const std::string& d) { check(n, ok, d); });

    std::cout << "-----------------------------------------------\n";
    std::cout << g_pass << " pass / " << g_fail << " fail\n";
    return g_fail == 0 ? 0 : 1;
}
