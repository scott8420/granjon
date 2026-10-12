#include "Shell.hpp"
#include "core/Recents.hpp"
#include "Log.hpp"

#include <chrono>
#include <gtkmm/alertdialog.h>
#include <gtkmm/filedialog.h>
#include <giomm/file.h>
#include <gtkmm/label.h>
#include <gtkmm/settings.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/editable.h>
#include <gtkmm/textview.h>
#include <glibmm/main.h>
#include <giomm/menuitem.h>
#include <glibmm/miscutils.h>
#include <glibmm/variant.h>

#include <cstdio>
#include <filesystem>

// Shell_helpers.cpp -- HELPERS. Reusable orchestration called from zones,
// bindings, and handlers: the recents seam, the view repaint, the undo labels.

namespace granjon {

std::string Shell::recents_file() const {  // helper: XDG path (UI-side resolution)
    // The UI resolves the XDG dir (Glib here) and hands the core a plain path --
    // the core stays GTK-free (docs/two-layer-seam.md).
    return (std::filesystem::path(Glib::get_user_data_dir()) / "granjon" / "recent.json")
        .string();
}

std::string Shell::settings_file() {  // helper
    return (std::filesystem::path(Glib::get_user_config_dir()) / "granjon" / "settings.json").string();
}

void Shell::save_settings() {  // helper: best effort -- a read-only config dir must not stop the work
    if (!core::save_settings(m_settings_file, m_settings))
        if (auto lg = log::get(log::Area::Shell)) lg->warn("could not write {}", m_settings_file);
}

void Shell::note_recent(const std::string& path) {  // helper: push + persist + repaint
    if (path.empty()) return;
    // The font in hand is the one to reopen; its folder is where Open starts.
    m_settings.last_font = path;
    m_settings.open_folder = std::filesystem::path(path).parent_path().string();
    save_settings();
    core::recents_add(m_recents, path);
    core::save_recents(m_recents_file, m_recents);
    rebuild_recents_menu();
}

void Shell::rebuild_recents_menu() {  // helper: repaint the submenu
    if (!m_recents_menu) return;
    m_recents_menu->remove_all();

    // Display name = folder basename; identical basenames get their parent dir
    // appended so they're distinguishable (Curvz's disambiguation).
    std::vector<std::string> names(m_recents.size());
    for (std::size_t i = 0; i < m_recents.size(); ++i)
        names[i] = std::filesystem::path(m_recents[i]).filename().string();
    for (std::size_t i = 0; i < names.size(); ++i)
        for (std::size_t j = i + 1; j < names.size(); ++j)
            if (names[i] == names[j]) {
                const std::string pi =
                    std::filesystem::path(m_recents[i]).parent_path().filename().string();
                const std::string pj =
                    std::filesystem::path(m_recents[j]).parent_path().filename().string();
                if (!pi.empty()) names[i] += "  (" + pi + ")";
                if (!pj.empty()) names[j] += "  (" + pj + ")";
            }

    for (std::size_t i = 0; i < m_recents.size(); ++i) {
        auto item = Gio::MenuItem::create(names[i], "");
        item->set_action_and_target(
            "recents.open", Glib::Variant<Glib::ustring>::create(m_recents[i]));
        m_recents_menu->append_item(item);
    }

    auto clear_section = Gio::Menu::create();
    clear_section->append("Clear recent", "recents.clear");
    m_recents_menu->append_section("", clear_section);
    if (m_recents_clear) m_recents_clear->set_enabled(!m_recents.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Repaint -- every view from the model.
// ─────────────────────────────────────────────────────────────────────────────

// One repaint per idle, AFTER the writes that caused it. A verb can write
// several records (a rename writes the glyph, its users, the kerning);
// repainting per write would rebuild views mid-verb (jot s045: "open UI after
// the rebuild you caused").
void Shell::queue_repaint() {  // helper
    if (m_repaint_queued) return;
    m_repaint_queued = true;
    Glib::signal_idle().connect_once([this]() {
        m_repaint_queued = false;
        repaint_changed();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// The file -- helpers behind New / Open / Save / Save As / close.
// ─────────────────────────────────────────────────────────────────────────────

// Unsaved changes? Ask before they are dropped: Save (then go on), Discard
// (go on), Cancel (stay). No changes: just go on.
void Shell::confirm_discard(std::function<void()> then) {  // helper
    if (!m_doc.modified()) { then(); return; }
    auto dlg = Gtk::AlertDialog::create("Save changes to \u201C" + m_doc.title() + "\u201D?");
    dlg->set_detail("Your changes since the last save will be lost if you don't save them.");
    dlg->set_buttons({"Cancel", "Discard", "Save"});
    dlg->set_cancel_button(0);
    dlg->set_default_button(2);
    dlg->set_modal(true);
    dlg->choose(*this, [this, dlg, then](const Glib::RefPtr<Gio::AsyncResult>& result) {
        int answer = 0;
        try { answer = dlg->choose_finish(result); } catch (const Glib::Error&) { answer = 0; }
        if (answer == 1) { then(); return; }
        if (answer != 2) return;
        if (m_doc.path.empty()) { save_as(then); return; }
        std::string err;
        if (m_doc.save({}, &err)) { note_recent(m_doc.path); update_title(); then(); }
        else say("Could not save: " + err, true);
    });
}

// The save dialog. A font is a folder; a name without .granjon / .ufo gets
// .granjon. `then` runs after a good save (close / new / open after "Save").
void Shell::save_as(std::function<void()> then) {  // helper
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Save font as");
    std::string base = m_doc.path.empty() ? m_doc.title() : std::filesystem::path(m_doc.path).stem().string();
    for (auto& c : base) if (c == ' ' || c == '/') c = '-';
    dialog->set_initial_name(base + ".granjon");
    std::error_code ec;
    if (!m_doc.path.empty())
        dialog->set_initial_folder(Gio::File::create_for_path(std::filesystem::path(m_doc.path).parent_path().string()));
    else if (!m_settings.open_folder.empty() && std::filesystem::is_directory(m_settings.open_folder, ec))
        dialog->set_initial_folder(Gio::File::create_for_path(m_settings.open_folder));
    dialog->save(*this, [this, dialog, then](const Glib::RefPtr<Gio::AsyncResult>& result) {
        Glib::RefPtr<Gio::File> file;
        try { file = dialog->save_finish(result); } catch (const Glib::Error&) { return; }
        if (!file) return;
        std::filesystem::path p = file->get_path();
        const std::string ext = p.extension().string();
        if (ext != ".granjon" && ext != ".ufo") p += ".granjon";
        if (std::filesystem::exists(p) && !std::filesystem::is_directory(p)) {
            say(p.filename().string() + " is a file, not a font folder -- choose another name.", true);
            return;
        }
        std::string err;
        if (!m_doc.save(p.string(), &err)) { say("Could not save: " + err, true); return; }
        note_recent(m_doc.path);
        update_title();
        say("Saved " + p.filename().string() + ".", false);
        if (then) then();
    });
}

void Shell::open_path(const std::string& path) {  // helper (already confirmed)
    std::string err;
    if (!m_doc.open(path, &err)) {
        say("Could not open: " + err, true);
        if (auto lg = log::get(log::Area::Io)) lg->warn("open '{}': {}", path, err);
        return;
    }
    note_recent(path);
    after_document_swap();
    say("Opened " + std::filesystem::path(path).filename().string() + " \u2014 " +
        std::to_string(m_doc.font.glyph_count()) + " glyphs.", false);
    if (auto lg = log::get(log::Area::Io)) lg->info("opened '{}'", path);
}

void Shell::after_document_swap() {  // helper: new / open
    m_ctx.set_filter(std::nullopt);
    m_ctx.set_scope(std::nullopt);
    m_editor.clear_tabs();
    m_ctx.select({});
    repaint();
    update_undo_ui();
}

// "Untitled Regular" / "Garamond Italic — Garamond-Italic.granjon", with a
// dot while there are unsaved changes.
void Shell::update_title() {  // helper
    std::string t = m_doc.title();
    if (!m_doc.path.empty()) t += " \u2014 " + std::filesystem::path(m_doc.path).filename().string();
    if (m_doc.modified()) t = "\u2022 " + t;
    set_title(t);
}

// The idle path: the editor and inspector repaint (cheap); the navigator and
// overview update only the rows / cells the step touched, and rebuild only when
// the glyph SET changed (added, removed, renamed, re-encoded).
void Shell::repaint_changed() {  // helper
    const auto t0 = std::chrono::steady_clock::now();
    if (!m_ctx.selected.empty() && !m_doc.font.glyph(m_ctx.selected)) m_ctx.select({});
    update_title();
    m_editor.refresh();
    m_spacing.refresh();   // live through a kern / sidebearing drag
    if (m_doc.journal.in_gesture()) return;   // m_dirty keeps collecting until the release
    const auto dirty = std::move(m_dirty);
    m_dirty.clear();
    m_navigator.update(dirty);
    m_overview.update(dirty);
    m_report.update(dirty);
    m_features.update(dirty);
    m_preview.refresh();   // compiles only when shown, after a pause
    m_inspector.refresh();
    // Left in on purpose: a step that takes long enough to feel is worth a line.
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ms > 50)
        if (auto lg = log::get(log::Area::Shell)) lg->info("slow repaint: {} ms ({} records)", static_cast<long>(ms), dirty.size());
}

void Shell::repaint() {  // helper
    // A step may have removed the selected glyph (undo of an add): fall back
    // to the font before any view reads the selection.
    if (!m_ctx.selected.empty() && !m_doc.font.glyph(m_ctx.selected)) m_ctx.select({});
    update_title();
    m_editor.refresh();
    m_spacing.refresh();
    m_dirty.clear();   // a full repaint covers everything
    m_navigator.refresh();
    m_overview.refresh();
    m_report.refresh();
    m_features.refresh();
    m_preview.refresh();
    m_inspector.refresh();
}

void Shell::say(const std::string& s, bool warn) {  // helper: the status strip
    m_status.set_text(s);
    if (warn) m_status.add_css_class("warn");
    else      m_status.remove_css_class("warn");
}

// The house motif follows the desktop: GTK's prefer-dark (set by Appearance
// from the portal) -> dark tokens; otherwise the light motif's tokens.
void Shell::apply_motif() {  // helper
    bool dark = false;
    if (auto settings = Gtk::Settings::get_default())
        dark = settings->property_gtk_application_prefer_dark_theme().get_value();
    if (dark) remove_css_class("granjon-light");
    else      add_css_class("granjon-light");
}

// Undo / Redo say what they would do, and grey when there is nothing: the
// actions (so buttons and menu items follow), the button tooltips, and the
// menu's labels -- "Undo Delete “Jade”" names the step before you take it.
void Shell::update_undo_ui() {  // helper
    update_title();   // every journal change may flip the unsaved dot
    if (m_act_undo) m_act_undo->set_enabled(m_doc.journal.can_undo());
    if (m_act_redo) m_act_redo->set_enabled(m_doc.journal.can_redo());
    const std::string u = m_doc.journal.undo_label(), r = m_doc.journal.redo_label();
    m_undo_button.set_tooltip_text(u.empty() ? "Nothing to undo" : "Undo " + u + "  (Ctrl+Z)");
    m_redo_button.set_tooltip_text(r.empty() ? "Nothing to redo" : "Redo " + r + "  (Ctrl+Shift+Z)");
    if (m_edit_menu) {
        m_edit_menu->remove_all();
        m_edit_menu->append(u.empty() ? "Undo" : "Undo " + u, "win.undo");
        m_edit_menu->append(r.empty() ? "Redo" : "Redo " + r, "win.redo");
    }
}

// Is the keyboard in something that types? Then Ctrl+Z is that box's own,
// whether or not it had anything to undo.
bool Shell::focus_is_text() const {  // helper
    // A spin button is a number, not a text box: its Ctrl+Z is the document's
    // (its own text undo is off -- views/Inspector.cpp). The focus sits on the
    // spin's inner text, so look up the whole chain for a spin first.
    for (const Gtk::Widget* w = get_focus(); w; w = w->get_parent())
        if (dynamic_cast<const Gtk::SpinButton*>(w)) return false;
    for (const Gtk::Widget* w = get_focus(); w; w = w->get_parent())
        if (dynamic_cast<const Gtk::TextView*>(w) || dynamic_cast<const Gtk::Editable*>(w)) return true;
    return false;
}

}  // namespace granjon
