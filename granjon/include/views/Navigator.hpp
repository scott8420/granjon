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
#include <set>
#include <vector>
#include <string>

namespace granjon::views {

class Navigator : public widgets::Box {
public:
    explicit Navigator(ViewContext& ctx);

    void refresh();                 // rebuild from the model (after writes)

private:
    void sync_selection();          // the row for ctx.selected, without emitting
    void on_rows_changed();         // the multi-selection moved: scope + the focus glyph
    void on_row_activated(Gtk::ListBoxRow* row);   // double-click: open the pick in editor tabs
    void on_section_click(const std::string& section, int n_press, double x, bool additive);
    void update_scope();            // glyph rows + picked sections -> ViewContext::scope
    std::vector<std::string> picked_glyphs() const;   // in list order
    void on_add();
    void on_remove();
    // Sections fold (M5b+, Scott: "collapse the glyphs in the navigator by
    // sets"). A header row toggles its section; Ctrl/Alt-click folds or opens
    // them all. Open sections are remembered (core::Settings). While a search
    // narrows, every section shows open.
    bool is_open(const std::string& section) const;
    void set_open(const std::string& section, bool open, bool save = true);
    void toggle_all(bool open);
    void apply_folds();             // show / hide rows by their section's state

    ViewContext&         m_ctx;
    widgets::ScrolledWindow m_scroll;
    widgets::ListBox     m_list;
    widgets::Button      m_add;
    widgets::Button      m_remove;
    widgets::Label       m_count;
    bool                 m_syncing = false;
    struct Section {
        Gtk::ListBoxRow*              header = nullptr;
        Gtk::Label*                   chevron = nullptr;
        std::vector<Gtk::ListBoxRow*> rows;
    };
    std::map<std::string, Section> m_sections;          // by section name
    std::map<std::string, std::string> m_glyph_section; // glyph -> its section
    std::set<std::string>      m_picked_sections;       // section headers picked (whole sections)
    std::set<std::string>      m_multi;                 // picked glyph rows (kept across rebuilds)
    bool                       m_additive = false;      // the last press had Ctrl / Shift
    std::string                m_press_row;             // the row under the last press
};

}  // namespace granjon::views
