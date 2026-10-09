#pragma once
// views::GlyphSearch -- the header's one search: "go to glyph" by name,
// character, codepoint or /escape (model::find_glyphs).
//
//   typing    the navigator and overview narrow to the hits (ViewContext::
//             filter); a dropdown lists the best eight with a drawing each
//   ↑ ↓       move in the dropdown
//   Enter     select the highlighted hit in every view, clear the search
//   Escape    clear the search, nothing selected changes
//   no glyph  for a character / codepoint / AGL name / Unicode name: up to
//             four "Create é as eacute" rows -- the add verb, so it undoes
// Ctrl+F (win.find) puts the keyboard here from anywhere.
#include "model/Find.hpp"
#include "views/ViewContext.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/popover.h>

#include <optional>

namespace granjon::views {

class GlyphSearch : public widgets::Box {
public:
    explicit GlyphSearch(ViewContext& ctx);
    ~GlyphSearch() override;

    void focus();

private:
    void on_changed();
    void on_activate_row(Gtk::ListBoxRow* row);
    void move(int delta);
    void clear();

    ViewContext&          m_ctx;
    widgets::SearchEntry  m_entry;
    Gtk::Popover          m_pop;
    widgets::ListBox      m_list;
    std::vector<model::FindCreate> m_create;
    bool                  m_clearing = false;
};

}  // namespace granjon::views
