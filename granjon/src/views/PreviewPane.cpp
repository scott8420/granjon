#include "views/PreviewPane.hpp"

#include "Log.hpp"
#include "export/Otf.hpp"

#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

namespace granjon::views {

namespace {

constexpr int kSizes[] = {10, 12, 14, 18, 24, 32, 48, 72, 96};
constexpr const char* kParagraph =
    "Robert Granjon cut his first types in Paris around 1543 and worked on for nearly fifty years, in Lyon, "
    "Antwerp, Frankfurt and Rome. His italics set the pattern for the sixteenth century; his civilité "
    "imitated the French hand; his music types and his Arabic for the Medici press were cut for books no "
    "one had printed before. THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG — 0123456789 (§, &, ?!).";

std::filesystem::path preview_dir() {
    return std::filesystem::path(Glib::get_user_cache_dir()) / "granjon" / "preview";
}

}  // namespace

PreviewPane::PreviewPane(ViewContext& ctx)
    : widgets::Box("view.preview", Gtk::Orientation::VERTICAL, 0),
      m_ctx(ctx),
      m_bar("preview.bar", Gtk::Orientation::HORIZONTAL, 8),
      m_text("preview.text"),
      m_kern("preview.kern"),
      m_dark("preview.dark"),
      m_missing("preview.missing"),
      m_info("preview.info"),
      m_scroll("preview.scroll"),
      m_area("preview.page") {
    add_css_class("preview");

    m_bar.add_css_class("overview-bar");
    m_text.set_hexpand(true);
    m_text.set_text(m_ctx.settings.preview_text);
    m_text.set_placeholder_text("Sample text -- set in the compiled font");
    m_text.set_tooltip_text("Set by Pango in the font as it compiles (the bytes Export writes). Remembered.");
    m_text.signal_changed().connect([this] {
        if (m_syncing) return;
        m_ctx.settings.preview_text = m_text.get_text();
        if (m_ctx.save_settings) m_ctx.save_settings();
        update_missing();
        m_area.queue_draw();
    });
    m_kern.set_label("Kerning");
    m_kern.set_active(m_ctx.settings.preview_kern);
    m_kern.set_tooltip_text("The compiled GPOS kerning on / off -- compare. Remembered.");
    m_kern.signal_toggled().connect([this] {
        if (m_syncing) return;
        m_ctx.settings.preview_kern = m_kern.get_active();
        if (m_ctx.save_settings) m_ctx.save_settings();
        m_area.queue_draw();
    });
    m_dark.set_label("Dark");
    m_dark.set_active(m_ctx.settings.preview_dark);
    m_dark.set_tooltip_text("Light text on dark. Remembered.");
    m_dark.signal_toggled().connect([this] {
        if (m_syncing) return;
        m_ctx.settings.preview_dark = m_dark.get_active();
        if (m_ctx.save_settings) m_ctx.save_settings();
        m_area.queue_draw();
    });
    m_missing.add_css_class("dim");
    m_missing.set_ellipsize(Pango::EllipsizeMode::END);
    m_missing.set_max_width_chars(24);
    m_info.add_css_class("dim");
    m_info.set_text("Not compiled yet.");
    m_bar.append(m_text);
    m_bar.append(m_missing);
    m_bar.append(m_kern);
    m_bar.append(m_dark);
    m_bar.append(m_info);
    append(m_bar);

    m_area.set_hexpand(true);
    m_area.set_draw_func(sigc::mem_fun(*this, &PreviewPane::on_draw));
    m_scroll.set_child(m_area);
    m_scroll.set_vexpand(true);
    m_scroll.set_hexpand(true);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    append(m_scroll);

    signal_map().connect([this] { if (m_stale) schedule(0); });
    m_ctx.settings_changed.connect([this] {
        m_syncing = true;
        if (std::string(m_text.get_text()) != m_ctx.settings.preview_text) m_text.set_text(m_ctx.settings.preview_text);
        if (m_kern.get_active() != m_ctx.settings.preview_kern) m_kern.set_active(m_ctx.settings.preview_kern);
        if (m_dark.get_active() != m_ctx.settings.preview_dark) m_dark.set_active(m_ctx.settings.preview_dark);
        m_syncing = false;
        m_area.queue_draw();
    });
    update_missing();

    // Preview files a crashed or killed run left behind (its process is gone).
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(preview_dir(), ec)) {
        const std::string n = e.path().filename().string();
        if (n.rfind("preview-", 0) != 0) continue;
        const std::string pid = n.substr(8, n.find('-', 8) - 8);
        if (pid.empty() || std::filesystem::exists("/proc/" + pid, ec)) continue;
        std::filesystem::remove(e.path(), ec);
    }
}

