#include "Shell.hpp"

#include "model/Find.hpp"
#include "unicode/Blocks.hpp"
#include "unicode/UcdTable.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "Log.hpp"
#include "Registry.hpp"
#include "core/Recents.hpp"

#include <gtkmm/filedialog.h>
#include <giomm/asyncresult.h>
#include <giomm/file.h>

#include <filesystem>

// Shell_handlers.cpp -- HANDLERS. Slot bodies: what a user action actually does.
// Each is small and single-concern; orchestration it shares lives in helpers.

namespace granjon {

// ─────────────────────────────────────────────────────────────────────────────
// The file. A .granjon (or .ufo) font is a FOLDER, so Open chooses a folder.
// Anything that would drop unsaved changes asks first (confirm_discard).
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_new() {  // handler: Ctrl+N
    confirm_discard([this] {
        m_doc.new_font();
        after_document_swap();
        say("New font.", false);
    });
}

void Shell::on_open() {  // handler: Ctrl+O
    confirm_discard([this] {
        auto dialog = Gtk::FileDialog::create();
        dialog->set_title("Open a font  (a .granjon or .ufo folder)");
        dialog->select_folder(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto dir = dialog->select_folder_finish(result);
                if (dir) open_path(dir->get_path());
            } catch (const Glib::Error&) {
                // dismissed: nothing to do
            }
        });
    });
}

void Shell::on_save() {  // handler: Ctrl+S
    if (m_doc.path.empty()) { save_as({}); return; }
    std::string err;
    if (m_doc.save({}, &err)) {
        note_recent(m_doc.path);
        update_title();
        say("Saved " + std::filesystem::path(m_doc.path).filename().string() + ".", false);
    } else {
        say("Could not save: " + err, true);
    }
}

void Shell::on_save_as() { save_as({}); }  // handler: Ctrl+Shift+S

// The window's X. Unsaved changes: ask; the dialog's answer closes it.
bool Shell::on_close_request() {  // handler
    if (m_closing || !m_doc.modified()) return false;   // let it close
    confirm_discard([this] { m_closing = true; close(); });
    return true;   // held until the answer
}

void Shell::on_recent_open(const std::string& path) {  // handler: open a recent font
    if (path.empty()) return;
    if (!std::filesystem::exists(path)) {
        core::recents_remove(m_recents, path);
        core::save_recents(m_recents_file, m_recents);
        rebuild_recents_menu();
        say(path + " is gone -- removed from the recent list.", true);
        return;
    }
    confirm_discard([this, path] { open_path(path); });
}

void Shell::on_recent_clear() {  // handler: empty the recents list
    m_recents.clear();
    core::save_recents(m_recents_file, m_recents);
    rebuild_recents_menu();
}

void Shell::on_dump_registry() {  // handler: print the live widget tree
    // The debuggability payoff, on demand. Same call works from a gdb prompt.
    registry::dump();
}

void Shell::on_shortcuts() {  // handler: open the keyboard reference
    // Same lifetime stone as About: build once, re-present. The window's CONTENT
    // is not written here -- it renders by walking core::shortcut_registry(),
    // the same list bind_accelerators() wires from, so the two can't drift.
    if (!m_shortcuts) m_shortcuts = std::make_unique<ShortcutsDialog>();
    m_shortcuts->show(*this);
}

void Shell::on_about() {  // handler: open the About window (dialog-lifetime exemplar)
    // Lazily build once, then re-present. Hide-on-close singleton (CANON:
    // "Lifetime shape is design") -- the X hides it, so one instance lives for
    // the session and its named children never re-register.
    if (!m_about) m_about = std::make_unique<AboutWindow>(*this);
    m_about->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// The verb handlers. Each is ONE call into command:: (which goes through the
// journal). None writes m_doc.font directly: the selftest's gate would not see
// a handler that did, so the rule is kept here by hand -- handlers call verbs.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_undo() {  // handler: take the last step back
    const auto r = m_doc.journal.undo();
    if (!r.done) return;
    m_ctx.select(r.selection.empty() ? std::string{} : r.selection.front());
    say("Undid: " + r.label, false);
    if (auto lg = log::get(log::Area::Shell)) lg->info("undo: {}", r.label);
}

void Shell::on_redo() {  // handler: put the undone step back
    const auto r = m_doc.journal.redo();
    if (!r.done) return;
    m_ctx.select(r.selection.empty() ? std::string{} : r.selection.front());
    say("Redid: " + r.label, false);
    if (auto lg = log::get(log::Area::Shell)) lg->info("redo: {}", r.label);
}

void Shell::on_other_case() {  // handler: case navigation, A <-> a
    if (m_ctx.selected.empty()) { say("Select a glyph to go to its other case.", false); return; }
    char32_t other = 0;
    const std::string to = model::other_case_glyph(m_doc.font, m_ctx.selected, &other);
    if (!to.empty())  m_ctx.select(to);
    else if (other)   say("No glyph for " + unicode::utf8(other) + " (" + unicode::format_cp(other) +
                          ") yet -- type it in the search to create it.", true);
    else              say(m_ctx.selected + " has no other case.", false);
}

void Shell::on_view(const Glib::ustring& id) {  // handler: the view switcher
    if (!m_stack.get_child_by_name(id)) return;
    m_stack.set_visible_child(id);
    m_act_view->change_state(Glib::Variant<Glib::ustring>::create(id));
    if (id == "editor") m_editor.focus_canvas();   // arrows and Escape go to the canvas
}

void Shell::on_toggle_navigator() {  // handler: F9
    const bool show = !m_navigator.get_visible();
    m_navigator.set_visible(show);
    m_act_nav->change_state(Glib::Variant<bool>::create(show));
}

void Shell::on_toggle_inspector() {  // handler: Ctrl+I
    const bool show = !m_inspector.get_visible();
    m_inspector.set_visible(show);
    m_act_insp->change_state(Glib::Variant<bool>::create(show));
}

}  // namespace granjon
