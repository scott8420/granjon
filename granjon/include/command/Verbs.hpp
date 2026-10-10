#pragma once
// command::Verbs -- every user choice that writes the font. Each goes through
// the journal (Journal::run or a gesture) -- the ONE DOOR. The UI calls these
// and nothing else writes the model.
//
// A verb that refuses (bad name, glyph in use, value unchanged) returns false
// and writes nothing; a refusal is not a step. `why` (when given) says why, in
// words a designer can act on -- the teach-as-you-correct voice starts here.
//
// The verb table at the bottom is what the selftest walks: one door, undo
// restores, redo re-applies. A verb the UI offers and the table lacks is the
// gap the gate cannot see -- add it here in the same change that adds the
// button. (CANON: "Undo is foundation, not a feature".)
#include "geom/Edit.hpp"
#include "model/Coverage.hpp"
#include "model/Font.hpp"

#include <functional>
#include <string>
#include <vector>

namespace granjon::command {

using model::Font;
using model::FontJournal;

// ── glyphs ─────────────────────────────────────────────────────────────────
bool add_glyph(FontJournal& j, Font& f, const std::string& name,
               const std::vector<char32_t>& unicodes = {}, const std::string& after = {},
               std::string* why = nullptr);
// Many new glyphs as ONE step (a char set's "create missing"), appended to
// the glyph order as given. Refuses -- writing nothing -- if any name is bad
// or taken. `label` names the step ("Create 17 missing (Adobe Latin 2)").
bool add_glyphs(FontJournal& j, Font& f, const std::vector<model::NewGlyph>& glyphs,
                const std::string& label = {}, std::string* why = nullptr);
// One new glyph for a character, built from parts when it can be (é = e +
// acutecomb, model::plan_composite) and empty otherwise. `built` says which.
bool create_glyph_for(FontJournal& j, Font& f, const std::string& name, char32_t cp,
                      const std::string& after = {}, bool* built = nullptr, std::string* why = nullptr);
bool remove_glyph(FontJournal& j, Font& f, const std::string& name, std::string* why = nullptr);
// Rename carries every reference with it: components in other glyphs, kerning
// pairs and group members, metrics keys. One step.
bool rename_glyph(FontJournal& j, Font& f, const std::string& from, const std::string& to,
                  std::string* why = nullptr);
bool set_unicodes(FontJournal& j, Font& f, const std::string& name, const std::vector<char32_t>& cps);
bool set_status(FontJournal& j, Font& f, const std::string& name, model::GlyphStatus s);
bool set_note(FontJournal& j, Font& f, const std::string& name, const std::string& note);   // merges
bool set_advance(FontJournal& j, Font& f, const std::string& name, double advance);        // merges

// ── outline drag: press .. move .. release = ONE step, snapped on release ──
void drag_begin(FontJournal& j, Font& f, const std::vector<std::string>& glyphs,
                const std::string& label = "Move");
// Translate whole outlines by (dx, dy) from where they were at the press.
void drag_translate(FontJournal& j, Font& f, double dx, double dy);
// The editor's drags (M4), inside the same gesture -- each move is "press +
// delta", never cumulative. Each applies to the FIRST glyph drag_begin took.
//   drag_points       picked nodes / handles / anchors (geom::move_points)
//   drag_sidebearing  Right: the advance follows. Left, move_outline false:
//                     the ORIGIN moves under a fixed drawing -- the outline
//                     shifts the other way in font units and the advance
//                     takes up the difference, so the RSB holds. Left,
//                     move_outline true (a modifier): the drawing slides
//                     inside a fixed advance.
enum class Side { Left, Right };
void drag_points(FontJournal& j, Font& f, const geom::Points& pts, double dx, double dy,
                 bool free_handles = false);   // Alt on a handle: it moves alone, its node turns Corner
void drag_sidebearing(FontJournal& j, Font& f, Side side, double dx, bool move_outline);
bool drag_end(FontJournal& j, Font& f);    // snaps to integer units, then commits
void drag_cancel(FontJournal& j);

// Arrow keys: move the picked points by (dx, dy) font units. Presses in a row
// on one glyph merge into one step (the 1500 ms window).
bool nudge(FontJournal& j, Font& f, const std::string& glyph, const geom::Points& pts,
           double dx, double dy);

// ── components & anchors (M5) ──────────────────────────────────────────────
// A component on `base`, offset (dx, dy). Refuses a missing base or a cycle.
bool add_component(FontJournal& j, Font& f, const std::string& glyph, const std::string& base,
                   double dx = 0, double dy = 0, std::string* why = nullptr);
// Components -> plain contours (nested ones too), one step.
bool decompose(FontJournal& j, Font& f, const std::string& glyph, std::string* why = nullptr);
bool add_anchor(FontJournal& j, Font& f, const std::string& glyph, const std::string& name,
                double x, double y, std::string* why = nullptr);
bool rename_anchor(FontJournal& j, Font& f, const std::string& glyph, int index, const std::string& name,
                   std::string* why = nullptr);
// The Delete key: the anchors and components in the pick (nodes not yet).
bool delete_points(FontJournal& j, Font& f, const std::string& glyph, const geom::Points& pts,
                   std::string* why = nullptr);
// The type of every picked node (and of the node under each picked handle),
// ONE step. Refuses when nothing changes.
bool set_node_type(FontJournal& j, Font& f, const std::string& glyph, const geom::Points& pts,
                   model::Node::Type type);
// Typed coordinates (the inspector): one point to (x, y), whole units.
// Edits to the same point merge in the window.
bool set_position(FontJournal& j, Font& f, const std::string& glyph, const geom::PointRef& p,
                  double x, double y);
// model::default_anchors on each glyph, ONE step. Refuses if none get any.
bool add_default_anchors(FontJournal& j, Font& f, const std::vector<std::string>& glyphs,
                         std::string* why = nullptr);

// ── font records ───────────────────────────────────────────────────────────
bool set_family(FontJournal& j, Font& f, const std::string& family);
bool set_style(FontJournal& j, Font& f, const std::string& style);
bool set_units_per_em(FontJournal& j, Font& f, int upm, std::string* why = nullptr);
bool set_metric(FontJournal& j, Font& f, model::Metrics::Field which, int value);   // merges
bool set_kern(FontJournal& j, Font& f, const std::string& left, const std::string& right, int value);

// ── the verb table ─────────────────────────────────────────────────────────
// Each entry runs its verb once on the selftest fixture (make_fixture).
struct Verb {
    std::string name;
    std::function<void(FontJournal&, Font&)> run;
};
const std::vector<Verb>& verbs();
// The fixture the table runs on: a new font plus H, O, n, o, a component
// glyph (Ohorn-like "Odot" on O), a kerning group and a pair.
void make_fixture(Font& f);

}  // namespace granjon::command
