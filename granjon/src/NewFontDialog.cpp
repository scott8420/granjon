#include "NewFontDialog.hpp"

#include "Registry.hpp"
#include "unicode/CharSet.hpp"
#include "views/GlyphPaint.hpp"

#include <gtkmm/drawingarea.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/stringlist.h>

#include <algorithm>
#include <cmath>

namespace granjon {

namespace {

// The six starter glyphs set in a row, fitted to the card: descender ..
// ascender tall, their advances wide, filled in the widget's own colour.
void paint_sample(Gtk::Widget& w, const Cairo::RefPtr<Cairo::Context>& cr, int width, int height,
                  const model::Font* f) {
    const auto fg = w.get_color();
    if (!f) {   // Blank: an empty dashed frame
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.35);
        cr->set_line_width(1.0);
        const std::vector<double> dash{4.0, 3.0};
        cr->set_dash(dash, 0);
        const double s = std::min(width, height) * 0.62;
        cr->rectangle(std::round((width - s * 0.7) / 2) + 0.5, std::round((height - s) / 2) + 0.5,
                      std::round(s * 0.7), std::round(s));
        cr->stroke();
        return;
    }
    const auto& m = f->metrics();
    double total = 0;
    for (const auto& n : model::control_glyphs())
        if (const auto* g = f->glyph(n)) total += g->advance;
    const double em_h = m.ascender - m.descender;
    const double pad = 10;
    const double s = std::min((width - 2 * pad) / std::max(1.0, total), (height - 2 * pad) / em_h);
    cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), fg.get_alpha());
    cr->translate((width - total * s) / 2, (height - em_h * s) / 2 + m.ascender * s);
    cr->scale(s, -s);
    double x = 0;
    for (const auto& n : model::control_glyphs()) {
        const auto* g = f->glyph(n);
        if (!g) continue;
        cr->save();
        cr->translate(x, 0);
        views::append_glyph_path(cr, *f, *g);
        cr->restore();
        x += g->advance;
    }
    cr->set_fill_rule(Cairo::Context::FillRule::WINDING);
    cr->fill();
}

}  // namespace

NewFontDialog::NewFontDialog()
    : m_root("newfont.root", Gtk::Orientation::VERTICAL, 12),
      m_cards_box("newfont.cards", Gtk::Orientation::HORIZONTAL, 8),
      m_blurb("newfont.blurb"),
      m_numbers("newfont.numbers"),
      m_family("newfont.family"),
      m_charset("newfont.charset"),
      m_cancel("newfont.cancel"),
      m_ok("newfont.create") {
    set_name("shell.newfont");
    registry::add("shell.newfont", this);
    set_title("New Font");
    set_modal(true);
    set_resizable(false);
    set_hide_on_close(true);

    m_root.set_margin(18);

    auto head = Gtk::make_managed<Gtk::Label>("Start from");
    head->add_css_class("card-title");
    head->set_xalign(0.0f);
    m_root.append(*head);

    // The cards: a radio group of toggle buttons.
    m_cards.push_back({"", nullptr, nullptr});
    for (const auto& p : model::presets()) {
        Card c;
        c.id = p.id;
        c.sample = std::make_unique<model::Font>();
        model::make_preset_font(*c.sample, {p.id, p.name, "@none", false});
        m_cards.push_back(std::move(c));
    }
    widgets::ToggleButton* first = nullptr;
    for (auto& c : m_cards) {
        const model::Preset* p = model::preset(c.id);
        auto b = Gtk::make_managed<widgets::ToggleButton>("newfont.card." + (c.id.empty() ? std::string("blank") : c.id));
        b->add_css_class("preset-card");
        if (first) b->set_group(*first); else first = b;
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        auto area = Gtk::make_managed<Gtk::DrawingArea>();
        area->set_content_width(176);
        area->set_content_height(70);
        const model::Font* sample = c.sample.get();
        area->set_draw_func([area, sample](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
            paint_sample(*area, cr, w, h, sample);
        });
        auto name = Gtk::make_managed<Gtk::Label>(p ? p->name : "Blank");
        name->add_css_class("preset-name");
        box->append(*area);
        box->append(*name);
        b->set_child(*box);
        b->set_tooltip_text(p ? p->blurb : "An empty font: .notdef and space, the default metrics. Nothing drawn.");
        const std::string id = c.id;
        b->signal_toggled().connect([this, b, id] { if (b->get_active()) pick(id); });
        c.button = b;
        m_cards_box.append(*b);
    }
    m_root.append(m_cards_box);

    // What the pick means.
    m_blurb.set_xalign(0.0f);
    m_blurb.set_wrap(true);
    m_blurb.set_max_width_chars(80);
    m_numbers.set_xalign(0.0f);
    m_numbers.set_wrap(true);
    m_numbers.add_css_class("hint");
    m_root.append(m_blurb);
    m_root.append(m_numbers);

    // Name, set, glyphs.
    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_column_spacing(12);
    grid->set_row_spacing(8);
    grid->set_margin_top(6);
    auto label = [](const char* t) {
        auto l = Gtk::make_managed<Gtk::Label>(t);
        l->add_css_class("field-label");
        l->set_xalign(1.0f);
        return l;
    };
    m_family.set_text("Untitled");
    m_family.set_hexpand(true);
    m_family.signal_activate().connect([this] { create(); });
    grid->attach(*label("Family"), 0, 0);
    grid->attach(m_family, 1, 0);

    std::vector<Glib::ustring> names{"Control glyphs only (n o H O p v)"};
    m_charset_ids = {"@none"};
    for (const auto& cs : unicode::charsets()) {
        names.push_back(cs.name + "  (" + std::to_string(cs.cps.size()) + ")");
        m_charset_ids.push_back(cs.id);
    }
    m_charset.set_model(Gtk::StringList::create(names));
    m_charset.property_selected().signal_changed().connect([this] {
        m_create_set.set_sensitive(!m_pick.empty() && chosen_charset() != "@none");
    });
    grid->attach(*label("Character set"), 0, 1);
    grid->attach(m_charset, 1, 1);

    m_create_set.set_label("Add the set's glyphs now: empty, accented letters built from parts");
    m_create_set.set_tooltip_text("Every character in the set gets a glyph. Accented letters are components "
                                  "(base + mark), so they fill in as you draw the base letters and marks. "
                                  "Off: only the control glyphs; the Overview still shows the set's gaps.");
    grid->attach(m_create_set, 1, 2);
    m_root.append(*grid);

    // Buttons, Mac order: Cancel then the default.
    auto buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    buttons->set_halign(Gtk::Align::END);
    buttons->set_margin_top(6);
    m_cancel.set_label("Cancel");
    m_ok.set_label("Create");
    m_ok.add_css_class("suggested-action");
    m_cancel.signal_clicked().connect([this] { set_visible(false); });
    m_ok.signal_clicked().connect([this] { create(); });
    buttons->append(m_cancel);
    buttons->append(m_ok);
    m_root.append(*buttons);
    set_child(m_root);
    set_default_widget(m_ok);

    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint key, guint, Gdk::ModifierType) {
        if (key == GDK_KEY_Escape) { set_visible(false); return true; }
        if (key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) {
            // Enter creates from anywhere -- a clicked card holds the focus,
            // and a toggle would otherwise eat the key. The dropdown's own
            // popup is a separate surface and keeps its Enter.
            create();
            return true;
        }
        return false;
    }, false);
    add_controller(keys);
}

