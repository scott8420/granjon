#include "views/ReportList.hpp"

#include "Log.hpp"
#include "command/Verbs.hpp"
#include "model/Spacing.hpp"

#include <glibmm/main.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/togglebutton.h>

#include <algorithm>
#include <chrono>

namespace granjon::views {

namespace {
constexpr std::size_t kMaxRows = 200;      // rows drawn per check; Fix all covers the rest
constexpr std::size_t kOpenUpTo = 50;      // a section opens by default up to this many
}  // namespace

ReportList::ReportList(ViewContext& ctx)
    : widgets::Box("report", Gtk::Orientation::VERTICAL, 0),
      m_ctx(ctx),
      m_bar("report.bar", Gtk::Orientation::HORIZONTAL, 8),
      m_summary("report.summary"),
      m_show_skipped("report.show_skipped"),
      m_fix_all("report.fix_all"),
      m_recheck("report.recheck"),
      m_scroll("report.scroll"),
      m_list("report.list", Gtk::Orientation::VERTICAL, 10) {
    add_css_class("report");

    m_bar.add_css_class("overview-bar");
    m_summary.set_xalign(0.0f);
    m_summary.set_hexpand(true);
    m_summary.set_ellipsize(Pango::EllipsizeMode::END);
    m_show_skipped.set_label("Show skipped");
    m_show_skipped.set_tooltip_text("List the findings you skipped too (greyed, with Unskip)");
    m_show_skipped.signal_toggled().connect([this] { rebuild(); });
    m_fix_all.set_label("Fix all");
    m_fix_all.set_tooltip_text("Fix every fixable finding in the report (skipped ones stay) -- one undo step");
    m_fix_all.signal_clicked().connect([this] {
        std::vector<model::Finding> all;
        for (const auto& c : model::checks())
            for (auto& x : visible(c.id))
                if (!model::is_skipped(m_ctx.doc.font, x)) all.push_back(x);
        std::size_t n = 0;
        if (command::fix_findings(m_ctx.doc.journal, m_ctx.doc.font, all, &n) && m_ctx.say)
            m_ctx.say(m_ctx.doc.journal.undo_label() + ". Ctrl+Z takes it back.", false);
    });
    m_recheck.set_label("Re-check");
    m_recheck.set_tooltip_text("Run every check on every glyph again");
    m_recheck.signal_clicked().connect([this] { recheck_all(); rebuild(); });
    m_bar.append(m_summary);
    m_bar.append(m_show_skipped);
    m_bar.append(m_recheck);
    m_bar.append(m_fix_all);
    append(m_bar);

    m_list.set_margin(14);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    m_scroll.set_child(m_list);
    append(m_scroll);

    signal_map().connect([this] {
        m_shown = true;
        if (m_stale) { recheck_all(); rebuild(); }
    });
    signal_unmap().connect([this] { m_shown = false; });
}

void ReportList::refresh() {
    m_stale = true;
    if (m_shown) { recheck_all(); rebuild(); }
}

void ReportList::update(const std::set<std::string>& ids) {
    if (!m_shown || m_stale) { m_stale = true; return; }
    const auto& f = m_ctx.doc.font;
    bool all = false, ui = false;
    std::set<std::string> glyphs;
    for (const auto& id : ids) {
        if (id == model::Font::kMetrics || id == model::Font::kInfo || id == model::Font::kMetricsKeys) all = true;
        else if (id == model::Font::kSkips) ui = true;
        else if (!id.empty() && id[0] != '@') glyphs.insert(id);
    }
    if (all) { recheck_all(); rebuild(); return; }
    if (glyphs.empty() && !ui) return;
    // A glyph another one's key refers to: those keyed glyphs re-check too.
    std::set<std::string> more;
    for (const auto& [name, k] : f.metrics_keys().keys)
        for (const std::string* s : {&k.left, &k.right, &k.width}) {
            const auto e = model::parse_key_expr(*s);
            if (e.valid && glyphs.count(e.ref)) more.insert(name);
        }
    glyphs.insert(more.begin(), more.end());
    for (const auto& g : glyphs) recheck(g);
    rebuild();
}

void ReportList::recheck_all() {
    const auto t0 = std::chrono::steady_clock::now();
    m_found.clear();
    const auto order = m_ctx.doc.font.glyph_order();
    for (const auto& n : order) recheck(n);
    m_stale = false;
    // Left in on purpose, like the shell's slow-repaint line: a full check
    // long enough to feel is worth knowing about on a real font.
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ms > 50)
        if (auto lg = log::get(log::Area::Shell)) lg->info("report: full check {} ms ({} glyphs)", static_cast<long>(ms), order.size());
}