PreviewPane::~PreviewPane() {
    m_timer.disconnect();
    drop_font();
}

void PreviewPane::drop_font() {
    if (m_pctx) { g_object_unref(m_pctx); m_pctx = nullptr; }
    if (m_fontmap) { g_object_unref(m_fontmap); m_fontmap = nullptr; }
    std::error_code ec;
    if (!m_file.empty()) std::filesystem::remove(m_file, ec);
    m_file.clear();
}

void PreviewPane::refresh() {
    m_stale = true;
    update_missing();
    if (get_mapped()) schedule(350);
}

void PreviewPane::schedule(int ms) {
    m_timer.disconnect();
    m_timer = Glib::signal_timeout().connect([this] { compile(); return false; }, static_cast<unsigned>(ms));
}

void PreviewPane::compile() {
    m_stale = false;
    otf::OtfReport rep;
    const otf::Bytes font = otf::compile_otf(m_ctx.doc.font, {}, &rep);

    // A fresh file name each time: cairo and FreeType cache faces by file, so
    // rewriting one path could show the old outlines.
    std::error_code ec;
    std::filesystem::create_directories(preview_dir(), ec);
    const std::string file =
        (preview_dir() / ("preview-" + std::to_string(::getpid()) + "-" + std::to_string(++m_serial) + ".otf")).string();
    {
        std::ofstream o(file, std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(font.data()), static_cast<std::streamsize>(font.size()));
        if (!o) {
            m_info.set_text("Preview: can't write the cache file.");
            if (auto lg = log::get(log::Area::Io)) lg->warn("preview: can't write '{}'", file);
            return;
        }
    }
    FcConfig* cfg = FcConfigCreate();
    if (!cfg || !FcConfigAppFontAddFile(cfg, reinterpret_cast<const FcChar8*>(file.c_str()))) {
        if (cfg) FcConfigDestroy(cfg);
        std::filesystem::remove(file, ec);
        m_info.set_text("Preview: fontconfig refused the compiled font.");
        if (auto lg = log::get(log::Area::Io)) lg->warn("preview: fontconfig refused '{}'", file);
        return;
    }
    PangoFontMap* fm = pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
    if (!fm || !PANGO_IS_FC_FONT_MAP(fm)) {
        if (fm) g_object_unref(fm);
        FcConfigDestroy(cfg);
        std::filesystem::remove(file, ec);
        m_info.set_text("Preview: no FreeType font map on this system.");
        return;
    }
    pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(fm), cfg);
    FcConfigDestroy(cfg);   // the font map holds its own reference

    drop_font();
    m_fontmap = fm;
    m_pctx = pango_font_map_create_context(fm);
    m_file = file;
    m_family = m_ctx.doc.font.info().family.empty() ? "Untitled" : m_ctx.doc.font.info().family;

    char buf[160];
    std::snprintf(buf, sizeof buf, "%d glyphs · %.0f KB · %.0f ms", rep.glyphs, rep.bytes / 1024.0, rep.ms);
    m_info.set_text(buf);
    std::string tip = "Compiled as Export writes it.";
    for (const auto& n : rep.notes) tip += "\n" + n;
    m_info.set_tooltip_text(tip);
    if (auto lg = log::get(log::Area::Io)) lg->info("preview: compiled {} glyphs, {} bytes, {:.1f} ms", rep.glyphs, rep.bytes, rep.ms);
    m_area.queue_draw();
}