NewFontDialog::~NewFontDialog() { registry::remove(this); }

void NewFontDialog::show(Gtk::Window& parent, const std::string& preset, bool create_set, Create create) {
    m_create = std::move(create);
    m_create_set.set_active(create_set);
    m_family.set_text("Untitled");
    const std::string id = model::preset(preset) ? preset : std::string{};
    for (auto& c : m_cards)
        if (c.id == id) c.button->set_active(true);
    pick(id);   // (set_active on the already-active card does not toggle)
    set_transient_for(parent);
    // Follow the main window's motif (the colour tokens hang off its class).
    if (parent.has_css_class("granjon-light")) add_css_class("granjon-light");
    else                                       remove_css_class("granjon-light");
    present();
    m_family.grab_focus();
    m_family.select_region(0, -1);
}

std::string NewFontDialog::chosen_charset() const {
    const guint i = m_charset.get_selected();
    return i < m_charset_ids.size() ? m_charset_ids[i] : std::string("@none");
}

void NewFontDialog::pick(const std::string& id) {
    m_pick = id;
    // The preset's own set is the default each time a card is picked.
    const model::Preset* p = model::preset(id);
    const std::string set = p ? p->charset : "@none";
    const auto it = std::find(m_charset_ids.begin(), m_charset_ids.end(), set);
    m_charset.set_selected(it == m_charset_ids.end() ? 0 : guint(it - m_charset_ids.begin()));
    m_charset.set_sensitive(p != nullptr);
    m_create_set.set_sensitive(p != nullptr && set != "@none");
    sync_details();
}

void NewFontDialog::sync_details() {
    const model::Preset* p = model::preset(m_pick);
    if (!p) {
        m_blurb.set_text("An empty font: .notdef and space, with the default metrics. Nothing drawn.");
        m_numbers.set_text("UPM 1000 · x-height 500 · caps 700 · overshoot 12");
        return;
    }
    m_blurb.set_text(p->blurb);
    const auto& m = p->metrics;
    std::string t = "UPM " + std::to_string(p->units_per_em) + " · x-height " + std::to_string(m.x_height) +
                    " · caps " + std::to_string(m.cap_height) + " · ascender " + std::to_string(m.ascender) +
                    " · descender " + std::to_string(m.descender) + " · overshoot " + std::to_string(m.overshoot) +
                    "\nStems: lowercase " + std::to_string(p->stem_v) + " / " + std::to_string(p->stem_h) +
                    ", capitals " + std::to_string(p->cap_stem_v) + " / " + std::to_string(p->cap_stem_h) +
                    "  (vertical / horizontal)";
    if (p->serif > 0) t += " · serifs " + std::to_string(p->serif) + " long, " + std::to_string(p->serif_t) + " thick";
    if (p->mono > 0)  t += " · every glyph " + std::to_string(p->mono) + " wide";
    t += "\nDrawn for you: n o H O p v, spaced, with spacing keys -- shapes to build on, not a finished design.";
    m_numbers.set_text(t);
}

void NewFontDialog::create() {
    if (!is_visible()) return;
    model::NewFontOptions o;
    o.preset = m_pick;
    std::string fam = m_family.get_text();
    fam.erase(0, fam.find_first_not_of(" \t"));
    fam.erase(fam.find_last_not_of(" \t") + 1);
    o.family = fam;
    o.charset = chosen_charset();
    o.create_set = m_create_set.get_active();
    set_visible(false);
    if (m_create) m_create(o);
}

}  // namespace granjon
