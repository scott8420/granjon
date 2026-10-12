#include "core/Shortcuts.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace granjon::core {

// ─────────────────────────────────────────────────────────────────────────────
// Accel formatting
// ─────────────────────────────────────────────────────────────────────────────
namespace {

std::string lower(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) o += char(std::tolower((unsigned char)c));
    return o;
}

// GTK key name -> display glyph. Single alpha keys uppercase; named keys map to a
// glyph where one reads better, otherwise the name is prettified.
std::string map_key(const std::string& key) {
    if (key.empty()) return "";
    static const std::map<std::string, std::string> named = {
        {"comma", ","},     {"period", "."},     {"question", "?"},
        {"slash", "/"},     {"minus", "\u2212"}, {"plus", "+"},
        {"equal", "="},     {"space", "Space"},
        {"Return", "Enter"},{"Escape", "Esc"},
        {"Left", "\u2190"}, {"Right", "\u2192"}, {"Up", "\u2191"}, {"Down", "\u2193"},
        {"Delete", "Del"},  {"BackSpace", "Backspace"},
        {"Page_Up", "Page Up"}, {"Page_Down", "Page Down"},
        {"KP_Add", "Keypad +"}, {"KP_Subtract", "Keypad \u2212"}, {"KP_0", "Keypad 0"},
    };
    auto it = named.find(key);
    if (it != named.end()) return it->second;
    if (key.size() == 1) {
        std::string s = key;
        s[0] = char(std::toupper((unsigned char)s[0]));
        return s;
    }
    std::string s = key;
    for (char& c : s)
        if (c == '_') c = ' ';
    return s;
}

// Split an accel into its modifier flags + bare key name. Shared by
// format_accel and any future consumer so they can't disagree.
struct Parsed { bool ctrl = false, alt = false, shift = false, super = false; std::string key; };

Parsed parse_accel(const std::string& accel) {
    Parsed p;
    std::size_t i = 0;
    while (i < accel.size()) {
        if (accel[i] == '<') {
            std::size_t j = accel.find('>', i);
            if (j == std::string::npos) break;
            const std::string ml = lower(accel.substr(i + 1, j - i - 1));
            if (ml == "ctrl" || ml == "control" || ml == "primary") p.ctrl = true;
            else if (ml == "alt" || ml == "mod1")                   p.alt = true;
            else if (ml == "shift")                                 p.shift = true;
            else if (ml == "super" || ml == "meta" || ml == "mod4") p.super = true;
            i = j + 1;
        } else {
            p.key = accel.substr(i);
            break;
        }
    }
    return p;
}

}  // namespace

std::string format_accel(const std::string& accel) {
    const Parsed p = parse_accel(accel);
    const std::string k = map_key(p.key);
    std::string out;
    auto add = [&](const std::string& s) {
        if (!out.empty()) out += "+";
        out += s;
    };
    if (p.ctrl)  add("Ctrl");
    if (p.alt)   add("Alt");
    if (p.shift) add("Shift");
    if (p.super) add("Super");
    if (!k.empty()) add(k);
    return out;
}

