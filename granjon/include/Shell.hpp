#pragma once
#include "widgets/Widgets.hpp"
#include "command/Document.hpp"   // the open font + its undo journal
#include "views/GlyphSearch.hpp"
#include "views/Inspector.hpp"
#include "views/Navigator.hpp"
#include "views/GlyphCanvas.hpp"
#include "views/OverviewGrid.hpp"
#include "views/SpacingStrip.hpp"
#include "views/ReportList.hpp"
#include "views/PreviewPane.hpp"
#include "views/ViewContext.hpp"

#include <gtkmm/applicationwindow.h>
#include <giomm/menu.h>
#include <giomm/simpleaction.h>

#include <array>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Shell -- the app window, AND the split-class exemplar.
//
// This class is split across multiple TUs by CATEGORY OF CODE, not by feature
// (CANON: "vocabulary first, file layout second" + "the header is the index").
// The split costs ~nothing while the parts are small; retrofitting it after a
// class grows to thousands of lines is a multi-session migration, so the
// structure is installed on day one. The header below IS the index -- every
// method carries a trailing `// category: ...` tag naming the file its body
// lives in. Read the tag, open that file.
//
// Vocabulary
// ----------
//   Zones    -- construction of named regions: the window frame, header,
//               sidebar+stack, the view pages, the menu model.
//               File: src/Shell_zones.cpp
//   Bindings -- what's wired to what: Gio actions, the recents action group,
//               accelerators.  File: src/Shell_bindings.cpp
//   Handlers -- slot bodies (on_*): what a user action actually does.
//               File: src/Shell_handlers.cpp
//   Helpers  -- reusable sync/orchestration called from zones/bindings/handlers.
//               File: src/Shell_helpers.cpp
//   Glue     -- what stays in Shell.cpp: the ctor + build_ui() (the ordered
//               construct -> name/register -> bind pass).
//
// Finding things
// --------------
//   "Where is the window built?"        -> Shell_zones.cpp    (build_header / build_body)
//   "Where do the panels live?"         -> src/views/ (Navigator, Inspector, OverviewGrid)
//   "Where are actions registered?"     -> Shell_bindings.cpp (bind_actions)
//   "What does 'Dump registry' do?"     -> Shell_handlers.cpp (on_dump_registry)
//   "Where does the recents list sync?" -> Shell_helpers.cpp  (note_recent / rebuild_*)
//   "Where do the views repaint?"       -> Shell_helpers.cpp  (repaint)
//   "Where does Ctrl+Z go?"             -> Shell_bindings.cpp (the window's undo keys)
//                                          + Shell_handlers.cpp (on_undo / on_redo)
// ─────────────────────────────────────────────────────────────────────────────
namespace granjon {

class AboutWindow;
class ShortcutsDialog;
class PreferencesWindow;

class Shell : public Gtk::ApplicationWindow {
public:
    Shell();
    ~Shell() override;

    void build_ui();                             // category: glue

private:
    // ── zones ──────────────────────────────────────────────────────────────
    void build_header();                         // category: zone: logo, panel toggles, undo/redo, view switcher, menu
    void build_body();                           // category: zone: navigator | views | inspector, status strip
    void build_views();                          // category: zone: the view stack (overview + stubs)
    void add_stub_view(const char* id, const char* title, const char* what);   // category: zone
    Glib::RefPtr<Gio::Menu> build_menu();        // category: zone: hamburger model

    // ── bindings ───────────────────────────────────────────────────────────
    void bind_actions();                         // category: bindings: actions, recents group, undo keys, motif

    // ── handlers ───────────────────────────────────────────────────────────
    void on_new();                               // category: handler: a fresh untitled font (Ctrl+N)
    void on_open();                              // category: handler: choose a .granjon / .ufo folder (Ctrl+O)
    void on_save();                              // category: handler: save, or Save As when untitled (Ctrl+S)
    void on_save_as();                           // category: handler: choose where (Ctrl+Shift+S)
    void on_export_otf();                        // category: handler: compile + write an .otf (Ctrl+E, M9)
    bool on_close_request();                     // category: handler: the window's X with unsaved changes
    void on_recent_open(const std::string& p);   // category: handler: open a recent font (parameterised)
    void on_recent_clear();                      // category: handler: empty the recents list
    void on_dump_registry();                     // category: handler: print the live widget tree
    void on_about();                             // category: handler: open the About window (dialog-lifetime exemplar)
    void on_font_metrics_keys();                 // category: handler: apply metrics keys font-wide (one step, M6)
    void on_font_directions();                   // category: handler: correct directions font-wide (one step)
    void on_font_anchors();                      // category: handler: default anchors on the whole font (one step)
    void on_preferences();                       // category: handler: open the Preferences window (Ctrl+,)
    void on_shortcuts();   // handler: open the keyboard reference (renders from core::shortcut_registry)
    void on_other_case();                        // category: handler: A <-> a (Alt+C)
    void on_undo();                              // category: handler: Ctrl+Z / menu / button
    void on_redo();                              // category: handler: Ctrl+Shift+Z, Ctrl+Y / menu / button
    void on_view(const Glib::ustring& id);       // category: handler: the view switcher (Ctrl+1..5)
    void on_toggle_navigator();                  // category: handler: show / hide the left panel (F9)
    void on_toggle_inspector();                  // category: handler: show / hide the right panel (Ctrl+I)

