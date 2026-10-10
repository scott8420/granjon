#pragma once
// views::ViewContext -- what every view shares: the open document, the ONE
// selection, and the window's voice (the status line) and view switch.
//
// The selection is a glyph name, or "" for the font itself. It is the same in
// every view: a glyph picked in the navigator is the one the inspector shows,
// the overview rings, and (M4) the editor opens. Undo hands a selection back
// through select(), so a step lands where it was made.
#include "command/Document.hpp"
#include "core/Settings.hpp"
#include "geom/Edit.hpp"

#include <sigc++/signal.h>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace granjon::views {

struct ViewContext {
    command::Document& doc;
    core::Settings&    settings;                         // the app's preferences (not undoable)
    // A view that changed a setting calls save_settings() and then emits
    // settings_changed, so the others (and the Preferences window) follow.
    // Listeners only re-apply what differs, so the round trip settles at once.
    std::function<void()> save_settings;
    sigc::signal<void()>  settings_changed;

    // The editor's pick inside the selected glyph (nodes, handles, anchors,
    // components). The canvas owns the gestures; the inspector shows and edits
    // a single picked point. Cleared when the selected glyph changes.
    geom::Points          pick;
    sigc::signal<void()>  pick_changed;
    // A picked guide (M5c), instead of points: owner "" = font-wide.
    struct GuideRef { std::string owner; int index = -1; };
    std::optional<GuideRef> guide_pick;   // emits pick_changed too
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

    // The navigator's multi-selection, as the set of glyphs the OVERVIEW shows
    // (Scott: "if i select 1-n in various sections then those selected should
    // show in the overview"). nullopt = no scope: the overview shows the font.
    // `selected` above stays the ONE glyph the inspector and editor follow.
    std::optional<std::set<std::string>> scope;
    sigc::signal<void()> scope_changed;
    void set_scope(std::optional<std::set<std::string>> s) {
        if (s == scope) return;
        scope = std::move(s);
        scope_changed.emit();
    }
    bool in_scope(const std::string& glyph) const { return !scope || scope->count(glyph) > 0; }
    // Open these glyphs as editor tabs, `active` in front (the navigator's
    // double-click). Wired by the shell to the editor.
    std::function<void(const std::vector<std::string>&, const std::string& active)> open_tabs;

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
