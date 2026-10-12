#pragma once
// model::Presets (M12) -- start a font from a preset: Sans / Serif / Display /
// Mono. GTK-free, so the selftest builds every preset and runs the checks on
// what it made.
//
// A preset (resources/presets/presets.txt, compiled in) is numbers: UPM,
// vertical metrics, overshoot, stems, serif, a char set, font guides, and the
// six control glyphs' widths and sidebearings. make_preset_font turns it into
// a font:
//
//   * metrics, overshoot, guides, the preset noted in FontInfo::origin
//   * the control glyphs n o H O p v DRAWN -- simple shapes from parts
//     (stems, bars, ellipses, serif slabs) merged into clean outlines with
//     geom::remove_overlaps, spaced, with their metrics keys
//   * optionally the char set's glyphs: empty, accented letters as
//     components (they fill in as their bases are drawn), with the [keys]
//     table's spacing keys (mono: W=<mono> on everything)
//
// Opening a document is not a step: the result is Loaded, like make_new_font.
#include "model/Font.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace granjon::model {

struct Preset {
    std::string id, name, blurb;
    int units_per_em = 1000;
    Metrics metrics;
    int stem_v = 0, stem_h = 0, cap_stem_v = 0, cap_stem_h = 0;
    int serif = 0, serif_t = 0;   // serif length past the stem, thickness; 0 = sans
    int mono = 0;                 // every advance; 0 = proportional
    int space = 250;
    std::string charset;          // unicode::charset id
    struct Shape { int width = 0, left = 0, right = 0; };
    std::map<std::string, Shape> shapes;   // n o H O p v
    std::vector<Guide> guides;
};

// The six glyphs a preset draws, in drawing order.
const std::vector<std::string>& control_glyphs();

// The bundled presets, in the chooser's order; the shared [keys] table.
const std::vector<Preset>& presets();
const Preset* preset(const std::string& id);
const std::vector<std::pair<std::string, MetricsKeys::Key>>& preset_keys();

// Parse a presets file (exposed for the selftest). Problems go to `errors`
// ("line 12: ...") and the bad line is skipped.
struct PresetFile {
    std::vector<Preset> presets;
    std::vector<std::pair<std::string, MetricsKeys::Key>> keys;
};
PresetFile parse_presets(std::string_view text, std::vector<std::string>* errors = nullptr);

// The control glyphs drawn for `p` (unicodes, advance, status Drafted),
// integral. `notes` gets a line for any part merge that fell back.
std::vector<Glyph> starter_glyphs(const Preset& p, std::vector<std::string>* notes = nullptr);
// s09: the preset's starter PARTS (M14c parts, left out of the compile), from
// the same pieces as the control glyphs: _part.stem (a lowercase stem with its
// serifs), _part.serif (the foot slab; serif presets only), _part.bowl (o's
// ring). Placed where n's stem / o's bowl sit, so a linked placement lands on
// the stem position.
std::vector<Glyph> starter_parts(const Preset& p, std::vector<std::string>* notes = nullptr);

struct NewFontOptions {
    std::string preset;          // an id; "" or unknown = a blank font (make_new_font)
    std::string family;          // "" = "Untitled"
    std::string charset;         // "" = the preset's; "@none" = control glyphs only
    bool        create_set = true;   // add the set's glyphs (empty / composites)
};
struct NewFontReport {
    int drawn = 0, added = 0, composites = 0, keys = 0, parts = 0;
    std::vector<std::string> notes;
};
void make_preset_font(Font& f, const NewFontOptions& o, NewFontReport* rep = nullptr);

}  // namespace granjon::model