    // ── helpers ────────────────────────────────────────────────────────────
    void note_recent(const std::string& path);   // category: helper: push onto the list + persist
    void confirm_discard(std::function<void()> then);   // category: helper: unsaved? ask Save / Discard / Cancel, then go on
    void save_as(std::function<void()> then);     // category: helper: the save dialog; `then` after a good save
    void open_path(const std::string& path);      // category: helper: load (already confirmed), reset the views
    void after_document_swap();                   // category: helper: new / open: selection, filter, views, title
    void update_title();                          // category: helper: name, file, the unsaved dot
    void rebuild_recents_menu();                  // category: helper: repaint the submenu
    std::string recents_file() const;             // category: helper: the XDG path (UI-side resolution)
    static std::string settings_file();           // category: helper: ~/.config/granjon/settings.json
    void save_settings();                         // category: helper: write the settings file (best effort)
    void queue_repaint();                         // category: helper: one repaint per idle, after the writes
    void repaint();                               // category: helper: every view from the model
    void repaint_changed();                       // category: helper: only what m_dirty names (the idle path)
    void update_undo_ui();                        // category: helper: Undo / Redo say what they would do, grey when nothing
    void say(const std::string& s, bool warn);    // category: helper: the status strip
    void apply_motif();                           // category: helper: granjon-light on the window when the desktop is light
    bool focus_is_text() const;                   // category: helper: the keyboard is in a text box (its own Ctrl+Z)

    // The open font and the shared selection. Declaration order matters: the
    // document before the context that refers to it, both before the views.
    // Settings load first: the views read them as they are built.
    std::string        m_settings_file = settings_file();
    core::Settings     m_settings      = core::load_settings(m_settings_file);
    command::Document  m_doc;
    views::ViewContext m_ctx{m_doc, m_settings};

    // Header: logo, undo/redo (start) | view switcher (title) |
    // search, panel toggles (one linked pair), menu (end).
    widgets::Button       m_logo_button;
    widgets::ToggleButton m_nav_toggle;
    widgets::Button       m_undo_button;
    widgets::Button       m_redo_button;
    widgets::Box          m_switcher;       // the segmented pill
    views::GlyphSearch    m_search{m_ctx};   // the one search: go to glyph (Ctrl+F)
    widgets::ToggleButton m_insp_toggle;
    widgets::Box          m_panels;         // linked pair: navigator | inspector toggles
    widgets::MenuButton   m_menu_button;

    // Body: navigator | (views | inspector), status strip below.
    widgets::Box          m_frame;          // vertical: panes + status
    widgets::Paned        m_outer;          // navigator | inner
    widgets::Paned        m_inner;          // stack | inspector
    widgets::Stack        m_stack;
    widgets::Label        m_status;
    views::Navigator      m_navigator{m_ctx};
    views::OverviewGrid   m_overview{m_ctx};
    views::GlyphCanvas    m_editor{m_ctx};
    views::SpacingStrip   m_spacing{m_ctx};
    views::ReportList     m_report{m_ctx};
    views::PreviewPane    m_preview{m_ctx};
    views::Inspector      m_inspector{m_ctx};

    // Recents state (core data; UI holds it + resolves the path -- the seam).
    std::vector<std::string>        m_recents;
    std::string                     m_recents_file;
    Glib::RefPtr<Gio::Menu>         m_recents_menu;   // live submenu, rebuilt in place
    Glib::RefPtr<Gio::SimpleAction> m_recents_clear;  // held to toggle enabled state

    bool                            m_repaint_queued = false;
    std::set<std::string>           m_dirty;          // records written since the last repaint
    bool                            m_closing = false;   // confirmed: let the next close through
    Glib::RefPtr<Gio::Menu>         m_edit_menu;      // Undo / Redo items, re-labelled live
    Glib::RefPtr<Gio::SimpleAction> m_act_undo, m_act_redo, m_act_view;
    Glib::RefPtr<Gio::SimpleAction> m_act_nav, m_act_insp;

    // The About window: a hide-on-close singleton, built lazily on first open.
    std::unique_ptr<AboutWindow>    m_about;
    std::unique_ptr<ShortcutsDialog> m_shortcuts;
    std::unique_ptr<PreferencesWindow> m_prefs;      // same hide-on-close singleton shape   // shortcut registry's GTK consumer (same lifetime stone)
};

}  // namespace granjon
