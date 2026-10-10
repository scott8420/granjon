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
    void build_kern_page();            // M6: the Spacing view's picked pair
    void refresh_kern();
    void on_key_rule();
    void on_family();
    void on_style();
    void on_upm();
    void on_metric(model::Metrics::Field f);
    void on_name();
    void on_status();
    void on_width();
    void on_unicodes();
    void on_note();
    void refresh_pick();               // the PICK card (one picked point)
    void refresh_parts(const model::Glyph& g);   // the COMPONENTS / ANCHORS lists
    void on_pick_xy();
    void on_handle_xy(bool out);       // the In / Out rows of a picked node
    void on_node_type(int type);
    void refresh_guide();              // the GUIDE card (a picked guide, M5c)
    void on_guide_field();
    void on_anchor_name();

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
    // metrics key (M6): one rule line, its state, apply
    widgets::Entry       m_key;
    widgets::Label       m_key_state;
    widgets::Button      m_key_apply;
    // kern page (M6)
    widgets::Label       m_kern_title;
    widgets::DrawingArea m_kern_preview;
    widgets::Label       m_kern_left;
    widgets::Label       m_kern_right;
    widgets::Label       m_kern_entry;
    widgets::SpinButton  m_kern_value;
    widgets::Button      m_kern_exception;
    widgets::Button      m_kern_remove;
    // the pair's readouts (M8)
    widgets::Label       m_kern_contact;
    widgets::Label       m_kern_optical;
    widgets::Label       m_kern_control;
    widgets::Label       m_kern_flag;
    widgets::Button      m_kern_to_gap;
    widgets::Entry       m_unicodes;
    widgets::Label       m_section;
    widgets::Label       m_uname;      // UCD name of the first code point
    widgets::Label       m_category;
    widgets::Button      m_case;       // "Go to a" -- case navigation
    widgets::Entry       m_note;

    // pick (M5): one picked node / handle / anchor / component
    Gtk::Box*            m_pick_card = nullptr;
    widgets::Label       m_pick_what;
    widgets::SpinButton  m_px;
    widgets::SpinButton  m_py;
    Gtk::Label*          m_anchor_name_label = nullptr;
    // node type for every picked node (M5b): Corner | Smooth | Symmetric | Cusp
    widgets::Box         m_types;
    std::array<Gtk::ToggleButton*, 4> m_type_buttons{};
    Gtk::Label*          m_types_label = nullptr;
    // a picked node's handles (M5b), relative to the node or absolute
    widgets::ToggleButton m_relative;
    widgets::Entry       m_inx, m_iny, m_outx, m_outy;   // compact: Enter / leaving commits
    Gtk::Box*            m_in_row = nullptr;
    Gtk::Box*            m_out_row = nullptr;
    Gtk::Label*          m_in_label = nullptr;
    Gtk::Label*          m_out_label = nullptr;
    Gtk::Label*          m_x_label = nullptr;
    Gtk::Label*          m_y_label = nullptr;
    // align / distribute the picked nodes + anchors (M5c)
    Gtk::Box*            m_align_rows = nullptr;
    // guide (M5c)
    Gtk::Box*            m_guide_card = nullptr;
    widgets::Label       m_guide_what;
    widgets::SpinButton  m_gx, m_gy, m_gangle;
    Gtk::Label*          m_gx_label = nullptr;
    Gtk::Label*          m_gy_label = nullptr;
    widgets::Entry       m_gname;
    widgets::ToggleButton m_gfont;
    widgets::Button      m_gdelete;
    widgets::Entry       m_anchor_name;
    // components + anchors (M5)
    widgets::Box         m_comp_list;
    widgets::Entry       m_comp_add;
    widgets::Button      m_decompose;
    widgets::Box         m_anchor_list;
    widgets::Entry       m_anchor_add;
    widgets::Button      m_default_anchors;
    // outline (M5d): contour count, direction, Correct / Reverse
    widgets::Label       m_outline_info;
    widgets::Button      m_correct;
    widgets::Button      m_reverse;
    widgets::Button      m_make_first;   // M5e: the picked node leads its contour
};

}  // namespace granjon::views
