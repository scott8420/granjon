#pragma once
// views::OverviewGrid -- the Overview view (M3): every glyph as a card
// (drawing, name, codepoint), grouped by Unicode block under section headers
// like the navigator's, code point order within a block, unencoded glyphs
// last in glyph order.
//
//   toolbar     Coverage menu: the bundled char sets as a checklist (have /
//               total, missing count); the chosen set's gaps show as empty
//               cells in their blocks. "Create N missing" adds them all --
//               ONE undo step (command::add_glyphs). Cell-size slider.
//   click       a glyph: the shared selection. A gap: says what it is.
//   double-     a glyph: opens it in the editor. A gap: creates that glyph
//   click       (the add verb, so it undoes) and selects it.
//
// While the header search narrows (ViewContext::filter) gaps hide: the grid
// shows the hits only.
#include "views/ViewContext.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/drawingarea.h>
#include <gtkmm/label.h>
#include <gtkmm/popover.h>

#include <map>
#include <string>
#include <vector>

namespace granjon::views {

class OverviewGrid : public widgets::Box {
public:
    explicit OverviewGrid(ViewContext& ctx);
    void refresh();

    // The coverage set shown ("" = none) -- the selftest-free seam the shell
    // could persist later.
    const std::string& charset() const { return m_charset; }
    void set_charset(const std::string& id);

private:
    void build_toolbar();
    void rebuild_checklist();
    void update_coverage_ui();
    void sync_selection();
    void apply_cell_size();
    void on_selected(Gtk::FlowBox& flow);
    void on_activated(Gtk::FlowBoxChild* child);
    void create_missing();
    Gtk::FlowBoxChild* glyph_cell(const std::string& name);
    Gtk::FlowBoxChild* gap_cell(char32_t cp);

    ViewContext&           m_ctx;
    // toolbar
    widgets::Box           m_bar;
    widgets::MenuButton    m_coverage;
    Gtk::Popover           m_coverage_pop;
    widgets::ListBox       m_checklist;
    widgets::Button        m_create;
    widgets::Scale         m_size;
    // body
    widgets::ScrolledWindow m_scroll;
    widgets::Box           m_sections;
    std::vector<Gtk::FlowBox*>         m_flows;
    std::map<std::string, Gtk::FlowBoxChild*> m_cells;   // glyph name -> its cell
    std::vector<Gtk::DrawingArea*>     m_arts;
    std::vector<Gtk::Label*>           m_captions;

    std::string            m_charset;     // "" = no coverage set
    std::vector<char32_t>  m_missing;     // of m_charset, refreshed with the grid
    int                    m_cell = 84;   // drawing size, px
    bool                   m_syncing = false;
};

}  // namespace granjon::views
