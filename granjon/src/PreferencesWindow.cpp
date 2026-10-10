#include "PreferencesWindow.hpp"

#include "unicode/CharSet.hpp"

#include <gtkmm/label.h>
#include <gtkmm/stringlist.h>

#include <algorithm>
#include <array>

namespace granjon {

namespace {

// The modifier vocabulary: what the file stores, what the window says.
constexpr std::array<const char*, 3> kModIds   = {"alt", "ctrl", "shift"};
constexpr std::array<const char*, 3> kModNames = {"Alt", "Ctrl", "Shift"};

Gtk::Box* card(Gtk::Box& into, const char* title) {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->add_css_class("card");
    auto t = Gtk::make_managed<Gtk::Label>(title);
    t->add_css_class("card-title");
    t->set_xalign(0.0f);
    box->append(*t);
    into.append(*box);
    return box;
}

// label ........ control   (and an optional hint under the label)
void row(Gtk::Box& card, const char* label, Gtk::Widget& control, const char* hint = nullptr) {
    auto line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    auto text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    text->set_hexpand(true);
    auto l = Gtk::make_managed<Gtk::Label>(label);
    l->add_css_class("field-label");
    l->set_xalign(0.0f);
    text->append(*l);
    if (hint) {
        auto h = Gtk::make_managed<Gtk::Label>(hint);
        h->add_css_class("hint");
        h->set_xalign(0.0f);
        h->set_wrap(true);
        h->set_max_width_chars(42);
        text->append(*h);
    }
    control.set_valign(Gtk::Align::CENTER);
    line->append(*text);
    line->append(control);
    card.append(*line);
}

}  // namespace

PreferencesWindow::PreferencesWindow(Gtk::Window& parent, views::ViewContext& ctx, const std::string& file)
    : m_ctx(ctx),
      m_scroll("prefs.scroll"),
      m_root("prefs.root", Gtk::Orientation::VERTICAL, 12),
      m_reopen("prefs.reopen"),
      m_fill("prefs.fill"),
      m_context("prefs.context"),
      m_upper("prefs.context_upper"),
      m_lower("prefs.context_lower"),
      m_modifier("prefs.sidebearing_modifier"),
      m_cell("prefs.cell"),
      m_charset("prefs.charset"),
      m_file("prefs.file"),
      m_reset("prefs.reset") {
    set_name("shell.preferences");
    set_title("Preferences");
    set_transient_for(parent);
    set_modal(false);
    set_hide_on_close(true);
    set_default_size(520, 640);
    add_css_class("inspector");   // the cards' surface

    m_root.set_margin(16);

    // ── General ────────────────────────────────────────────────────────────
    auto* general = card(m_root, "GENERAL");
    row(*general, "Reopen the last font at launch", m_reopen);

    // ── Editor ─────────────────────────────────────────────────────────────
    auto* editor = card(m_root, "EDITOR");
    row(*editor, "Shade the outline", m_fill, "The editor toolbar's Fill.");
    row(*editor, "Show context glyphs", m_context, "The editor toolbar's Context.");
    m_upper.set_width_chars(8);
    m_lower.set_width_chars(8);
    row(*editor, "Context beside capitals and figures", m_upper, "A glyph name (H, O.ss01) or a character.");
    row(*editor, "Context beside the rest", m_lower);
    {
        std::vector<Glib::ustring> names(kModNames.begin(), kModNames.end());
        m_modifier.set_model(Gtk::StringList::create(names));
    }
    row(*editor, "Slide the drawing", m_modifier,
        "Hold this while dragging the left sidebearing to move the drawing inside the width "
        "instead of moving the origin.");

    // ── Overview ───────────────────────────────────────────────────────────
    auto* overview = card(m_root, "OVERVIEW");
    m_cell.set_range(core::kOverviewCellMin, core::kOverviewCellMax);
    m_cell.set_increments(4, 16);
    m_cell.set_width_chars(5);
    row(*overview, "Cell size", m_cell, "The overview slider.");
    {
        std::vector<Glib::ustring> names{"None"};
        for (const auto& c : unicode::charsets()) names.push_back(c.name);
        m_charset.set_model(Gtk::StringList::create(names));
    }
    row(*overview, "Coverage set", m_charset, "The overview's Coverage menu.");

    // ── footer ─────────────────────────────────────────────────────────────
    auto foot = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    m_file.add_css_class("hint");
    m_file.set_xalign(0.0f);
    m_file.set_hexpand(true);
    m_file.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    m_file.set_text(file);
    m_file.set_tooltip_text("Where these are kept: " + file);
    m_reset.set_label("Reset to defaults");
    foot->append(m_file);
    foot->append(m_reset);
    m_root.append(*foot);

    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_child(m_root);
    set_child(m_scroll);

    // ── wiring: every control writes through ───────────────────────────────
    m_reopen.property_active().signal_changed().connect([this] { commit(); });
    m_fill.property_active().signal_changed().connect([this] { commit(); });
    m_context.property_active().signal_changed().connect([this] { commit(); });
    m_upper.signal_changed().connect([this] { commit(); });
    m_lower.signal_changed().connect([this] { commit(); });
    m_modifier.property_selected().signal_changed().connect([this] { commit(); });
    m_cell.signal_value_changed().connect([this] { commit(); });
    m_charset.property_selected().signal_changed().connect([this] { commit(); });
    m_reset.signal_clicked().connect([this] {
        // Keep where things are (last font, open folder); reset how things look.
        core::Settings d;
        d.last_font   = m_ctx.settings.last_font;
        d.open_folder = m_ctx.settings.open_folder;
        m_ctx.settings = d;
        if (m_ctx.save_settings) m_ctx.save_settings();
        m_ctx.settings_changed.emit();
    });
    m_ctx.settings_changed.connect([this] { sync(); });
    sync();
}

PreferencesWindow::~PreferencesWindow() = default;

void PreferencesWindow::sync() {
    m_syncing = true;
    const auto& s = m_ctx.settings;
    if (m_reopen.get_active() != s.reopen_last) m_reopen.set_active(s.reopen_last);
    if (m_fill.get_active() != s.editor_fill) m_fill.set_active(s.editor_fill);
    if (m_context.get_active() != s.editor_context) m_context.set_active(s.editor_context);
    // An entry being typed in keeps its text: only re-set it when it differs.
    if (std::string(m_upper.get_text()) != s.context_upper) m_upper.set_text(s.context_upper);
    if (std::string(m_lower.get_text()) != s.context_lower) m_lower.set_text(s.context_lower);
    const auto mod = std::find(kModIds.begin(), kModIds.end(), s.sidebearing_mod);
    const guint mi = mod == kModIds.end() ? 0u : static_cast<guint>(mod - kModIds.begin());
    if (m_modifier.get_selected() != mi) m_modifier.set_selected(mi);
    if (static_cast<int>(m_cell.get_value()) != s.overview_cell) m_cell.set_value(s.overview_cell);
    guint ci = 0;
    const auto& sets = unicode::charsets();
    for (std::size_t i = 0; i < sets.size(); ++i)
        if (sets[i].id == s.overview_charset) ci = static_cast<guint>(i + 1);
    if (m_charset.get_selected() != ci) m_charset.set_selected(ci);
    m_syncing = false;
}

void PreferencesWindow::commit() {
    if (m_syncing) return;
    core::Settings s = m_ctx.settings;
    s.reopen_last    = m_reopen.get_active();
    s.editor_fill    = m_fill.get_active();
    s.editor_context = m_context.get_active();
    // An emptied box means "back to the default", not "no context".
    s.context_upper  = m_upper.get_text().empty() ? core::Settings{}.context_upper : std::string(m_upper.get_text());
    s.context_lower  = m_lower.get_text().empty() ? core::Settings{}.context_lower : std::string(m_lower.get_text());
    const guint mi   = m_modifier.get_selected();
    s.sidebearing_mod = mi < kModIds.size() ? kModIds[mi] : "alt";
    s.overview_cell  = static_cast<int>(m_cell.get_value());
    const guint ci   = m_charset.get_selected();
    const auto& sets = unicode::charsets();
    s.overview_charset = (ci >= 1 && ci <= sets.size()) ? sets[ci - 1].id : std::string{};
    if (s == m_ctx.settings) return;
    m_ctx.settings = s;
    if (m_ctx.save_settings) m_ctx.save_settings();
    m_ctx.settings_changed.emit();
}

}  // namespace granjon
