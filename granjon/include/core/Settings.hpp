#pragma once
// core::Settings -- the app's preferences: one typed struct, one JSON file.
//
// GTK-free, headless-testable, encode + decode adjacent (the Recents pump
// shape). The UI resolves the path (~/.config/granjon/settings.json) and
// hands it in.
//
// Tolerant on load: a missing file, a missing key, a wrong type or an
// out-of-range value each fall back to that field's default -- an old or
// hand-edited file never breaks a launch, and a new field needs no migration.
// Unknown keys are dropped on the next save.
//
// Preferences are NOT undoable: they are the app's state, not the font's.
// The undo journal is for the document (ARC s03, M4b).
#include <string>
#include <vector>

namespace granjon::core {

constexpr int kSpacingSizeMin = 24, kSpacingSizeMax = 400;

struct Settings {
    // ── General ───────────────────────────────────────────────────────────
    bool        reopen_last  = true;   // open the last font at launch
    std::string last_font;             // its path; "" = none / it was untitled
    std::string open_folder;           // where the Open dialog starts

    // ── Editor ────────────────────────────────────────────────────────────
    bool        editor_fill     = true;    // shade the outline
    bool        editor_context  = true;    // ghost context glyphs
    std::string context_upper   = "H";     // beside capitals and figures (a glyph name or a character)
    std::string context_lower   = "n";     // beside the rest
    std::string sidebearing_mod = "alt";   // "alt" | "ctrl" | "shift": slide the drawing on a left-edge drag
    bool        editor_grid     = false;   // the zoom-adaptive grid
    bool        editor_snap     = true;    // snap drags to grid / metrics / sidebearings / other nodes
    bool        editor_align    = true;    // s09: alignment guides while a node is dragged
    bool        editor_image    = true;    // M13: show the background image
    std::string image_tint      = "#e0703c";   // M13: the ink colour a background image is drawn in (#rrggbb),
                                               // unless the image carries its own (UFO color)
    int         image_opacity   = 30;      // M13: the opacity a newly placed image gets, percent (5..100)
    int         line_width      = 0;       // M14a: the Line tool's stroke, font units (0 = the font's stem)
    bool        handles_relative = true;   // the inspector shows handles relative to their node

    // ── Navigator ─────────────────────────────────────────────────────────
    // The sections (Unicode blocks, "Unencoded") shown open; the rest are
    // folded to their header. Empty = all folded.
    std::vector<std::string> navigator_open;

    // ── Spacing (M6) ──────────────────────────────────────────────────────
    std::string spacing_text = "nnHOHnn nonon HOHOH";   // the strip's text (the /name syntax works)
    int         spacing_size = 96;                     // px per em, kSpacingSizeMin..Max

    // ── Preview + export (M9) ─────────────────────────────────────────────
    std::string preview_text = "Hamburgefonstiv HOHOHO nonono";   // the waterfall's sample
    bool        preview_kern = true;     // the compiled kerning on
    bool        preview_invert = false;  // show the page in the OTHER mode than the desktop's
    std::string export_folder;           // where the Export dialog starts
    std::string code_format = "cff";     // the inspector CODE card (M9c): "cff" | "truetype" | "svg"

    // ── Overview ──────────────────────────────────────────────────────────
    int         overview_cell    = 84;     // px, kOverviewCellMin..Max
    std::string overview_charset;          // "" = no coverage set

    // ── New font (M12) ────────────────────────────────────────────────────
    std::string new_preset = "sans";       // the chooser's last pick ("" = blank)
    bool        new_create_set = true;     // add the set's glyphs

    bool operator==(const Settings&) const = default;
};

inline constexpr int kOverviewCellMin = 40, kOverviewCellMax = 200;

Settings load_settings(const std::string& file);
bool     save_settings(const std::string& file, const Settings& s);

}  // namespace granjon::core
