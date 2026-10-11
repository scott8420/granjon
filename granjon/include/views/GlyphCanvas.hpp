#pragma once
// views::GlyphCanvas -- the Editor view (M4): the Curvz canvas, ported to a
// glyph in font units.
//
//   draws       metric guides (baseline, x-height, cap height, ascender,
//               descender) with their overshoot zones; the two sidebearing
//               lines with LSB / width / RSB; fixed context glyphs ghosted
//               either side (H for capitals and figures, n otherwise);
//               components (read-only, tinted); the outline, shaded or not
//               (the toolbar's Fill toggle); nodes and handles in Curvz's
//               shapes; anchors with their names.
//   click       a node / handle / anchor picks it (Shift adds or removes); a
//               segment picks its two end nodes; empty canvas starts a
//               marquee (Shift adds to the pick).
//   drag        the pick moves -- press .. release is ONE undo step, snapped
//               to whole units on release (command::drag_points). Escape
//               mid-drag puts it back.
//   sidebearing drag the right line: the width. Drag the left line: the
//               ORIGIN moves and the drawing stays where it is on screen
//               (the RSB holds). With Alt: the drawing slides inside a fixed
//               width instead.
//   keys        arrows nudge 1 unit (Shift: 10), merged into one step while
//               pressed in a row; Ctrl+A picks every node; Escape drops the
//               pick (or cancels a drag); Ctrl+0 fits; Ctrl+= / Ctrl+- zoom.
//   view        Ctrl+scroll zooms toward the pointer, scroll pans; middle-
//               drag or Space+drag pans.
//   image       (M13) the glyph's background image, behind everything. Drop
//               a PNG / JPEG on the canvas to place one. Unlocked: click it
//               to pick it, drag to move, drag a corner to scale (Shift: from
//               the centre), Delete removes. Locked: clicks go through.
//
// The y-up flip lives here and nowhere else (ARCHITECTURE §3): screen x =
// ox + x * s, screen y = oy - y * s.
#include "geom/Edit.hpp"
#include "views/ViewContext.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/gesturedrag.h>

#include <map>
#include <optional>
#include <vector>

#include <string>

namespace granjon::views {

class GlyphCanvas : public widgets::Box {
public:
    explicit GlyphCanvas(ViewContext& ctx);

    void refresh();                 // the model changed (or the selection did)
    void fit();                     // the glyph's width and the em, centred
    void focus_canvas();
    // Editor tabs (Scott: "if i double-click in navigator the set should open
    // in tabs for editing"). One tab per glyph; the selected glyph is the
    // front tab; a glyph selected elsewhere joins as a tab.
    void open_tabs(const std::vector<std::string>& names, const std::string& active);
    void clear_tabs();            // the editor came to the front: keys go to the canvas
    // M12 (the Primer's links): pick a tool by name -- "pick", "pen",
    // "measure", "hand". False for an unknown name.
    bool use_tool(const std::string& name);
    bool shading() const { return m_fill.get_active(); }

private:
    // ── view transform ─────────────────────────────────────────────────────
    geom::Vec2 to_screen(geom::Vec2 p) const { return {m_ox + p.x * m_s, m_oy - p.y * m_s}; }
    geom::Vec2 to_font(double x, double y) const { return {(x - m_ox) / m_s, (m_oy - y) / m_s}; }
    void zoom_at(double factor, double sx, double sy);
    void update_zoom_label();

    // ── drawing ────────────────────────────────────────────────────────────
    void on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void draw_guides(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h, const model::Glyph& g);
    void draw_overlay(const Cairo::RefPtr<Cairo::Context>& cr, const model::Glyph& g);
    void draw_anchors(const Cairo::RefPtr<Cairo::Context>& cr, const model::Glyph& g);
    std::string context_glyph(const model::Glyph& g) const;   // "H" / "n" or "" if the font lacks it

    // ── input ──────────────────────────────────────────────────────────────
    enum class Mode { None, Points, Sidebearing, Marquee, Pan, Guide, Measure, Pen, Image };
    int  sidebearing_at(double sx, double sy) const;   // -1 none, 0 left, 1 right
    void on_press(double x, double y);
    void on_drag(double dx, double dy);
    void on_release(double dx, double dy);
    void cancel_drag();
    bool on_key(guint keyval, guint keycode, Gdk::ModifierType state);
    void on_key_released(guint keyval, guint keycode, Gdk::ModifierType state);
    void on_motion(double x, double y);
    void say_pick();
    // M5e: walk the pick along the outline. dir +1 / -1; `contour` jumps to
    // the next / previous contour's start; `handles` visits in / out handles.
    void step_pick(int dir, bool contour, bool handles);
    void keep_in_view(geom::Vec2 font_pt);   // pan just enough
    void make_first();                       // the one picked node leads its contour
    const model::Glyph* glyph() const;

    void rebuild_tabs();
    void close_tab(const std::string& name);

