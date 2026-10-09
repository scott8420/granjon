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
// (M4 replaces this with node / handle drags; the gesture shape stays.)
void drag_translate(FontJournal& j, Font& f, double dx, double dy);
bool drag_end(FontJournal& j, Font& f);    // snaps to integer units, then commits
void drag_cancel(FontJournal& j);

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
