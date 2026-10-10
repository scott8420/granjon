#include "Shell.hpp"
#include "model/Spacing.hpp"

#include "model/Find.hpp"
#include "unicode/Blocks.hpp"
#include "unicode/UcdTable.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "PreferencesWindow.hpp"
#include "Log.hpp"
#include "Registry.hpp"
#include "core/Recents.hpp"
#include "export/Otf.hpp"
#include "export/Codes.hpp"
#include "export/Woff2.hpp"
#include "model/Spacing.hpp"

#include <gtkmm/filedialog.h>
#include <giomm/asyncresult.h>
#include <giomm/file.h>
#include <giomm/liststore.h>
#include <gtkmm/filefilter.h>

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
        m_settings.last_font.clear();   // untitled: nothing to reopen
        save_settings();
        after_document_swap();
        say("New font.", false);
    });
}

void Shell::on_open() {  // handler: Ctrl+O
    confirm_discard([this] {
        auto dialog = Gtk::FileDialog::create();
        dialog->set_title("Open a font  (a .granjon or .ufo folder)");
        std::error_code ec;
        if (!m_settings.open_folder.empty() && std::filesystem::is_directory(m_settings.open_folder, ec))
            dialog->set_initial_folder(Gio::File::create_for_path(m_settings.open_folder));
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

// Import (M11): an OTF / TTF read into a new, unsaved document -- the file on
// disk is never touched; Save asks where to keep the .granjon.
void Shell::on_import() {  // handler: Ctrl+Shift+O
    confirm_discard([this] {
        auto dialog = Gtk::FileDialog::create();
        dialog->set_title("Import a font  (OTF / TTF)");
        auto filters = Gio::ListStore<Gtk::FileFilter>::create();
        auto fonts = Gtk::FileFilter::create();
        fonts->set_name("OpenType / TrueType fonts");
        for (const char* p : {"*.otf", "*.ttf", "*.OTF", "*.TTF"}) fonts->add_pattern(p);
        filters->append(fonts);
        auto all = Gtk::FileFilter::create();
        all->set_name("All files");
        all->add_pattern("*");
        filters->append(all);
        dialog->set_filters(filters);
        std::error_code ec;
        if (!m_settings.open_folder.empty() && std::filesystem::is_directory(m_settings.open_folder, ec))
            dialog->set_initial_folder(Gio::File::create_for_path(m_settings.open_folder));
        dialog->open(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
            Glib::RefPtr<Gio::File> file;
            try { file = dialog->open_finish(result); } catch (const Glib::Error&) { return; }
            if (!file) return;
            const std::string p = file->get_path();
            reader::ImportReport rep;
            std::string err;
            if (!m_doc.import(p, &rep, &err)) {
                say(err, true);
                if (auto lg = log::get(log::Area::Io)) lg->warn("import '{}': {}", p, err);
                return;
            }
            after_document_swap();
            char buf[200];
            std::snprintf(buf, sizeof buf, " (%s) \u2014 %d glyphs, %d kern entries, %.0f ms. Save as\u2026 keeps it.",
                          rep.format.c_str(), rep.glyphs, rep.kern_pairs, rep.ms);
            std::string line = "Imported " + std::filesystem::path(p).filename().string() + buf;
            if (!rep.notes.empty()) line += " " + rep.notes.front() + (rep.notes.size() > 1 ? " (more in the log)" : "");
            say(line, false);
            if (auto lg = log::get(log::Area::Io)) {
                lg->info("import '{}': {} {} glyphs, {} contours, {} components, {} code points, kern {} entries / {} groups, {:.1f} ms",
                         p, rep.format, rep.glyphs, rep.contours, rep.components, rep.encoded, rep.kern_pairs, rep.kern_groups, rep.ms);
                for (const auto& n : rep.notes) lg->info("import: {}", n);
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

// Export OTF (M9) / TTF (M10a): the font compiled -- the OTF is the bytes the
// Preview sets -- written beside the font by default ("Family-Style.otf").
// Read-only on the font: no undo step. The dialog starts where the last export
// went (either format).
void Shell::on_export_otf() { export_font(".otf"); }  // handler: Ctrl+E
void Shell::on_export_ttf() { export_font(".ttf"); }  // handler: Ctrl+Shift+E

void Shell::export_font(const std::string& ext) {  // helper
    const bool ttf = ext == ".ttf";
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title(ext == ".woff2" ? "Export WOFF2 (web font)" : ttf ? "Export TTF" : "Export OTF");
    dialog->set_initial_name(otf::default_file_name(m_doc.font.info(), ext));
    std::error_code ec;
    if (!m_settings.export_folder.empty() && std::filesystem::is_directory(m_settings.export_folder, ec))
        dialog->set_initial_folder(Gio::File::create_for_path(m_settings.export_folder));
    else if (!m_doc.path.empty())
        dialog->set_initial_folder(Gio::File::create_for_path(std::filesystem::path(m_doc.path).parent_path().string()));
    dialog->save(*this, [this, dialog, ttf, ext](const Glib::RefPtr<Gio::AsyncResult>& result) {
        Glib::RefPtr<Gio::File> file;
        try { file = dialog->save_finish(result); } catch (const Glib::Error&) { return; }
        if (!file) return;
        std::filesystem::path p = file->get_path();
        if (p.extension() != ext) p += ext;
        otf::OtfReport rep;
        std::string err;
        const bool ok = ext == ".woff2" ? otf::export_woff2(m_doc.font, p, {}, &rep, &err)
                      : ttf ? otf::export_ttf(m_doc.font, p, {}, &rep, &err) : otf::export_otf(m_doc.font, p, {}, &rep, &err);
        if (!ok) {
            say("Could not export: " + err, true);
            if (auto lg = log::get(log::Area::Io)) lg->warn("export '{}': {}", p.string(), err);
            return;
        }
        m_settings.export_folder = p.parent_path().string();
        save_settings();
        char buf[200];
        std::snprintf(buf, sizeof buf, " \u2014 %d glyphs, %.0f KB, %.0f ms", rep.glyphs, rep.bytes / 1024.0, rep.ms);
        std::string line = "Exported " + p.filename().string() + buf;
        if (!rep.notes.empty()) line += ". " + rep.notes.front() + (rep.notes.size() > 1 ? " (more in the log)" : "");
        say(line, false);
        if (auto lg = log::get(log::Area::Io)) {
            lg->info("export '{}': {} glyphs, {} code points, {} bytes, {:.1f} ms; kern {} glyph pairs, {} class cells, {} subtables{}",
                     p.string(), rep.glyphs, rep.encoded, rep.bytes, rep.ms, rep.kern_glyph_pairs, rep.kern_class_pairs,
                     rep.gpos_subtables, ttf ? "; " + std::to_string(rep.tt_points) + " glyf points" : std::string());
            for (const auto& n : rep.notes) lg->info("export: {}", n);
        }
    });
}

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

void Shell::on_font_directions() {  // handler: the menu's "Correct contour directions in every glyph"
    std::size_t n = 0;
    if (command::correct_directions(m_doc.journal, m_doc.font, m_doc.font.glyph_order(), &n))
        say(m_doc.journal.undo_label() + ". Ctrl+Z takes it back.", false);
    else
        say("Every contour already runs the right way.", false);
}

void Shell::on_font_metrics_keys() {  // handler: the menu's "Apply metrics keys in every glyph" (M6)
    std::size_t n = 0;
    std::vector<std::string> cyclic;
    model::key_order(m_doc.font, &cyclic);
    std::string loop;
    for (const auto& c : cyclic) loop += (loop.empty() ? "" : ", ") + c;
    if (command::apply_metrics_keys(m_doc.journal, m_doc.font, {}, &n))
        say(m_doc.journal.undo_label() + ". Ctrl+Z takes it back." +
            (loop.empty() ? "" : "  Keys refer in a circle at: " + loop), !loop.empty());
    else if (m_doc.font.metrics_keys().keys.empty())
        say("No glyph has a metrics key yet -- type one in the inspector's METRICS card.", false);
    else
        say("Every metrics key already matches." + (loop.empty() ? "" : "  Keys refer in a circle at: " + loop),
            !loop.empty());
}

void Shell::on_font_anchors() {  // handler: the menu's "Add default anchors to every letter and mark"
    std::string why;
    if (command::add_default_anchors(m_doc.journal, m_doc.font, m_doc.font.glyph_order(), &why))
        say(m_doc.journal.undo_label() + ". Ctrl+Z takes them all back.", false);
    else
        say(why, false);
}

void Shell::on_preferences() {  // handler: Ctrl+,
    if (!m_prefs) m_prefs = std::make_unique<PreferencesWindow>(*this, m_ctx, m_settings_file);
    // Follow the window's motif (the colour tokens hang off window.granjon-light).
    if (has_css_class("granjon-light")) m_prefs->add_css_class("granjon-light");
    else                                m_prefs->remove_css_class("granjon-light");
    m_prefs->present();
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
    if (id == "spacing") m_spacing.focus_strip();
    else m_ctx.set_kern_pick(std::nullopt);   // the pair belongs to the strip
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

namespace granjon {

// Export SVG (M9c): outlines as plain artwork -- the selected glyph, or the
// Spacing strip's text set with its kerning, one <path> per glyph, y flipped.
// The "convert to outlines" hand-off; read-only on the font.
void Shell::export_svg(bool strip) {  // helper
    std::vector<std::string> names;
    std::string base;
    if (strip) {
        names = model::parse_strip(m_doc.font, m_settings.spacing_text);
        base = otf::postscript_name(m_doc.font.info().family, m_doc.font.info().style) + "-text";
        if (names.empty()) { say("The Spacing text sets no glyphs -- type some in the Spacing view first.", true); return; }
    } else {
        if (m_ctx.selected.empty() || !m_doc.font.glyph(m_ctx.selected)) { say("Select a glyph to export it as SVG.", true); return; }
        names = {m_ctx.selected};
        base = m_ctx.selected;
    }
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title(strip ? "Export Spacing text as SVG" : "Export glyph as SVG");
    dialog->set_initial_name(base + ".svg");
    std::error_code ec;
    if (!m_settings.export_folder.empty() && std::filesystem::is_directory(m_settings.export_folder, ec))
        dialog->set_initial_folder(Gio::File::create_for_path(m_settings.export_folder));
    else if (!m_doc.path.empty())
        dialog->set_initial_folder(Gio::File::create_for_path(std::filesystem::path(m_doc.path).parent_path().string()));
    dialog->save(*this, [this, dialog, names](const Glib::RefPtr<Gio::AsyncResult>& result) {
        Glib::RefPtr<Gio::File> file;
        try { file = dialog->save_finish(result); } catch (const Glib::Error&) { return; }
        if (!file) return;
        std::filesystem::path p = file->get_path();
        if (p.extension() != ".svg") p += ".svg";
        std::string err;
        if (!otf::export_svg(m_doc.font, names, p, {}, &err)) { say("Could not export: " + err, true); return; }
        m_settings.export_folder = p.parent_path().string();
        save_settings();
        say("Exported " + p.filename().string() + " \u2014 " + std::to_string(names.size()) + " glyph" +
            (names.size() == 1 ? "" : "s") + " as outlines.", false);
        if (auto lg = log::get(log::Area::Io)) lg->info("export svg '{}': {} glyphs", p.string(), names.size());
    });
}

}  // namespace granjon
