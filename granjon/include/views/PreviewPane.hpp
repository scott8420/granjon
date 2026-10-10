#pragma once
// views::PreviewPane (M9) -- the Preview view (Ctrl+4): the font as it
// compiles, set by the desktop's own text stack.
//
// Each compile (otf::compile_otf, the same bytes Export writes) goes to a
// private file under ~/.cache/granjon/preview/ and into a PRIVATE Pango font
// map whose fontconfig holds that one font and nothing else: no installed copy
// of the family can answer for it, the rest of the UI never sees it, and a
// character the font lacks shows as missing. Pango shapes it (HarfBuzz under
// Pango), so kerning here is the compiled GPOS, not the strip's own layout.
//
//   compiles    when shown and the font changed (a short pause after the
//               last step; a drag compiles once, at its release)
//   bar         sample text (remembered), Kerning on / off, the other mode
//               (the page follows the desktop's light / dark; the button,
//               labelled with the mode it would show, flips it), and what the
//               compile made (glyphs, size, ms)
//   page        a waterfall of the sample, then a paragraph
#include "views/ViewContext.hpp"
#include "widgets/Widgets.hpp"

#include <sigc++/connection.h>

#include <string>

typedef struct _PangoFontMap PangoFontMap;
typedef struct _PangoContext PangoContext;

namespace granjon::views {

class PreviewPane : public widgets::Box {
public:
    explicit PreviewPane(ViewContext& ctx);
    ~PreviewPane() override;

    void refresh();          // the font changed: compile when shown

private:
    void schedule(int ms);
    void compile();
    void on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void update_missing();
    void drop_font();
    bool desktop_dark() const;
    void label_mode();

    ViewContext& m_ctx;
    widgets::Box          m_bar;
    widgets::Entry        m_text;
    widgets::ToggleButton m_kern;
    widgets::ToggleButton m_invert;
    widgets::Label        m_missing;
    widgets::Label        m_info;
    widgets::ScrolledWindow m_scroll;
    widgets::DrawingArea  m_area;

    PangoFontMap*   m_fontmap = nullptr;   // private, one font
    PangoContext*   m_pctx = nullptr;
    std::string     m_family;              // as compiled
    std::string     m_file;                // the current preview file
    int             m_serial = 0;
    bool            m_stale = true;
    bool            m_syncing = false;
    sigc::connection m_timer;
};

}  // namespace granjon::views
