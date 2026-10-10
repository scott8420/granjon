// otf::Codes -- M9c: drawing codes and SVG artwork.
#include "export/Codes.hpp"
#include "export/OtfParts.hpp"
#include "export/Quad.hpp"
#include "model/Spacing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace granjon::otf {

namespace {

std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.10g", std::abs(v) < 1e-9 ? 0.0 : v);
    return b;
}

std::string utf8(char32_t c) {
    std::string o;
    if (c < 0x80) o += static_cast<char>(c);
    else if (c < 0x800) { o += static_cast<char>(0xC0 | c >> 6); o += static_cast<char>(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += static_cast<char>(0xE0 | c >> 12); o += static_cast<char>(0x80 | (c >> 6 & 0x3F)); o += static_cast<char>(0x80 | (c & 0x3F)); }
    else { o += static_cast<char>(0xF0 | c >> 18); o += static_cast<char>(0x80 | (c >> 12 & 0x3F)); o += static_cast<char>(0x80 | (c >> 6 & 0x3F)); o += static_cast<char>(0x80 | (c & 0x3F)); }
    return o;
}

std::string xml_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

// The Type 2 charstring, read back: operands then operator, a batched
// operator's argument groups one per line (PostScript order, as ttx shows).
std::string disassemble(const Bytes& cs, int* ops_out) {
    std::ostringstream o;
    std::vector<int> st;
    bool first = true;
    int ops = 0;
    auto flush = [&](const char* name, std::size_t group) {
        if (group == 0 || st.empty()) {
            for (std::size_t i = 0; i < st.size(); ++i) o << (i ? " " : "") << st[i];
            o << (st.empty() ? "" : " ") << name << "\n";
        } else {
            for (std::size_t i = 0; i < st.size(); i += group) {
                for (std::size_t k = i; k < std::min(st.size(), i + group); ++k) o << (k > i ? " " : "") << st[k];
                o << (i + group >= st.size() ? std::string(" ") + name : std::string()) << "\n";
            }
        }
        st.clear();
        ++ops;
    };
    for (std::size_t i = 0; i < cs.size();) {
        const unsigned c = cs[i];
        if (c == 28) { st.push_back(static_cast<std::int16_t>(cs[i + 1] << 8 | cs[i + 2])); i += 3; continue; }
        if (c >= 32 && c <= 246) { st.push_back(static_cast<int>(c) - 139); ++i; continue; }
        if (c >= 247 && c <= 250) { st.push_back((static_cast<int>(c) - 247) * 256 + cs[i + 1] + 108); i += 2; continue; }
        if (c >= 251 && c <= 254) { st.push_back(-(static_cast<int>(c) - 251) * 256 - cs[i + 1] - 108); i += 2; continue; }
        ++i;
        if (first && (c == 21 || c == 14)) {
            if (st.size() > (c == 21 ? 2u : 0u)) {
                o << st.front() << " % the width (in the font: relative to nominalWidthX)\n";
                st.erase(st.begin());
            }
            first = false;
        }
        switch (c) {
            case 21: flush("rmoveto", 0); break;
            case 5:  flush("rlineto", 2); break;
            case 8:  flush("rrcurveto", 6); break;
            case 14: flush("endchar", 0); break;
            default: { char b[16]; std::snprintf(b, sizeof b, "op%u", c); flush(b, 0); }
        }
    }
    if (ops_out) *ops_out = ops;
    return o.str();
}

std::string path_data(const std::vector<model::Contour>& cs, double dx, double top) {
    std::ostringstream d;
    auto X = [&](double x) { return num(x + dx); };
    auto Y = [&](double y) { return num(top - y); };
    for (const auto& c : cs) {
        const auto& N = c.nodes;
        const std::size_t n = N.size();
        if (n < 2 && !(n == 1 && c.closed && (N[0].has_in() || N[0].has_out()))) continue;
        d << "M" << X(N[0].x) << " " << Y(N[0].y);
        const std::size_t segs = c.closed ? n : n - 1;
        for (std::size_t i = 0; i < segs; ++i) {
            const auto& a = N[i];
            const auto& b = N[(i + 1) % n];
            if (!a.has_out() && !b.has_in()) {
                if (c.closed && i == n - 1) break;
                d << "L" << X(b.x) << " " << Y(b.y);
            } else {
                d << "C" << X(a.cx2) << " " << Y(a.cy2) << " " << X(b.cx1) << " " << Y(b.cy1) << " " << X(b.x) << " "
                  << Y(b.y);
            }
        }
        d << "Z";
    }
    return d.str();
}

}  // namespace