std::string ShortcutSpec::display_keys() const {
    if (!keys.empty()) return keys;
    std::string out;
    for (const auto& a : accels) {
        if (!out.empty()) out += "  /  ";
        out += format_accel(a);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// The registry  (authored section [A-Z] -> row)
// ─────────────────────────────────────────────────────────────────────────────
const std::vector<ShortcutSpec>& shortcut_registry() {
    // Fields: {section, action, accels, keys, description}
    // action non-empty => wired via set_accels_for_action. keys empty => derived.
    // Deliberately small -- a seed proves the SHAPE, not a product's key set. It
    // carries one doc-only row so that half of the paradigm is exercised too.
    static const std::vector<ShortcutSpec> kReg = {
        // ── Diagnostics ───────────────────────────────────────────────────────
        {"Diagnostics", "win.dump-registry", {"<Ctrl><Shift>d"}, "",
         "Dump the widget registry to the log"},

        // ── Edit (s046) ───────────────────────────────────────────────────────
        // Doc-only ON PURPOSE: an accelerator would take Ctrl+Z from every text
        // box. The window hears these instead (Shell_bindings.cpp).
        {"Edit", "", {}, "Ctrl+Z", "Undo -- anywhere but a text box (which keeps its own)"},
        {"Edit", "", {}, "Ctrl+Shift+Z  /  Ctrl+Y", "Redo"},

        // ── Editor ────────────────────────────────────────────────────────────
        // Doc-only: the canvas handles these itself while it has the keyboard.
        {"Editor", "", {}, "Click / Shift+click", "Pick a node, handle or anchor / add or remove it"},
        {"Editor", "", {}, "Click a segment", "Pick its two end nodes"},
        {"Editor", "", {}, "Click inside a component", "Pick the component (drag or nudge moves its offset)"},
        {"Editor", "", {}, "Delete / Backspace", "Delete the picked anchors and components"},
        {"Editor", "", {}, "Drag empty canvas", "Marquee (Shift adds)"},
        {"Editor", "", {}, "Drag the pick", "Move it -- one undo step, whole units on release"},
        {"Editor", "", {}, "Shift+drag", "Lock to horizontal / vertical (a handle: from its node)"},
        {"Editor", "", {}, "Alt+drag a handle", "Move it alone -- its node turns Corner"},
        {"Editor", "", {}, "Ctrl+drag", "Fine drag: a tenth of the speed, no snapping"},
        {"Editor", "", {}, "Double-click a node", "Corner <-> smooth"},
        {"Editor", "", {}, "Drag out of a ruler", "A new guide (top: horizontal, left: vertical) -- this glyph"},
        {"Editor", "", {}, "Drag a guide onto a ruler", "Delete it (or Delete with it picked)"},
        {"Editor", "", {}, "Hold M + drag", "Measure: distance, angle, delta -- edits nothing"},
        {"Editor", "", {}, "Hold M + click", "The stems through that point, across and up"},
        {"Editor", "", {}, "Drag the left line", "Move the origin; the drawing stays put"},
        {"Editor", "", {}, "Alt+drag the left line", "Slide the drawing inside the width"},
        {"Editor", "", {}, "Drag the right line", "Change the width"},
        {"Editor", "", {}, "Arrows / Shift+arrows", "Nudge 1 / 10 units"},
        {"Editor", "", {}, "V", "Select (the solid arrow): whole paths -- click a path or inside it, Shift+click adds / removes, a box takes every path it touches; double-click goes into Pick"},
        {"Editor", "", {}, "A", "Pick (the hollow arrow): nodes and handles (the tool panel, left of the canvas: Select, Pick, Pen, Rectangle, Ellipse, Line, Measure, Hand; Fill, Context, Grid, Snap)"},
        {"Editor", "", {}, "P", "The pen: click a corner, drag a curve (Alt: out-handle alone, Shift: 45 degrees), click the first node to close, click an open end to go on"},
        {"Editor", "", {}, "R  /  E  /  \\", "Rectangle / Ellipse / Line: drag to draw (Shift: square, circle, 45 degrees; Alt: from the centre) -- one closed contour, picked, one undo step"},
        {"Editor", "", {}, "Enter  /  Escape (pen)", "End the path; Escape again leaves the pen"},
        {"Editor", "", {}, "Delete  /  Backspace", "Delete the picked nodes (the curve is refit -- the shape holds), retract picked handles, delete anchors / components"},
        {"Editor", "", {}, "Double-click a segment", "Add a node there (the shape does not change)"},
        {"Editor", "", {}, "L  /  C", "Make the segments between picked nodes lines / curves"},
        {"Editor", "", {}, "Ctrl+A", "Pick every node, anchor and component"},
        {"Editor", "", {}, "Ctrl+D", "Duplicate the picked paths in place, on top -- the copies come picked (drag or nudge them)"},
        {"Editor", "", {}, "Alt+D", "Clone the picked paths: copies offset right and down (2 % of the em), on top, picked"},
        {"Editor", "", {}, "Ctrl+]  /  Ctrl+[", "Bring forward / send backward the picked paths (the top picked path is the one Subtract and Intersect use)"},
        {"Editor", "", {}, "Ctrl+Shift+]  /  Ctrl+Shift+[", "Bring to front / send to back the picked paths"},
        {"Editor", "", {}, "Tab  /  Shift+Tab", "Next / previous node along the contour (nothing picked: the first start)"},
        {"Editor", "", {}, "Ctrl+Tab  /  Ctrl+Shift+Tab", "Next / previous contour's start node"},
        {"Editor", "", {}, "`  /  Shift+`", "Next / previous point, handles included"},
        {"Editor", "", {}, "F", "Make the picked node its contour's start -- one undo step"},
        {"Editor", "", {}, "Escape", "Cancel the drag, else drop the pick, else leave the canvas (it keeps Tab)"},
        {"Editor", "", {}, "Ctrl+0", "Fit the glyph"},
        {"Editor", "", {}, "Ctrl+=  /  Ctrl+-", "Zoom in / out"},
        {"Editor", "", {}, "Ctrl+scroll", "Zoom toward the pointer"},
        {"Editor", "", {}, "Scroll, middle-drag, Space+drag", "Pan"},

        // ── General ───────────────────────────────────────────────────────────
        {"General", "win.new",           {"<Ctrl>n"}, "", "New font (choose a preset)"},
        {"General", "win.open",          {"<Ctrl>o"}, "", "Open a font (.granjon / .ufo folder)"},
        {"General", "win.import-font",   {"<Ctrl><Shift>o"}, "", "Import an OTF / TTF as a new font (Save keeps it as .granjon)"},
        {"General", "win.save",          {"<Ctrl>s"}, "", "Save"},
        {"General", "win.save-as",       {"<Ctrl><Shift>s"}, "", "Save as"},
        {"General", "win.export-otf",    {"<Ctrl>e"}, "", "Export OTF (the font compiled, as the Preview shows it)"},
        {"General", "win.proof-print",   {"<Ctrl>p"}, "", "Print proof (waterfall, spacing, kerning, features, every glyph)"},
        {"General", "win.export-ttf",    {"<Ctrl><Shift>e"}, "", "Export TTF (quadratic outlines)"},
        {"General", "win.shortcuts",     {"<Ctrl>question", "<Ctrl>slash"}, "",
         "Keyboard shortcuts (this window)"},
        {"General", "win.primer",        {"F1"}, "", "Primer: short notes on drawing type (overshoot, stems, spacing ...)"},
        {"General", "win.about",         {}, "", "About Granjon (the logo, top left)"},
        {"General", "win.preferences",   {"<Ctrl>comma"}, "", "Preferences"},
        {"General", "win.quit",          {"<Ctrl>q"}, "", "Quit"},

        // ── Mouse ─────────────────────────────────────────────────────────────
        // Doc-only: a gesture is not a GAction, but the reader still needs it.
        {"Mouse", "", {}, "Click a recent", "Open that font"},
        {"Mouse", "", {}, "Double-click a glyph", "Open it in the editor"},
        {"Mouse", "", {}, "Click a navigator section", "Show its glyphs in the overview (Ctrl+click adds sections)"},
        {"Mouse", "", {}, "Ctrl / Shift+click navigator glyphs", "Pick several -- the overview shows just those"},
        {"Mouse", "", {}, "Double-click in the navigator", "Open the pick (or the section) in editor tabs"},
        {"Mouse", "", {}, "Click a section's arrow", "Fold / open it (Ctrl+click: all); remembered"},

        // ── View ──────────────────────────────────────────────────────────────
        {"Report", "", {}, "Click a finding", "Open the glyph in the editor with the spot picked"},
        {"Report", "", {}, "Fix / Skip / Fix all", "Each click is one undo step; skips are saved with the font"},
        {"Spacing", "", {}, "Click a glyph", "Select it (the inspector follows)"},
        {"Spacing", "", {}, "Click between two glyphs", "Pick the pair (the inspector's KERN card)"},
        {"Spacing", "", {}, "Drag a picked pair  /  Left, Right", "Kern it (Shift x10) -- the entry in effect, else the groups' pair"},
        {"Spacing", "", {}, "Alt + drag / arrows", "Kern an exception: just these two glyphs"},
        {"Spacing", "", {}, "Drag under a glyph", "Left half: its LSB; right half: its RSB (Alt slides the drawing)"},
        {"Spacing", "", {}, "Ctrl + any drag", "A tenth of the speed"},
        {"Spacing", "", {}, "Left / Right, no pair", "Step the selection along the strip"},
        {"Spacing", "", {}, "Ctrl+scroll", "Size"},
        {"Spacing", "", {}, "Escape", "Drop the pair, else leave the strip"},
        {"View", "win.view::overview", {"<Ctrl>1"}, "", "Overview"},
        {"View", "win.view::editor",   {"<Ctrl>2"}, "", "Editor"},
        {"View", "win.view::spacing",  {"<Ctrl>3"}, "", "Spacing"},
        {"View", "win.view::preview",  {"<Ctrl>4"}, "", "Preview"},
        {"View", "win.view::report",   {"<Ctrl>5"}, "", "Report"},
        {"View", "win.view::features", {"<Ctrl>6"}, "", "Features (the feature file)"},
        {"View", "win.show-navigator", {"F9"}, "", "Show / hide the navigator"},
        {"View", "win.show-inspector", {"<Ctrl>i"}, "", "Show / hide the inspector"},
        {"View", "win.other-case",     {"<Alt>c"}, "", "Go to the other case of the glyph (A \u2194 a)"},
        {"View", "win.find",           {"<Ctrl>f", "<Ctrl>k"}, "", "Find a glyph: name, character, U+code, /name"},


    };
    return kReg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Collision detection
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> find_accel_collisions() {
    std::map<std::string, std::pair<int, bool>> seen;  // accel -> {count, any_action}
    for (const auto& s : shortcut_registry())
        for (const auto& a : s.accels) {
            auto& e = seen[a];
            e.first += 1;
            if (!s.action.empty()) e.second = true;
        }
    std::vector<std::string> out;
    for (const auto& [accel, e] : seen)
        if (e.first >= 2 && e.second) out.push_back(accel);  // std::map keeps it sorted+unique
    return out;
}

}  // namespace granjon::core