void ReportList::recheck(const std::string& glyph) {
    auto xs = model::check_glyph(m_ctx.doc.font, glyph);
    if (xs.empty()) m_found.erase(glyph);
    else            m_found[glyph] = std::move(xs);
}

std::vector<model::Finding> ReportList::visible(model::CheckId id) const {
    std::vector<model::Finding> out;
    const bool skipped = m_show_skipped.get_active();
    for (const auto& n : m_ctx.doc.font.glyph_order()) {   // glyph order, so the list reads like the font
        const auto it = m_found.find(n);
        if (it == m_found.end()) continue;
        for (const auto& x : it->second)
            if (x.check == id && (skipped || !model::is_skipped(m_ctx.doc.font, x))) out.push_back(x);
    }
    return out;
}

void ReportList::rebuild() {
    while (auto* c = m_list.get_first_child()) m_list.remove(*c);
    const auto& f = m_ctx.doc.font;
    std::size_t total = 0, skipped = 0, fixable = 0;
    std::set<std::string> glyphs;
    for (const auto& [n, xs] : m_found)
        for (const auto& x : xs) {
            if (model::is_skipped(f, x)) { ++skipped; continue; }
            ++total;
            glyphs.insert(n);
            if (x.fixable) ++fixable;
        }
    std::string sum = total == 0 ? "No findings" : std::to_string(total) + (total == 1 ? " finding in " : " findings in ") +
                                                       std::to_string(glyphs.size()) + (glyphs.size() == 1 ? " glyph" : " glyphs");
    if (skipped) sum += "  (" + std::to_string(skipped) + " skipped)";
    m_summary.set_text(sum);
    m_fix_all.set_sensitive(fixable > 0);

    if (total == 0 && !(m_show_skipped.get_active() && skipped)) {
        auto empty = Gtk::make_managed<Gtk::Label>(m_found.empty() ? "Every check passes on every glyph."
                                                                   : "Nothing left but what you skipped.");
        empty->add_css_class("hint");
        empty->set_margin_top(40);
        m_list.append(*empty);
        return;
    }

    for (const auto& info : model::checks()) {
        const auto xs = visible(info.id);
        if (xs.empty()) continue;
        std::size_t live = 0, can_fix = 0;
        for (const auto& x : xs)
            if (!model::is_skipped(f, x)) { ++live; if (x.fixable) ++can_fix; }
        const bool open = (xs.size() <= kOpenUpTo) != (m_toggled.count(info.id) > 0);

        auto card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        card->add_css_class("card");
        auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto fold = Gtk::make_managed<Gtk::Button>();
        fold->set_has_frame(false);
        fold->set_icon_name(open ? "pan-down-symbolic" : "pan-end-symbolic");
        fold->set_tooltip_text(open ? "Fold" : "Unfold");
        const auto id = info.id;
        fold->signal_clicked().connect([this, id] {
            if (!m_toggled.erase(id)) m_toggled.insert(id);
            Glib::signal_idle().connect_once([this] { rebuild(); });   // not inside the button's own click
        });
        auto title = Gtk::make_managed<Gtk::Label>(std::string(info.name) + "  ·  " + std::to_string(live));
        title->add_css_class("card-title");
        title->set_xalign(0.0f);
        title->set_hexpand(true);
        head->append(*fold);
        head->append(*title);
        if (can_fix > 0) {
            auto fix = Gtk::make_managed<Gtk::Button>("Fix all " + std::to_string(can_fix));
            fix->set_tooltip_text("Fix every one of these -- one undo step");
            fix->signal_clicked().connect([this, id] {
                std::vector<model::Finding> todo;
                for (auto& x : visible(id)) if (!model::is_skipped(m_ctx.doc.font, x)) todo.push_back(x);
                if (command::fix_findings(m_ctx.doc.journal, m_ctx.doc.font, todo) && m_ctx.say)
                    m_ctx.say(m_ctx.doc.journal.undo_label() + ".", false);
            });
            head->append(*fix);
        }
        if (live > 0) {
            auto skip = Gtk::make_managed<Gtk::Button>("Skip all");
            skip->set_tooltip_text("Skip every one of these (saved with the font; Show skipped brings them back) -- one undo step");
            skip->signal_clicked().connect([this, id] {
                std::vector<model::Finding> todo;
                for (auto& x : visible(id)) if (!model::is_skipped(m_ctx.doc.font, x)) todo.push_back(x);
                command::skip_findings(m_ctx.doc.journal, m_ctx.doc.font, todo, true);
            });
            head->append(*skip);
        }
        card->append(*head);
        auto why = Gtk::make_managed<Gtk::Label>(info.why);
        why->set_wrap(true);
        why->set_xalign(0.0f);
        why->add_css_class("hint");
        card->append(*why);

        if (open) {
            auto rows = Gtk::make_managed<Gtk::ListBox>();
            rows->set_selection_mode(Gtk::SelectionMode::NONE);
            rows->set_activate_on_single_click(true);
            rows->add_css_class("report-rows");
            auto shown = std::make_shared<std::vector<model::Finding>>();
            for (const auto& x : xs) {
                if (shown->size() >= kMaxRows) break;
                shown->push_back(x);
                const bool sk = model::is_skipped(f, x);
                auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
                row->set_margin_top(2);
                row->set_margin_bottom(2);
                auto name = Gtk::make_managed<Gtk::Label>(x.glyph);
                name->set_xalign(0.0f);
                name->set_width_chars(14);
                name->set_max_width_chars(14);
                name->set_ellipsize(Pango::EllipsizeMode::END);
                name->add_css_class("report-glyph");
                auto msg = Gtk::make_managed<Gtk::Label>(x.message);
                msg->set_xalign(0.0f);
                msg->set_hexpand(true);
                msg->set_wrap(true);
                row->append(*name);
                row->append(*msg);
                if (sk) {
                    name->add_css_class("dim");
                    msg->add_css_class("dim");
                }
                if (x.fixable && !sk) {
                    auto fix = Gtk::make_managed<Gtk::Button>("Fix");
                    fix->set_valign(Gtk::Align::CENTER);
                    fix->signal_clicked().connect([this, x] {
                        command::fix_findings(m_ctx.doc.journal, m_ctx.doc.font, {x});
                    });
                    row->append(*fix);
                }
                auto skip = Gtk::make_managed<Gtk::Button>(sk ? "Unskip" : "Skip");
                skip->set_valign(Gtk::Align::CENTER);
                skip->set_has_frame(false);
                skip->signal_clicked().connect([this, x, sk] {
                    command::skip_findings(m_ctx.doc.journal, m_ctx.doc.font, {x}, !sk);
                });
                row->append(*skip);
                rows->append(*row);
            }
            rows->signal_row_activated().connect([this, shown](Gtk::ListBoxRow* r) {
                const int i = r ? r->get_index() : -1;
                if (i >= 0 && i < static_cast<int>(shown->size())) go_to((*shown)[i]);
            });
            card->append(*rows);
            if (xs.size() > kMaxRows) {
                auto more = Gtk::make_managed<Gtk::Label>("… and " + std::to_string(xs.size() - kMaxRows) +
                                                          " more (Fix all covers them)");
                more->set_xalign(0.0f);
                more->add_css_class("hint");
                card->append(*more);
            }
        }
        m_list.append(*card);
    }
}

void ReportList::go_to(const model::Finding& x) {
    const model::Glyph* g = m_ctx.doc.font.glyph(x.glyph);
    if (!g) return;
    m_ctx.select(x.glyph);
    m_ctx.pick.clear();
    m_ctx.guide_pick.reset();
    using Part = geom::PointRef::Part;
    if (x.contour >= 0 && x.contour < static_cast<int>(g->contours.size())) {
        if (x.index >= 0 && x.index < static_cast<int>(g->contours[x.contour].nodes.size()))
            m_ctx.pick.insert({x.contour, x.index, Part::Node});
        else
            for (int i = 0; i < static_cast<int>(g->contours[x.contour].nodes.size()); ++i)
                m_ctx.pick.insert({x.contour, i, Part::Node});
    }
    if (m_ctx.show_view) m_ctx.show_view("editor");
    m_ctx.pick_changed.emit();
    if (m_ctx.say) m_ctx.say(x.glyph + ": " + x.message, false);
}

}  // namespace granjon::views