    ViewContext&         m_ctx;
    widgets::ScrolledWindow m_tab_scroll;
    widgets::Box         m_tabs;
    std::vector<std::string> m_tab_names;
    widgets::Box         m_bar;
    widgets::Label       m_title;
    widgets::ToggleButton m_context;
    widgets::ToggleButton m_fill;
    widgets::ToggleButton m_grid;           // the zoom-adaptive grid (a preference)
    widgets::ToggleButton m_snap;           // snapping (a preference)
    widgets::ToggleButton m_image;          // M13: show the background image (a preference)
    widgets::ToggleButton m_pen;            // M5g: the pen tool (P)
    // M5h: the tool panel down the canvas's left edge -- tools (one at a time)
    // above, the view toggles below.
    widgets::ToggleButton m_tool_pick;      // V
    widgets::ToggleButton m_tool_measure;   // (hold M still works)
    widgets::ToggleButton m_tool_hand;      // (hold Space still works)
    widgets::Box          m_tools;
    widgets::Box          m_work;           // the panel | the canvas
    enum class Tool { Pick, Pen, Measure, Hand };
    Tool tool() const;
    void set_tool(Tool t);
    void on_tool_changed();
    widgets::Button      m_fit_button;
    widgets::Button      m_anchors;          // default anchors for this glyph
    widgets::Label       m_readout;          // pointer x, y -- or the drag's delta
    widgets::Label       m_zoom;
    widgets::DrawingArea m_area;
    Glib::RefPtr<Gtk::GestureDrag> m_drag, m_pan;
    Glib::RefPtr<Gtk::GestureClick> m_click;   // double-click a node: corner <-> smooth

    double m_s = 0.5, m_ox = 0, m_oy = 0;   // px per unit; the origin on screen
    bool   m_fitted = false;                // fit on the first draw with a size
    std::string m_shown;                    // the glyph the pick belongs to

    geom::Points& m_pick;                   // = ViewContext::pick (the inspector reads it)
    Mode   m_mode = Mode::None;
    bool   m_began = false;                 // the journal gesture is open
    bool   m_space = false;
    int    m_side = -1, m_hover_side = -1;
    double m_press_x = 0, m_press_y = 0;    // screen
    double m_press_ox = 0, m_press_oy = 0;
    double m_press_advance = 0;
    double m_mx = 0, m_my = 0;              // marquee corner, screen
    geom::Points m_press_pick;              // for Shift+marquee
    model::Glyph m_press_glyph;             // the glyph at the press (snapping reads it)
    geom::PointRef m_lead;
    std::optional<geom::PointRef> m_unpick;   // Shift-pressed on a picked point: drop it if no drag follows                  // the point under the pointer at the press -- the one that snaps
    std::optional<double> m_snap_x, m_snap_y;   // what the lead snapped to (drawn while dragging)
    void draw_grid(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void draw_user_guides(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h, const model::Glyph& g);
    void draw_rulers(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void draw_directions(const Cairo::RefPtr<Cairo::Context>& cr, const model::Glyph& g, int w);
    void draw_measure(const Cairo::RefPtr<Cairo::Context>& cr, const model::Glyph& g);
    std::optional<ViewContext::GuideRef> guide_at(double sx, double sy) const;
    const model::Guide* picked_guide() const;
    geom::Vec2 snap_to_node(geom::Vec2 p) const;   // the measure tool's ends
    // guides (M5c)
    bool        m_guide_new = false;        // dragging a new guide out of a ruler
    bool        m_guide_h = true;           // ... horizontal (top ruler) or vertical (left ruler)
    geom::Vec2  m_guide_preview;
    model::Guide m_guide_press;             // an existing guide at the press
    // measure (hold M)
    bool        m_measure = false;
    bool        m_meas_dragged = false;
    geom::Vec2  m_meas_a, m_meas_b;
    std::optional<std::pair<double, double>> m_stem_h, m_stem_v;   // stems at the click
    geom::Vec2  m_stem_at;
    void update_readout(double sx, double sy);
    // the pen (M5g)
    std::string m_pen_glyph;                // the path being drawn belongs to this glyph
    int         m_pen_contour = -1;         // ... and is this open contour (-1: none)
    bool        m_pen_from_start = false;   // the next node continues it from its first node
    int         m_pen_at_contour = -1, m_pen_at_node = -1;   // the node the press put in
    bool        m_pen_at_closed = false;
    void set_pen(bool on);
    void end_path(bool say_it = true);
    bool pen_path_live() const;             // a path is being drawn in the glyph shown
    void draw_pen(const Cairo::RefPtr<Cairo::Context>& cr, const model::Glyph& g);
    // the background image (M13)
    bool m_image_picked = false;            // its frame and corner handles show; Delete removes it
    int  m_image_handle = -1;               // the drag: -1 moves, 0..3 scales from that corner
    model::GlyphImage m_image_press;        // the image at the press
    bool image_live(const model::Glyph& g) const;   // shown, unlocked, with its pixels
    bool image_px(const model::GlyphImage& im, int& w, int& h) const;
    geom::Vec2 image_corner(const model::GlyphImage& im, int w, int h, int i) const;   // font units, 0..3 ccw from bottom-left
    int  image_handle_at(const model::Glyph& g, double sx, double sy) const;         // -1 none
    bool image_inside(const model::Glyph& g, double sx, double sy) const;
    void draw_image(const Cairo::RefPtr<Cairo::Context>& cr, const model::Glyph& g);
    // Decoded pixels, keyed by the pool's bytes (the shared_ptr keeps the key alive).
    std::map<const std::string*, std::pair<model::Font::ImageBytes, Cairo::RefPtr<Cairo::ImageSurface>>> m_surfaces;
};

}  // namespace granjon::views
