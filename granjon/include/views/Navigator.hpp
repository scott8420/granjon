#pragma once
// views::Navigator -- the left panel: a Mac-style source list.
//
//   FONT
//     ● Untitled Regular          <- selecting it shows the font in the inspector
//   BASIC LATIN              4
//     ● H                 U+0048
//     ...
//   UNENCODED                1
//     ○ .notdef
//   ─────────────────────────────
//   [+] [−]              7 glyphs
//
// Sections are Unicode blocks (unicode/Blocks.hpp), in glyph order within a
// section. The dot is the glyph's status. The header search (GlyphSearch)
// narrows the list through ViewContext::filter. + and − are verbs (command/), so
// both undo; a refused delete says why on the status line.
#include "views/ViewContext.hpp"
#include "widgets/Widgets.hpp"

#include <map>
#include <string>

namespace granjon::views {

class Navigator : public widgets::Box {
public:
    explicit Navigator(ViewContext& ctx);

    void refresh();                 // rebuild from the model (after writes)

private:
    void sync_selection();          // the row for ctx.selected, without emitting
    void on_row_selected(Gtk::ListBoxRow* row);
    void on_add();
    void on_remove();

    ViewContext&         m_ctx;
    widgets::ScrolledWindow m_scroll;
    widgets::ListBox     m_list;
    widgets::Button      m_add;
    widgets::Button      m_remove;
    widgets::Label       m_count;
    bool                 m_syncing = false;
    // Row name -> (section, count): the header func draws a header over the
    // first row of each section.
    std::map<std::string, std::pair<std::string, std::size_t>> m_section;
};

}  // namespace granjon::views