GlyphCode glyph_code(const model::Font& f, const std::string& glyph, CodeFormat fmt) {
    GlyphCode out;
    const model::Glyph* g = f.glyph(glyph);
    if (!g) return out;
    const OutGlyph o = prepare_glyph(f, *g);
    const std::string head = o.name != glyph ? "glyph " + o.name + " (yours: " + glyph + ")" : "glyph " + o.name;

    if (fmt == CodeFormat::Cff) {
        const Bytes cs = charstring(o, -1, 0);   // width always written, against nominal 0
        int ops = 0;
        out.text = "% " + head + "\n% Type 2 charstring: operands, then the operator\n" + disassemble(cs, &ops);
        out.summary = std::to_string(cs.size()) + " bytes · " + std::to_string(ops) + " operators";
    } else if (fmt == CodeFormat::TrueType) {
        std::ostringstream t;
        int points = 0, offs = 0, contours = 0;
        t << "<!-- " << head << " -->\n";
        for (const auto& c : o.contours) {
            if (c.nodes.empty()) continue;
            const QContour q = quad_contour(c, f.info().units_per_em / 1000.0);
            if (q.size() < 2) continue;
            ++contours;
            t << "<contour>\n";
            for (const auto& p : q) {
                t << "  <pt x=\"" << std::lround(p.x) << "\" y=\"" << std::lround(p.y) << "\" on=\"" << (p.on ? 1 : 0) << "\"/>\n";
                ++points;
                if (!p.on) ++offs;
            }
            t << "</contour>\n";
        }
        t << "<!-- advance " << o.advance << " -->\n";
        out.text = t.str();
        out.summary = std::to_string(contours) + " contours · " + std::to_string(points) + " points (" +
                      std::to_string(offs) + " off-curve)";
    } else {
        const int top = std::max(f.metrics().ascender, o.ymax);
        const std::string d = path_data(o.contours, 0, top);
        out.text = "<!-- " + head + "; y down from y = " + std::to_string(top) + " -->\n<path d=\"" + d + "\"/>\n";
        out.summary = std::to_string(d.size()) + " characters of path data";
    }
    return out;
}

std::string text_svg(const model::Font& f, const std::vector<std::string>& glyphs, const SvgOptions& opt) {
    const auto placed = model::layout_strip(f, glyphs);
    const int upm = std::max(1, f.info().units_per_em);
    double top = f.metrics().ascender, bottom = f.metrics().descender, right = 0;
    std::vector<OutGlyph> outs;
    for (const auto& p : placed) {
        const model::Glyph* g = f.glyph(p.name);
        outs.push_back(g ? prepare_glyph(f, *g) : OutGlyph{});
        if (!outs.back().empty) {
            top = std::max(top, static_cast<double>(outs.back().ymax));
            bottom = std::min(bottom, static_cast<double>(outs.back().ymin));
        }
        right = std::max(right, p.x + p.advance);
    }
    const double m = opt.margin;
    const double w = right + 2 * m, h = top - bottom + 2 * m;
    const double scale = opt.em_px / upm;
    std::string text;
    for (const auto& p : placed) {
        const model::Glyph* g = f.glyph(p.name);
        text += g && !g->unicodes.empty() ? utf8(g->unicodes[0]) : "/" + p.name + " ";
    }

    std::ostringstream s;
    s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    s << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << num(std::round(w * scale * 100) / 100) << "px\" height=\""
      << num(std::round(h * scale * 100) / 100) << "px\" viewBox=\"0 0 " << num(w) << " " << num(h) << "\">\n";
    s << "  <title>" << xml_escape(f.info().family + " " + f.info().style) << "</title>\n";
    s << "  <desc>Outlines from Granjon: " << xml_escape(text) << " (font units, one em = " << upm
      << "; kerning applied)</desc>\n";
    s << "  <g fill=\"#000000\">\n";
    for (std::size_t i = 0; i < placed.size(); ++i) {
        const auto& o = outs[i];
        if (o.empty) continue;
        const std::string d = path_data(o.contours, placed[i].x + m, top + m);
        if (d.empty()) continue;
        s << "    <path id=\"g" << i + 1 << "-" << xml_escape(placed[i].name) << "\" d=\"" << d << "\"/>\n";
    }
    s << "  </g>\n</svg>\n";
    return s.str();
}

bool export_svg(const model::Font& f, const std::vector<std::string>& glyphs, const std::filesystem::path& to,
                const SvgOptions& opt, std::string* why) {
    const std::string svg = text_svg(f, glyphs, opt);
    return write_font_file(Bytes(svg.begin(), svg.end()), to, why);
}

}  // namespace granjon::otf