void PreviewPane::update_missing() {
    std::set<char32_t> miss;
    const Glib::ustring t = m_text.get_text();
    std::u32string all(t.begin(), t.end());
    const Glib::ustring para(kParagraph);
    all.append(para.begin(), para.end());
    for (char32_t c : all)
        if (c != U' ' && !m_ctx.doc.font.glyph_for(c)) miss.insert(c);
    if (miss.empty()) { m_missing.set_text(""); m_missing.set_tooltip_text(""); return; }
    Glib::ustring s;
    for (char32_t c : miss) s += static_cast<gunichar>(c);
    m_missing.set_text(std::to_string(miss.size()) + " missing");
    m_missing.set_tooltip_text("Not in the font (they show as boxes): " + s);
}

void PreviewPane::on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int) {
    const bool dark = m_ctx.settings.preview_dark;
    const double fg = dark ? 0.93 : 0.08, bg = dark ? 0.11 : 1.0, dim = dark ? 0.55 : 0.55;
    cr->set_source_rgb(bg, bg, bg);
    cr->paint();
    const double gutter = 56, pad = 16;
    double y = pad;

    auto ui = m_area.create_pango_layout("");
    auto ui_label = [&](const std::string& s, double x, double base) {
        ui->set_text(s);
        Pango::FontDescription d = m_area.get_pango_context()->get_font_description();
        d.set_size(8 * PANGO_SCALE);
        ui->set_font_description(d);
        cr->set_source_rgb(dim, dim, dim);
        cr->move_to(x, base - ui->get_baseline() / static_cast<double>(PANGO_SCALE));
        ui->show_in_cairo_context(cr);
    };
    if (!m_pctx) {
        ui_label(m_stale ? "Compiling…" : "Nothing compiled.", pad, y + 20);
        return;
    }

    pango_cairo_update_context(cr->cobj(), m_pctx);
    std::string sample = m_text.get_text();
    if (sample.empty()) sample = "Hamburgefonstiv";

    auto set = [&](const std::string& text, int px, int width_px, bool wrap) {
        PangoLayout* l = pango_layout_new(m_pctx);
        pango_layout_set_text(l, text.c_str(), -1);
        PangoFontDescription* d = pango_font_description_new();
        pango_font_description_set_family(d, m_family.c_str());
        pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
        pango_layout_set_font_description(l, d);
        pango_font_description_free(d);
        if (!m_ctx.settings.preview_kern) {
            PangoAttrList* attrs = pango_attr_list_new();
            pango_attr_list_insert(attrs, pango_attr_font_features_new("kern 0"));
            pango_layout_set_attributes(l, attrs);
            pango_attr_list_unref(attrs);
        }
        pango_layout_set_width(l, width_px * PANGO_SCALE);
        if (wrap) pango_layout_set_wrap(l, PANGO_WRAP_WORD);
        else pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
        return l;
    };

    const int text_w = std::max(40, w - static_cast<int>(gutter + pad));
    for (int px : kSizes) {
        PangoLayout* l = set(sample, px, text_w, false);
        const double base = y + pango_layout_get_baseline(l) / static_cast<double>(PANGO_SCALE);
        int lw = 0, lh = 0;
        pango_layout_get_pixel_size(l, &lw, &lh);
        ui_label(std::to_string(px), pad, base);
        cr->set_source_rgb(fg, fg, fg);
        cr->move_to(gutter, y);
        pango_cairo_show_layout(cr->cobj(), l);
        g_object_unref(l);
        y += std::max(lh, px) + 8;
    }
    y += 16;
    for (int px : {14, 18}) {
        PangoLayout* l = set(kParagraph, px, std::min(text_w, px * 38), true);
        int lw = 0, lh = 0;
        pango_layout_get_pixel_size(l, &lw, &lh);
        ui_label(std::to_string(px), pad, y + pango_layout_get_baseline(l) / static_cast<double>(PANGO_SCALE));
        cr->set_source_rgb(fg, fg, fg);
        cr->move_to(gutter, y);
        pango_cairo_show_layout(cr->cobj(), l);
        g_object_unref(l);
        y += lh + 24;
    }
    const int want = static_cast<int>(std::ceil(y + pad));
    if (m_area.get_content_height() != want) {
        Glib::signal_idle().connect_once([this, want] { m_area.set_content_height(want); });
    }
}

}  // namespace granjon::views
