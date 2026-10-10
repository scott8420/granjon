#pragma once
// views::SpacingStrip (M6) -- the Spacing view: a line of text set from the
// model (advances + class kerning; HarfBuzz arrives with export, M9), metric
// lines behind, sidebearings and kerns read out, all of it editable.
//
//   click a glyph            it becomes the selection (the inspector follows)
//   click between two        the pair is picked (the inspector's KERN card);
//                            drag or Left / Right (Shift x10) kern it, Alt = an
//                            exception; one undo step per drag, arrows merge
//   the band under a glyph   drag its left half: the LSB; its right half: the
//                            RSB (Alt slides the drawing) -- one step each;
//                            Ctrl on any drag: a tenth of the speed
//   Left / Right, no pair    the selection steps along the strip
//   Escape                   drop the pair, then leave the strip
#include "views/ViewContext.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/gesturedrag.h>

#include <string>
#include <vector>

namespace granjon::views {

class SpacingStrip : public widgets::Box {
public:
    explicit SpacingStrip(ViewContext& ctx);

    void refresh();          // the model changed: re-read the strip, redraw
    void focus_strip();      // came to the front

private:
    struct Cell {
        std::string name;
        int    index = 0;            // into m_names
        int    line = 0;
        double x = 0;                // origin, px
        double base = 0;             // baseline, px
        double adv = 0;              // advance, px
        int    kern = 0;             // with the previous cell on this line (font units)
        bool   line_start = false;
    };
    enum class Zone { None, Glyph, Pair, Left, Right };
    struct Hit { Zone zone = Zone::None; int cell = -1; };

    void reparse();
    void relayout(int w);
    Hit  hit_at(double x, double y) const;
    void on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void on_press(double x, double y);
    void on_drag(double dx, double dy);
    void on_release(double dx, double dy);
    bool on_key(guint keyval, guint keycode, Gdk::ModifierType state);
    void on_motion(double x, double y);
    void pick_pair(int right_cell);
    void drop_pair();
    void select_cell(int cell);
    void kern_by(int delta, bool exception);
    void say_pair();
    void set_size(int px);
    double scale() const;

    ViewContext& m_ctx;
    widgets::Box         m_bar;
    widgets::Entry       m_text;
    widgets::Scale       m_size;
    widgets::Label       m_size_label;
    widgets::Label       m_missing;
    widgets::DrawingArea m_area;
    Glib::RefPtr<Gtk::GestureDrag> m_drag;

    std::vector<std::string> m_names;     // the strip, glyph names
    std::vector<Cell>        m_cells;     // laid out at the last width
    int    m_laid_w = -1;
    int    m_sel = -1;                    // the clicked occurrence (cell index)
    int    m_pair = -1;                   // the picked pair's RIGHT cell
    // a drag in progress
    Zone   m_mode = Zone::None;
    int    m_drag_cell = -1;
    int    m_kern_press = 0;
    bool   m_exception = false;
    bool   m_alt_press = false;
    bool   m_fine = false;                // Ctrl at the press: a tenth of the speed
    double m_band_h = 24;                 // the sidebearing band under each line, px
    bool   m_syncing = false;             // settings round trip
};

}  // namespace granjon::views
