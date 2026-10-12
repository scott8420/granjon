// views::FeatureView (M16c) -- see FeatureView.hpp.
#include "views/FeatureView.hpp"

#include "command/Verbs.hpp"
#include "model/Fea.hpp"

#include <glibmm/main.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/eventcontrollerfocus.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gdk/gdkkeysyms.h>

#include <cctype>

namespace granjon::views {

FeatureView::FeatureView(ViewContext& ctx)
    : widgets::Box("features", Gtk::Orientation::VERTICAL, 0),
      m_ctx(ctx),
      m_bar("features.bar", Gtk::Orientation::HORIZONTAL, 8),
      m_summary("features.summary"),
      m_generate("features.generate"),
      m_scroll("features.scroll"),
      m_text("features.text"),
      m_errors("features.errors", Gtk::Orientation::VERTICAL, 2) {
    add_css_class("features");
    m_bar.add_css_class("overview-bar");
    m_summary.set_xalign(0.0f);
    m_summary.set_hexpand(true);
    m_summary.set_ellipsize(Pango::EllipsizeMode::END);
    m_generate.set_label("Generate");
    m_generate.set_tooltip_text("Write substitutions from the glyph names into an automatic block at the top -- "
                                "f_i, f_f_i -> liga; a.sc -> smcp (and c2sc from A); .ss01 -> ss01; .onum .lnum .tnum "
                                ".pnum .sups .subs .sinf .numr .dnom .case .swsh -> their features; .alt / .salt -> salt. "
                                "Your own code below the block is kept. One undo step.");
    m_generate.signal_clicked().connect([this] { generate(); });
    m_bar.append(m_summary);
    m_bar.append(m_generate);
    append(m_bar);

    m_buf = m_text.get_buffer();
    m_err_tag = m_buf->create_tag("error");
    m_err_tag->property_background_rgba() = Gdk::RGBA("rgba(237, 84, 77, 0.22)");
    m_comment_tag = m_buf->create_tag("comment");
    m_comment_tag->property_foreground_rgba() = Gdk::RGBA("rgba(128, 128, 128, 1.0)");
    m_kw_tag = m_buf->create_tag("keyword");
    m_kw_tag->property_weight() = Pango::Weight::BOLD;
    m_text.set_monospace(true);
    m_text.set_left_margin(14);
    m_text.set_top_margin(10);
    m_text.set_wrap_mode(Gtk::WrapMode::NONE);
    m_text.add_css_class("fea-text");
    // One undo system: the buffer's own would take back keystrokes behind the
    // journal's back (as the number fields, s05). Ctrl+Z / Ctrl+Shift+Z first
    // write what is still waiting for the pause, then reach the journal.
    m_buf->set_enable_undo(false);
    {
        auto keys = Gtk::EventControllerKey::create();
        keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
        keys->signal_key_pressed().connect([this](guint key, guint, Gdk::ModifierType st) {
            const bool ctrl = (st & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
            if (!ctrl || (key != GDK_KEY_z && key != GDK_KEY_Z)) return false;
            if (m_pending.connected()) {
                m_pending.disconnect();
                commit();
            }
            // the text view keeps Ctrl+Z to itself: hand it to the window
            const bool shift = (st & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
            m_text.activate_action(shift ? "win.redo" : "win.undo");
            return true;
        }, false);
        m_text.add_controller(keys);
        auto focus = Gtk::EventControllerFocus::create();
        focus->signal_leave().connect([this] { if (m_pending.connected()) { m_pending.disconnect(); commit(); } });
        m_text.add_controller(focus);
    }
    m_buf->signal_changed().connect([this] { changed(); });
    m_scroll.set_child(m_text);
    m_scroll.set_vexpand(true);
    append(m_scroll);
    m_errors.add_css_class("fea-errors");
    m_errors.set_margin(8);
    append(m_errors);
    refresh();
}

void FeatureView::refresh() { load(); }

void FeatureView::update(const std::set<std::string>& ids) {
    if (ids.count(model::Font::kFeatures)) load();
    else if (!ids.empty()) check();   // glyphs added / renamed: names may now resolve (or not)
}

void FeatureView::load() {
    const std::string& t = m_ctx.doc.font.features().text;
    if (std::string(m_buf->get_text()) == t) { check(); return; }
    m_pending.disconnect();
    m_loading = true;
    m_buf->set_text(t);
    m_loading = false;
    check();
}

void FeatureView::changed() {
    if (m_loading) return;
    m_pending.disconnect();
    m_pending = Glib::signal_timeout().connect([this] { commit(); return false; }, 600);
}

void FeatureView::commit() {
    const std::string t = m_buf->get_text();
    command::set_features(m_ctx.doc.journal, m_ctx.doc.font, t);   // merges a typing run
    check();
}

void FeatureView::check() {
    const std::string t = m_buf->get_text();
    auto& f = m_ctx.doc.font;
    const auto doc = model::parse_fea(t, [&](const std::string& n) {
        const model::Glyph* g = f.glyph(n);
        return g && !g->skip_export;
    });
    // tint: comments, keywords, error lines
    m_buf->remove_all_tags(m_buf->begin(), m_buf->end());
    {
        static const std::set<std::string> kw = {"feature", "lookup", "sub", "substitute", "by", "from", "languagesystem",
                                                  "script", "language", "pos", "position", "ignore", "lookupflag"};
        int line = 0;
        std::size_t start = 0;
        while (start <= t.size()) {
            std::size_t end = t.find('\n', start);
            if (end == std::string::npos) end = t.size();
            const std::string l = t.substr(start, end - start);
            const auto it_line = m_buf->get_iter_at_line(line);
            if (const auto h = l.find('#'); h != std::string::npos) {
                auto a = it_line, b = it_line;
                a.forward_chars(static_cast<int>(Glib::ustring(l.substr(0, h)).size()));
                b.forward_to_line_end();
                if (b.get_line() != line) b = it_line;   // an empty line
                m_buf->apply_tag(m_comment_tag, a, b);
            }
            // the first word of a statement
            std::size_t i = 0;
            while (i < l.size() && std::isspace(static_cast<unsigned char>(l[i]))) ++i;
            std::size_t j = i;
            while (j < l.size() && std::isalpha(static_cast<unsigned char>(l[j]))) ++j;
            if (j > i && kw.count(l.substr(i, j - i))) {
                auto a = it_line, b = it_line;
                a.forward_chars(static_cast<int>(i));
                b.forward_chars(static_cast<int>(j));
                m_buf->apply_tag(m_kw_tag, a, b);
            }
            ++line;
            if (end == t.size()) break;
            start = end + 1;
        }
    }
    while (auto* c = m_errors.get_first_child()) m_errors.remove(*c);
    for (const auto& e : doc.errors) {
        auto a = m_buf->get_iter_at_line(e.line - 1), b = a;
        b.forward_to_line_end();
        m_buf->apply_tag(m_err_tag, a, b);
    }
    auto row = [this](const model::FeaMessage& m, bool error) {
        auto b = Gtk::make_managed<Gtk::Button>();
        auto l = Gtk::make_managed<Gtk::Label>((error ? "Line " : "Line ") + std::to_string(m.line) + (error ? ":  " : " (skipped):  ") + m.text);
        l->set_xalign(0.0f);
        l->set_ellipsize(Pango::EllipsizeMode::END);
        if (error) l->add_css_class("error");
        else l->add_css_class("hint");
        b->set_child(*l);
        b->set_has_frame(false);
        const int line = m.line;
        b->signal_clicked().connect([this, line] {
            auto it = m_buf->get_iter_at_line(line - 1);
            m_buf->place_cursor(it);
            m_text.scroll_to(it, 0.2);
            m_text.grab_focus();
        });
        m_errors.append(*b);
    };
    std::size_t shown = 0;
    for (const auto& e : doc.errors) if (shown++ < 12) row(e, true);
    for (const auto& w : doc.warnings) if (shown++ < 16) row(w, false);
    m_errors.set_visible(shown > 0);
    if (t.find_first_not_of(" \t\r\n") == std::string::npos)
        m_summary.set_text("No features yet -- Generate makes them from the glyph names, or type them (Adobe feature syntax).");
    else if (!doc.ok())
        m_summary.set_text(std::to_string(doc.errors.size()) + (doc.errors.size() == 1 ? " error" : " errors") +
                           " -- nothing is compiled until it's fixed (export leaves GSUB out).");
    else {
        std::string tags;
        for (const auto& ft : doc.features) {
            std::string tg = ft.tag;
            while (!tg.empty() && tg.back() == ' ') tg.pop_back();
            if (tags.find(tg) == std::string::npos) tags += (tags.empty() ? "" : " ") + tg;
        }
        m_summary.set_text("Compiles: " + std::to_string(doc.features.size()) + (doc.features.size() == 1 ? " feature, " : " features, ") +
                           std::to_string(doc.rules) + (doc.rules == 1 ? " rule" : " rules") + (tags.empty() ? "" : "  (" + tags + ")") +
                           (doc.warnings.empty() ? "" : "  -- " + std::to_string(doc.warnings.size()) + " skipped"));
    }
}

void FeatureView::generate() {
    auto& f = m_ctx.doc.font;
    const std::string gen = model::generate_fea(f);
    const std::string now = m_buf->get_text();
    const std::string next = model::replace_auto_block(now, gen);
    if (gen.empty() && next == now) {
        if (m_ctx.say) m_ctx.say("Nothing to generate: no glyph names like f_i, a.sc, a.ss01, one.onum or g.alt in the font.", false);
        return;
    }
    m_pending.disconnect();
    if (command::set_features(m_ctx.doc.journal, f, next, "Generate features", false)) {
        load();
        if (m_ctx.say) m_ctx.say(m_ctx.doc.journal.undo_label() + ". Ctrl+Z takes it back.", false);
    } else if (m_ctx.say) {
        m_ctx.say("The automatic block is up to date.", false);
    }
}

}  // namespace granjon::views
