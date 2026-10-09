#pragma once
// views::ViewContext -- what every view shares: the open document, the ONE
// selection, and the window's voice (the status line) and view switch.
//
// The selection is a glyph name, or "" for the font itself. It is the same in
// every view: a glyph picked in the navigator is the one the inspector shows,
// the overview rings, and (M4) the editor opens. Undo hands a selection back
// through select(), so a step lands where it was made.
#include "command/Document.hpp"

#include <sigc++/signal.h>

#include <functional>
#include <optional>
#include <set>
#include <string>

namespace granjon::views {

struct ViewContext {
    command::Document& doc;
    std::string        selected;                         // glyph name; "" = the font
    sigc::signal<void()> selection_changed;              // emitted by select() only

    // The header search narrows the navigator and the overview to its hits
    // while a query is live. nullopt = no filter (show everything).
    std::optional<std::set<std::string>> filter;
    sigc::signal<void()> filter_changed;
    void set_filter(std::optional<std::set<std::string>> f) {
        if (f == filter) return;
        filter = std::move(f);
        filter_changed.emit();
    }
    bool shows(const std::string& glyph) const { return !filter || filter->count(glyph) > 0; }

    std::function<void(const std::string&, bool warn)> say;     // the status line
    std::function<void(const std::string&)>            show_view;   // "overview", "editor", ...

    void select(const std::string& glyph) {
        const std::string g = (glyph.empty() || glyph[0] == '@' || !doc.font.glyph(glyph)) ? std::string{} : glyph;
        if (g == selected) return;
        selected = g;
        selection_changed.emit();
    }
};

}  // namespace granjon::views
