// views::ProofPrinter -- see ProofPrinter.hpp.
#include "views/ProofPrinter.hpp"

#include "export/Otf.hpp"
#include "geom/CubicSegment.hpp"
#include "geom/Edit.hpp"
#include "model/Font.hpp"

#include <cairo-pdf.h>
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace granjon::views {

struct ProofPrinter::Fonts {
    PangoFontMap* map = nullptr;    // the proof font alone
    PangoContext* ctx = nullptr;
    PangoContext* ui = nullptr;     // the desktop's fonts, for labels
    ~Fonts() {
        if (ctx) g_object_unref(ctx);
        if (ui) g_object_unref(ui);
        if (map) g_object_unref(map);
    }
};

namespace {

constexpr double kMargin = 47;      // 0.65 inch
constexpr double kHeader = 28;      // header band under the top margin

std::string today() {
    const std::time_t t = std::time(nullptr);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%d", std::localtime(&t));
    return b;
}

}  // namespace

void ProofPrinter::paper_size(const std::string& paper, double& w, double& h) {
    if (paper == "a4") { w = 595.276; h = 841.89; }
    else { w = 612; h = 792; }
}

ProofPrinter::ProofPrinter(const model::Font& f, model::ProofPlan plan, bool kern)
    : m_font(f), m_plan(std::move(plan)), m_kern(kern), m_fonts(std::make_unique<Fonts>()) {
    otf::OtfReport rep;
    const otf::Bytes bytes = otf::compile_otf(f, {}, &rep);
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) / "granjon-proof";
    std::filesystem::create_directories(dir, ec);
    static int serial = 0;
    m_file = (dir / ("proof-" + std::to_string(::getpid()) + "-" + std::to_string(++serial) + ".otf")).string();
    {
        std::ofstream o(m_file, std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!o) { m_why = "can't write the compiled font to " + dir.string(); return; }
    }
    FcConfig* cfg = FcConfigCreate();
    if (!cfg || !FcConfigAppFontAddFile(cfg, reinterpret_cast<const FcChar8*>(m_file.c_str()))) {
        if (cfg) FcConfigDestroy(cfg);
        m_why = "fontconfig refused the compiled font";
        return;
    }
    PangoFontMap* fm = pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
    if (!fm || !PANGO_IS_FC_FONT_MAP(fm)) {
        if (fm) g_object_unref(fm);
        FcConfigDestroy(cfg);
        m_why = "no FreeType font map on this system";
        return;
    }
    pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(fm), cfg);
    FcConfigDestroy(cfg);
    m_fonts->map = fm;
    m_fonts->ctx = pango_font_map_create_context(fm);
    m_fonts->ui = pango_font_map_create_context(pango_cairo_font_map_get_default());
    m_ok = true;
}

ProofPrinter::~ProofPrinter() {
    m_fonts.reset();
    std::error_code ec;
    if (!m_file.empty()) std::filesystem::remove(m_file, ec);
}

int ProofPrinter::pages(double w, double h) {
    cairo_surface_t* s = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, nullptr);
    cairo_t* cr = cairo_create(s);
    const int n = run(cr, w, h, -1);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    return n;
}

void ProofPrinter::draw_page(cairo_t* cr, int page, double w, double h) { run(cr, w, h, page); }

bool ProofPrinter::write_pdf(const std::string& path, const std::string& paper, int* pages_out) {
    if (!m_ok) return false;
    double w = 0, h = 0;
    paper_size(paper, w, h);
    const int n = pages(w, h);
    cairo_surface_t* s = cairo_pdf_surface_create(path.c_str(), w, h);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        m_why = std::string("can't write ") + path + ": " + cairo_status_to_string(cairo_surface_status(s));
        cairo_surface_destroy(s);
        return false;
    }
    cairo_pdf_surface_set_metadata(s, CAIRO_PDF_METADATA_TITLE, (m_plan.family + " " + m_plan.style + " -- proof").c_str());
    cairo_pdf_surface_set_metadata(s, CAIRO_PDF_METADATA_CREATOR, "Granjon");
    cairo_t* cr = cairo_create(s);
    for (int i = 0; i < n; ++i) {
        run(cr, w, h, i);
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_finish(s);
    const bool good = cairo_surface_status(s) == CAIRO_STATUS_SUCCESS;
    if (!good) m_why = cairo_status_to_string(cairo_surface_status(s));
    cairo_surface_destroy(s);
    if (pages_out) *pages_out = n;
    return good;
}

