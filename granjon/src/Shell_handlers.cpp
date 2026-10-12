#include "Shell.hpp"
#include "views/PathOps.hpp"
#include <glibmm/spawn.h>
#include <glibmm/miscutils.h>
#include "views/ProofPrinter.hpp"
#include "model/Proof.hpp"
#include <algorithm>
#include <gtkmm/printoperation.h>
#include "model/Spacing.hpp"

#include "model/Find.hpp"
#include "unicode/Blocks.hpp"
#include "unicode/UcdTable.hpp"
#include "AboutWindow.hpp"
#include "NewFontDialog.hpp"
#include "PrimerWindow.hpp"
#include "core/Primer.hpp"
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
// M12: New asks which preset to start from (the chooser), then replaces the
// document. A preset font opens its six control glyphs in the editor, n in
// front, and points the Overview's coverage at the set.
void Shell::on_new() {  // handler: Ctrl+N
    confirm_discard([this] {
        if (!m_newfont) m_newfont = std::make_unique<NewFontDialog>();
        m_newfont->show(*this, m_settings.new_preset, m_settings.new_create_set,
                        [this](const model::NewFontOptions& o) {
            model::NewFontReport rep;
            m_doc.new_font(o, &rep);
            m_settings.last_font.clear();   // untitled: nothing to reopen
            m_settings.new_preset = o.preset;
            const model::Preset* p = model::preset(o.preset);
            if (p) {
                m_settings.new_create_set = o.create_set;
                m_settings.overview_charset = o.charset == "@none" ? std::string{} : o.charset;
            }
            save_settings();
            m_ctx.settings_changed.emit();
            after_document_swap();
            if (!p) { say("New font.", false); return; }
            if (auto lg = log::get(log::Area::Io)) for (const auto& n : rep.notes) lg->warn("new font: {}", n);
            m_ctx.open_tabs(model::control_glyphs(), "n");
            m_ctx.select("n");
            m_ctx.show_view("editor");
            std::string s = "New " + p->name + " font: n o H O p v drawn to start from";
            if (rep.added > 0)
                s += ", " + std::to_string(rep.added) + " more glyphs from the set (" +
                     std::to_string(rep.composites) + " built from parts)";
            s += ". Primer (F1) has the why.";
            say(s, !rep.notes.empty());
        });
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

// s10: ttfautohint on an exported TTF, in place. Returns a note for the
// status line ("" when it wasn't asked for). The unhinted file stays when
// ttfautohint is missing or fails.
std::string Shell::autohint(const std::filesystem::path& ttf) {  // helper
    const std::string exe = Glib::find_program_in_path("ttfautohint");
    if (exe.empty()) return " -- not hinted: ttfautohint is not installed";
    const auto tmp = ttf.string() + ".hinting";
    std::string out, err;
    int status = 0;
    try {
        Glib::spawn_sync("", std::vector<std::string>{exe, "--no-info", ttf.string(), tmp}, Glib::SpawnFlags::DEFAULT, {},
                         &out, &err, &status);
    } catch (const Glib::Error& e) {
        return std::string(" -- not hinted: ") + e.what();
    }
    std::error_code ec;
    if (status != 0 || !std::filesystem::exists(tmp, ec)) {
        std::filesystem::remove(tmp, ec);
        if (auto lg = log::get(log::Area::Io)) lg->warn("ttfautohint failed ({}): {}", status, err);
        const auto nl = err.find('\n');
        return " -- not hinted: ttfautohint said \"" + err.substr(0, nl) + "\"";
    }
    std::filesystem::rename(tmp, ttf, ec);
    if (ec) return " -- not hinted: " + ec.message();
    return ", hinted by ttfautohint";
}

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
        std::string hinted;
        if (ttf && m_settings.export_autohint) hinted = autohint(p);   // s10
        m_settings.export_folder = p.parent_path().string();
        save_settings();
        char buf[200];
        std::snprintf(buf, sizeof buf, " \u2014 %d glyphs, %.0f KB, %.0f ms", rep.glyphs, rep.bytes / 1024.0, rep.ms);
        std::string line = "Exported " + p.filename().string() + buf + hinted;
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

void Shell::on_font_directions() {  // handler: the menu's Every glyph > "Correct contour directions"
    std::size_t n = 0;
    if (command::correct_directions(m_doc.journal, m_doc.font, m_doc.font.glyph_order(), &n))
        say(m_doc.journal.undo_label() + ". Ctrl+Z takes it back.", false);
    else
        say("Every contour already runs the right way.", false);
}

namespace {
// r9: Preferences, PROOF -> what the proof puts in. The glyph pages follow
// the navigator's selection when that is set and there is one.
model::ProofOptions proof_options(const core::Settings& st, const views::ViewContext& ctx) {
    model::ProofOptions o;
    auto has = [&](const char* p) { return std::find(st.proof_pages.begin(), st.proof_pages.end(), p) != st.proof_pages.end(); };
    o.waterfall = has("waterfall");
    o.text = has("text");
    o.spacing = has("spacing");
    o.kerning = has("kerning");
    o.features = has("features");
    o.glyphs = has("glyphs");
    o.sizes = model::parse_proof_sizes(st.proof_sizes);
    o.glyph_columns = st.proof_cells == "large" ? 5 : st.proof_cells == "small" ? 12 : 8;
    if (st.proof_glyphs == "selection" && ctx.scope && !ctx.scope->empty()) o.only = &*ctx.scope;
    return o;
}
}  // namespace

model::ProofPlan Shell::proof_plan_now() {
    model::ProofPlan p = model::proof_plan(m_doc.font, m_settings.preview_text, proof_options(m_settings, m_ctx));
    if (p.empty()) say("Nothing to proof: every page is off, or has nothing in it (Preferences, PROOF).", true);
    return p;
}

void Shell::on_proof_pdf() {  // handler: the menu's "Export proof as PDF…" (s10)
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Export proof as PDF");
    dialog->set_initial_name(otf::default_file_name(m_doc.font.info(), "-proof.pdf"));
    std::error_code ec;
    if (!m_settings.export_folder.empty() && std::filesystem::is_directory(m_settings.export_folder, ec))
        dialog->set_initial_folder(Gio::File::create_for_path(m_settings.export_folder));
    dialog->save(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        Glib::RefPtr<Gio::File> file;
        try { file = dialog->save_finish(result); } catch (const Glib::Error&) { return; }
        if (!file) return;
        std::filesystem::path p = file->get_path();
        if (p.extension() != ".pdf") p += ".pdf";
        model::ProofPlan plan = proof_plan_now();
        if (plan.empty()) return;
        const std::string subset = plan.glyph_subset ? ", glyphs: the selection (" + std::to_string(plan.glyphs.size()) + ")" : "";
        views::ProofPrinter pr(m_doc.font, std::move(plan), m_settings.preview_kern);
        int pages = 0;
        if (!pr.ok() || !pr.write_pdf(p.string(), m_settings.proof_paper, &pages)) {
            say("Could not write the proof: " + pr.why(), true);
            return;
        }
        m_settings.export_folder = p.parent_path().string();
        save_settings();
        say("Proof: " + p.filename().string() + " -- " + std::to_string(pages) + (pages == 1 ? " page" : " pages") +
                (m_settings.proof_paper == "a4" ? ", A4" : ", Letter") + subset + ".", false);
    });
}

void Shell::on_proof_print() {  // handler: Ctrl+P (s10)
    model::ProofPlan plan = proof_plan_now();
    if (plan.empty()) return;
    auto pr = std::make_shared<views::ProofPrinter>(m_doc.font, std::move(plan), m_settings.preview_kern);
    if (!pr->ok()) { say("Could not set the proof: " + pr->why(), true); return; }
    auto op = Gtk::PrintOperation::create();
    op->set_job_name(m_doc.font.info().family + " " + m_doc.font.info().style + " proof");
    op->set_unit(Gtk::Unit::POINTS);
    op->set_use_full_page(true);   // the proof keeps its own margins
    op->signal_begin_print().connect([op, pr](const Glib::RefPtr<Gtk::PrintContext>& ctx) {
        op->set_n_pages(pr->pages(ctx->get_width(), ctx->get_height()));
    });
    op->signal_draw_page().connect([pr](const Glib::RefPtr<Gtk::PrintContext>& ctx, int page) {
        pr->draw_page(ctx->get_cairo_context()->cobj(), page, ctx->get_width(), ctx->get_height());
    });
    try {
        const auto res = op->run(Gtk::PrintOperation::Action::PRINT_DIALOG, *this);
        if (res == Gtk::PrintOperation::Result::APPLY) say("Proof sent to the printer.", false);
    } catch (const Glib::Error& e) {
        say(std::string("Printing failed: ") + e.what(), true);
    }
}

void Shell::on_font_extremes() {  // handler: the menu's Every glyph > "Add extreme points" (s10)
    std::size_t n = 0;
    if (command::fix_check_everywhere(m_doc.journal, m_doc.font, model::CheckId::Extremes, &n))
        say(m_doc.journal.undo_label() + " -- extreme points on " + std::to_string(n) + " contour(s). Ctrl+Z takes it back.", false);
    else
        say("No curve is missing an extreme point (skipped findings left alone).", false);
}

void Shell::on_font_inktraps() {  // handler: the menu's "Ink traps at every tight crotch" (s10)
    std::size_t n = 0;
    const auto am = views::fix_amounts(m_ctx);
    if (command::fix_check_everywhere(m_doc.journal, m_doc.font, model::CheckId::InkTraps, &n, am))
        say(m_doc.journal.undo_label() + " -- " + std::to_string(n) + " ink trap(s), " + std::to_string(std::lround(am.trap_width)) +
                " wide, " + std::to_string(std::lround(am.trap_depth)) + " deep. Ctrl+Z takes it back.", false);
    else
        say("No crotch tighter than 60 degrees without a trap (skipped findings left alone).", false);
}

void Shell::on_font_overlaps() {  // handler: the menu's Every glyph > "Remove overlaps" (M10c)
    std::size_t n = 0, fb = 0, ww = 0;
    const bool did = command::remove_overlaps(m_doc.journal, m_doc.font, m_doc.font.glyph_order(), &n, &fb, &ww);
    std::string tail = fb ? " " + std::to_string(fb) + " glyph(s) could not be merged cleanly and were left as drawn." : "";
    if (ww) tail += " " + std::to_string(ww) + " glyph(s) skipped: a contour runs the wrong way (correct directions first).";
    fb += ww;
    if (did)
        say(m_doc.journal.undo_label() + ". Ctrl+Z takes it back." + tail, fb > 0);
    else
        say((fb ? std::string("No overlaps removed.") : std::string("No glyph has overlaps.")) + tail, fb > 0);
}

void Shell::on_font_metrics_keys() {  // handler: the menu's Every glyph > "Apply metrics keys" (M6)
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
    restore_pick(r.detail);
    say("Undid: " + r.label, false);
    if (auto lg = log::get(log::Area::Shell)) lg->info("undo: {}", r.label);
}

void Shell::on_redo() {  // handler: put the undone step back
    const auto r = m_doc.journal.redo();
    if (!r.done) return;
    m_ctx.select(r.selection.empty() ? std::string{} : r.selection.front());
    restore_pick(r.detail);
    say("Redid: " + r.label, false);
    if (auto lg = log::get(log::Area::Shell)) lg->info("redo: {}", r.label);
}

// s09: put back the pick a step recorded -- only on the glyph it was made in,
// and only the points that still exist.
void Shell::restore_pick(const std::string& detail) {
    std::string glyph;
    geom::Points pts = geom::pick_from_text(detail, &glyph);
    if (detail.empty() || glyph != m_ctx.selected) return;
    const model::Glyph* g = m_doc.font.glyph(glyph);
    if (!g) return;
    for (auto it = pts.begin(); it != pts.end();)
        it = geom::valid(*g, *it) ? std::next(it) : pts.erase(it);
    if (pts == m_ctx.pick) return;
    m_ctx.pick = std::move(pts);
    m_ctx.pick_changed.emit();
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

// ─────────────────────────────────────────────────────────────────────────────
// The Primer (M12). Its own window, beside this one; its "Try it" links are
// done HERE, in the main window, so the note stays open while you try it.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_primer(const std::string& note) {  // handler: F1
    if (!m_primer)
        m_primer = std::make_unique<PrimerWindow>([this](const std::string& t) { on_primer_link(t); });
    m_primer->show(*this, note);
}

void Shell::on_primer_link(const std::string& target) {  // handler
    const core::LinkTarget t = core::parse_link_target(target);
    if (!t.valid) { say("The Primer link '" + target + "' isn't one Granjon knows.", true); return; }
    const auto& f = m_doc.font;
    // The editor on these glyphs (as tabs, the first in front); false when the
    // font has none of them.
    const auto edit = [&](const std::string& list) {
        std::vector<std::string> names;
        std::size_t a = 0;
        while (a <= list.size()) {
            const auto b = list.find(',', a);
            const std::string n = list.substr(a, b == std::string::npos ? std::string::npos : b - a);
            if (!n.empty() && f.glyph(n)) names.push_back(n);
            if (b == std::string::npos) break;
            a = b + 1;
        }
        if (names.empty()) return false;
        m_ctx.select(names.front());
        m_ctx.open_tabs(names, names.front());
        return true;
    };
    if (t.kind == "tool") {
        const auto c = t.arg.find(':');
        const std::string tool = t.arg.substr(0, c);
        const std::string glyph = c == std::string::npos ? std::string{} : t.arg.substr(c + 1);
        if (!glyph.empty() && !edit(glyph)) {
            say("This font has no '" + glyph + "' yet -- draw one (or start a font from a preset, Ctrl+N).", true);
            return;
        }
        if (glyph.empty()) m_ctx.show_view("editor");
        m_editor.use_tool(tool);
        m_editor.focus_canvas();
        if (tool == "measure")
            say("Measure: click inside a stem for its width, or drag between two points. Escape returns to Pick.", false);
        else if (tool == "pen")
            say("Pen: click for corners, drag for curves, click the first node to close.", false);
    } else if (t.kind == "glyph") {
        if (!edit(t.arg)) say("This font has none of: " + t.arg + ". Start one from a preset (Ctrl+N).", true);
    } else if (t.kind == "view") {
        m_ctx.show_view(t.arg);
    } else if (t.kind == "report") {
        m_ctx.show_view("report");
        const int n = m_report.open_check(t.arg);
        if (n == 0) say("No " + t.arg + " findings: every glyph passes that check.", false);
    } else if (t.kind == "spacing") {
        m_settings.spacing_text = t.arg;
        save_settings();
        m_ctx.settings_changed.emit();
        m_ctx.show_view("spacing");
    } else if (t.kind == "font") {
        m_ctx.set_kern_pick(std::nullopt);
        m_ctx.select({});
        if (!m_inspector.get_visible()) activate_action("win.show-inspector");
        say("The inspector's font page: Vertical Metrics (overshoot), and Started From for a preset font.", false);
    } else if (t.kind == "action") {
        activate_action("win." + t.arg);
    }
    present();
}

}  // namespace granjon
