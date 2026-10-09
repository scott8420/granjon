#pragma once
// views::Inspector -- the right panel: Keynote/Figma-style cards about the
// selection. A glyph selected: Glyph (preview, name, status), Metrics (width,
// sidebearings), Unicode, Note. Nothing (the font) selected: Names, Grid,
// Vertical metrics -- what M1 called the Info view.
//
// Every field is a verb (command/): a text field commits on Enter or on
// leaving it; a spin commits per change and presses within 1.5 s merge.
// Refusals (a bad name, a duplicate) come back on the status line, and the
// field returns to the model's value.
#include "model/FontRecords.hpp"
#include "views/ViewContext.hpp"
#include "widgets/Widgets.hpp"

#include <array>

namespace granjon::views {

class Inspector : public widgets::ScrolledWindow {
public:
    explicit Inspector(ViewContext& ctx);

    void refresh();                    // re-read the model (after writes / selection)
    void focus_name();                 // put the keyboard in the glyph-name field

private:
    void build_font_page();
    void build_glyph_page();
    void on_family();
    void on_style();
    void on_upm();
    void on_metric(model::Metrics::Field f);
    void on_name();
    void on_status();
    void on_width();
    void on_unicodes();
    void on_note();

    ViewContext&        m_ctx;
    widgets::Stack      m_pages;
    bool                m_painting = false;

    // font
    widgets::Entry      m_family;
    widgets::Entry      m_style;
    widgets::SpinButton m_upm;
    std::array<widgets::SpinButton*, 5> m_metric{};

    // glyph
    widgets::DrawingArea m_preview;
    widgets::Label       m_title;
    widgets::Entry       m_name;
    widgets::DropDown    m_status;
    widgets::SpinButton  m_width;
    widgets::Label       m_lsb;
    widgets::Label       m_rsb;
    widgets::Entry       m_unicodes;
    widgets::Label       m_section;
    widgets::Label       m_uname;      // UCD name of the first code point
    widgets::Label       m_category;
    widgets::Button      m_case;       // "Go to a" -- case navigation
    widgets::Entry       m_note;
};

}  // namespace granjon::views