int ProofPrinter::run(cairo_t* cr, double W, double H, int only) {
    if (!m_ok) return 0;
    pango_cairo_update_context(cr, m_fonts->ctx);
    pango_cairo_update_context(cr, m_fonts->ui);
    const double left = kMargin, right = W - kMargin, top = kMargin + kHeader, bottom = H - kMargin - 14;
    const double width = right - left;
    int page = 0;
    double y = top;
    const std::string date = today();
    auto on = [&] { return page == only; };

    auto ui_layout = [&](const std::string& t, double pt, bool bold = false) {
        PangoLayout* l = pango_layout_new(m_fonts->ui);
        pango_layout_set_text(l, t.c_str(), -1);
        PangoFontDescription* d = pango_font_description_from_string("Sans");
        pango_font_description_set_size(d, static_cast<int>(pt * PANGO_SCALE));
        if (bold) pango_font_description_set_weight(d, PANGO_WEIGHT_BOLD);
        pango_layout_set_font_description(l, d);
        pango_font_description_free(d);
        return l;
    };
    auto label = [&](const std::string& t, double x, double yy, double pt, double grey = 0.45, bool bold = false) {
        PangoLayout* l = ui_layout(t, pt, bold);
        if (on()) {
            cairo_set_source_rgb(cr, grey, grey, grey);
            cairo_move_to(cr, x, yy);
            pango_cairo_show_layout(cr, l);
        }
        int lw = 0, lh = 0;
        pango_layout_get_size(l, &lw, &lh);
        g_object_unref(l);
        return lh / static_cast<double>(PANGO_SCALE);
    };
    auto font_layout = [&](const std::string& t, double pt, int wrap_w, const char* features = nullptr) {
        PangoLayout* l = pango_layout_new(m_fonts->ctx);
        pango_layout_set_text(l, t.c_str(), -1);
        PangoFontDescription* d = pango_font_description_new();
        pango_font_description_set_family(d, m_plan.family.c_str());
        pango_font_description_set_size(d, static_cast<int>(pt * PANGO_SCALE));
        pango_layout_set_font_description(l, d);
        pango_font_description_free(d);
        std::string feat = m_kern ? "" : "kern 0";
        if (features) feat += (feat.empty() ? "" : ", ") + std::string(features);
        if (!feat.empty()) {
            PangoAttrList* attrs = pango_attr_list_new();
            pango_attr_list_insert(attrs, pango_attr_font_features_new(feat.c_str()));
            pango_layout_set_attributes(l, attrs);
            pango_attr_list_unref(attrs);
        }
        if (wrap_w > 0) {
            pango_layout_set_width(l, wrap_w * PANGO_SCALE);
            pango_layout_set_wrap(l, PANGO_WRAP_WORD);
        } else {
            pango_layout_set_width(l, static_cast<int>((width - 44) * PANGO_SCALE));
            pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
        }
        return l;
    };
    auto header = [&] {
        if (!on()) return;
        label(m_plan.family + " " + m_plan.style + "   " + m_plan.version, left, kMargin, 8, 0.25, true);
        PangoLayout* l = ui_layout("Granjon proof   " + date, 8);
        int lw = 0, lh = 0;
        pango_layout_get_pixel_size(l, &lw, &lh);
        cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
        cairo_move_to(cr, right - lw, kMargin);
        pango_cairo_show_layout(cr, l);
        g_object_unref(l);
        cairo_set_source_rgb(cr, 0.75, 0.75, 0.75);
        cairo_set_line_width(cr, 0.5);
        cairo_move_to(cr, left, kMargin + 15);
        cairo_line_to(cr, right, kMargin + 15);
        cairo_stroke(cr);
        PangoLayout* n = ui_layout(std::to_string(page + 1), 8);
        pango_layout_get_pixel_size(n, &lw, &lh);
        cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
        cairo_move_to(cr, (left + right) / 2 - lw / 2.0, H - kMargin);
        pango_cairo_show_layout(cr, n);
        g_object_unref(n);
    };
    auto new_page = [&] {
        ++page;
        y = top;
        header();
    };
    auto need = [&](double h) { if (y + h > bottom && y > top) new_page(); };
    auto section = [&](const std::string& t) {
        need(40);
        y += label(t, left, y, 10, 0.2, true) + 8;
    };
    // a line of proof text with its size in the gutter
    auto line = [&](const std::string& t, double pt, const char* features = nullptr, const std::string& side = "") {
        PangoLayout* l = font_layout(t, pt, 0, features);
        int lw = 0, lh = 0;
        pango_layout_get_size(l, &lw, &lh);
        const double hh = std::max(lh / static_cast<double>(PANGO_SCALE), pt * 1.2);
        need(hh);
        if (on()) {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_move_to(cr, left + 44, y);
            pango_cairo_show_layout(cr, l);
        }
        const double base = y + pango_layout_get_baseline(l) / static_cast<double>(PANGO_SCALE);
        label(side.empty() ? (pt == static_cast<int>(pt) ? std::to_string(static_cast<int>(pt)) : std::to_string(pt)) : side,
              left, base - 7, 6.5);
        g_object_unref(l);
        y += hh + 4;
    };

    header();

    // 1 waterfall + paragraphs
    section("Waterfall");
    for (double pt : m_plan.waterfall) line(m_plan.sample, pt);
    y += 10;
    section("Text");
    for (double pt : m_plan.text_sizes) {
        const int wrap = static_cast<int>(std::min(width - 44, pt * 36));
        PangoLayout* l = font_layout(m_plan.paragraph, pt, wrap);
        pango_layout_set_spacing(l, static_cast<int>(pt * 0.25 * PANGO_SCALE));
        int lw = 0, lh = 0;
        pango_layout_get_size(l, &lw, &lh);
        const double hh = lh / static_cast<double>(PANGO_SCALE);
        need(hh);
        if (on()) {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_move_to(cr, left + 44, y);
            pango_cairo_show_layout(cr, l);
        }
        label(std::to_string(static_cast<int>(pt)) + " pt", left, y + 1, 6.5);
        g_object_unref(l);
        y += hh + 14;
    }
    if (!m_plan.missing.empty()) {
        y += label("Not in the font (shown as boxes): " + m_plan.missing, left, y, 7) + 6;
    }

    // 2 spacing
    if (!m_plan.spacing.empty()) {
        new_page();
        section("Spacing");
        for (const auto& s : m_plan.spacing) line(s, 24);
    }
    // 3 kerning
    if (!m_plan.kerning.empty()) {
        new_page();
        section("Kerning -- " + std::to_string(m_plan.kern_pairs) + " pairs" + (m_kern ? "" : " (printed with kerning OFF)"));
        for (const auto& s : m_plan.kerning) line(s, 24);
    }
    // 4 features: off, then on
    if (!m_plan.features.empty()) {
        new_page();
        section("Features -- each line off, then on");
        for (const auto& f : m_plan.features) {
            const std::string on_feat = f.tag + " 1";
            const std::string off_feat = f.tag + " 0";
            line(f.text, 20, off_feat.c_str(), f.tag + " off");
            line(f.text, 20, on_feat.c_str(), f.tag + " on");
            y += 6;
        }
    }

    // 5 glyphs: outlines from the model, 8 to a row
    if (!m_plan.glyphs.empty()) {
        new_page();
        section("Glyphs -- " + std::to_string(m_plan.glyphs.size()));
        const auto& m = m_font.metrics();
        const double asc = std::max(m.ascender, m.cap_height), desc = std::min(m.descender, 0);
        const int cols = 8;
        const double cw = width / cols, art = 44, ch = art + 26;
        const double s = art / std::max(1.0, asc - desc);
        int c = 0;
        for (const auto& name : m_plan.glyphs) {
            if (c == 0) need(ch);
            const model::Glyph* g = m_font.glyph(name);
            const double x0 = left + c * cw, base = y + asc * s + 2;
            if (on() && g) {
                cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
                cairo_set_line_width(cr, 0.4);
                cairo_rectangle(cr, x0 + 2, y, cw - 4, ch - 4);
                cairo_stroke(cr);
                const double gx = x0 + (cw - g->advance * s) / 2;
                cairo_move_to(cr, gx, base);   // baseline tick
                cairo_line_to(cr, gx + g->advance * s, base);
                cairo_stroke(cr);
                cairo_new_path(cr);
                for (const auto& con : geom::all_contours(m_font, *g)) {
                    const int n = geom::segment_count(con);
                    if (n == 0) continue;
                    // a plain struct, not std::pair: GCC on aarch64 notes pair's C++17 ABI change (as Presets / Overlap)
                    struct XY { double x, y; };
                    const auto P = [&](geom::Vec2 p) { return XY{gx + p.x * s, base - p.y * s}; };
                    auto [mx, my] = P({con.nodes[0].x, con.nodes[0].y});
                    cairo_move_to(cr, mx, my);
                    for (int k = 0; k < n; ++k) {
                        const auto sg = geom::segment(con, k);
                        auto [ax, ay] = P(sg.p1);
                        auto [bx, by] = P(sg.p2);
                        auto [ex, ey] = P(sg.p3);
                        cairo_curve_to(cr, ax, ay, bx, by, ex, ey);
                    }
                    if (con.closed) cairo_close_path(cr);
                }
                cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
                cairo_set_source_rgb(cr, 0, 0, 0);
                cairo_fill(cr);
            }
            std::string cap = name;
            if (g && !g->unicodes.empty()) {
                char u[16];
                std::snprintf(u, sizeof u, "  %04X", static_cast<unsigned>(g->unicodes.front()));
                cap += u;
            }
            if (on()) {
                PangoLayout* l = ui_layout(cap, 5.5);
                pango_layout_set_width(l, static_cast<int>((cw - 8) * PANGO_SCALE));
                pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_MIDDLE);
                cairo_set_source_rgb(cr, 0.35, 0.35, 0.35);
                cairo_move_to(cr, x0 + 5, y + ch - 14);
                pango_cairo_show_layout(cr, l);
                g_object_unref(l);
            }
            if (++c == cols) { c = 0; y += ch; }
        }
        if (c) y += ch;
    }
    return page + 1;
}

}  // namespace granjon::views
