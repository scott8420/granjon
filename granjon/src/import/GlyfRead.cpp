// reader::GlyfRead -- the sfnt directory, TrueType glyf / loca, and the Smooth
// inference shared with CFF.
#include "geom/Edit.hpp"
#include "import/ReaderParts.hpp"

#include <algorithm>
#include <cmath>

namespace granjon::reader {

Sfnt read_sfnt(Span font) {
    Sfnt s;
    s.version = font.u32(0);
    if (s.version == 0x74746366) throw BadFont("a font collection (.ttc) -- open one of its fonts on its own");
    if (s.version == 0x774F4646 || s.version == 0x774F4632) throw BadFont("a compressed web font (WOFF / WOFF2) -- not read yet");
    if (s.version != 0x00010000 && s.version != 0x4F54544F && s.version != 0x74727565)
        throw BadFont("not an OpenType or TrueType font");
    const unsigned n = font.u16(4);
    for (unsigned i = 0; i < n; ++i) {
        const std::size_t r = 12 + 16 * static_cast<std::size_t>(i);
        std::string tag(reinterpret_cast<const char*>(font.sub(r, 4).p), 4);
        const std::uint32_t off = font.u32(r + 8), len = font.u32(r + 12);
        s.tables[tag] = font.sub(off, len);
    }
    return s;
}

namespace {

struct TP { double x, y; bool on; };

// One TrueType contour (quadratic B-spline, implied on-curves) -> cubic nodes,
// exactly. Then reversed into the model's direction (TrueType outers run
// clockwise; the model's and CFF's run counter-clockwise).
model::Contour tt_contour(std::vector<TP> pts) {
    model::Contour c;
    if (pts.empty()) return c;
    // Start on an on-curve point; if there is none, on the implied midpoint.
    auto first_on = std::find_if(pts.begin(), pts.end(), [](const TP& p) { return p.on; });
    if (first_on == pts.end()) {
        const TP a = pts[0], b = pts[1 % pts.size()];
        pts.insert(pts.begin() + 1, TP{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0, true});   // the implied on-curve, exactly
        first_on = pts.begin() + 1;
    }
    std::rotate(pts.begin(), first_on, pts.end());
    const std::size_t n = pts.size();
    c.nodes.push_back(model::Node::corner(pts[0].x, pts[0].y));
    std::vector<TP> off;
    auto flush_to = [&](double ex, double ey, bool to_start) {
        // From the last node through the queued off-curves to (ex, ey).
        for (std::size_t k = 0; k < off.size(); ++k) {
            model::Node& p0 = c.nodes.back();
            const TP& q = off[k];
            const bool last = k + 1 == off.size();
            const double tx = last ? ex : (q.x + off[k + 1].x) / 2.0, ty = last ? ey : (q.y + off[k + 1].y) / 2.0;
            p0.cx2 = p0.x + 2.0 / 3.0 * (q.x - p0.x);
            p0.cy2 = p0.y + 2.0 / 3.0 * (q.y - p0.y);
            const double c1x = tx + 2.0 / 3.0 * (q.x - tx), c1y = ty + 2.0 / 3.0 * (q.y - ty);
            if (last && to_start) {
                c.nodes.front().cx1 = c1x;
                c.nodes.front().cy1 = c1y;
            } else {
                model::Node m = model::Node::corner(tx, ty);
                m.cx1 = c1x; m.cy1 = c1y;
                if (!last) m.type = model::Node::Type::Smooth;   // an implied on-curve is smooth by construction
                c.nodes.push_back(m);
            }
        }
        if (off.empty() && !to_start) c.nodes.push_back(model::Node::corner(ex, ey));
        off.clear();
    };
    for (std::size_t i = 1; i < n; ++i) {
        if (!pts[i].on) { off.push_back(pts[i]); continue; }
        flush_to(pts[i].x, pts[i].y, false);
    }
    flush_to(pts[0].x, pts[0].y, true);   // the closing segment
    // A closing line onto a duplicate of the start point.
    if (c.nodes.size() > 1 && c.nodes.back().x == c.nodes.front().x && c.nodes.back().y == c.nodes.front().y &&
        !c.nodes.back().has_out()) {
        c.nodes.front().cx1 = c.nodes.back().cx1;
        c.nodes.front().cy1 = c.nodes.back().cy1;
        c.nodes.pop_back();
    }
    c.closed = true;
    geom::reverse(c);
    return c;
}

}  // namespace

std::vector<ReadGlyph> read_glyf(Span glyf, Span loca, int loca_format, int glyph_count, std::vector<std::string>& notes) {
    std::vector<ReadGlyph> out(static_cast<std::size_t>(glyph_count));
    int point_matched = 0;
    for (int g = 0; g < glyph_count; ++g) {
        const std::size_t a = loca_format == 0 ? 2u * loca.u16(2u * static_cast<std::size_t>(g)) : loca.u32(4u * static_cast<std::size_t>(g));
        const std::size_t b = loca_format == 0 ? 2u * loca.u16(2u * static_cast<std::size_t>(g + 1)) : loca.u32(4u * static_cast<std::size_t>(g + 1));
        if (b <= a) continue;   // empty
        const Span d = glyf.sub(a, b - a);
        const int nc = d.i16(0);
        ReadGlyph& rg = out[static_cast<std::size_t>(g)];
        if (nc >= 0) {
            std::vector<int> ends;
            for (int k = 0; k < nc; ++k) ends.push_back(static_cast<int>(d.u16(10 + 2 * static_cast<std::size_t>(k))));
            std::size_t i = 10 + 2 * static_cast<std::size_t>(nc);
            i += 2 + d.u16(i);   // instructions skipped
            const int np = ends.empty() ? 0 : ends.back() + 1;
            std::vector<std::uint8_t> fl;
            while (static_cast<int>(fl.size()) < np) {
                const std::uint8_t f = d.u8(i++);
                fl.push_back(f);
                if (f & 0x08) { const unsigned r = d.u8(i++); for (unsigned k = 0; k < r; ++k) fl.push_back(f); }
            }
            std::vector<int> xs(static_cast<std::size_t>(np)), ys(static_cast<std::size_t>(np));
            int v = 0;
            for (int k = 0; k < np; ++k) {
                const auto f = fl[static_cast<std::size_t>(k)];
                if (f & 0x02) { const int dd = d.u8(i++); v += (f & 0x10) ? dd : -dd; }
                else if (!(f & 0x10)) { v += d.i16(i); i += 2; }
                xs[static_cast<std::size_t>(k)] = v;
            }
            v = 0;
            for (int k = 0; k < np; ++k) {
                const auto f = fl[static_cast<std::size_t>(k)];
                if (f & 0x04) { const int dd = d.u8(i++); v += (f & 0x20) ? dd : -dd; }
                else if (!(f & 0x20)) { v += d.i16(i); i += 2; }
                ys[static_cast<std::size_t>(k)] = v;
            }
            int start = 0;
            for (int e : ends) {
                if (e < start || e >= np) throw BadFont("a glyph's contour ends out of order");
                std::vector<TP> pts;
                for (int k = start; k <= e; ++k)
                    pts.push_back({static_cast<double>(xs[static_cast<std::size_t>(k)]), static_cast<double>(ys[static_cast<std::size_t>(k)]), (fl[static_cast<std::size_t>(k)] & 1) != 0});
                start = e + 1;
                if (pts.size() < 2) continue;   // a lone point (often a hinting anchor): draws nothing
                model::Contour c = tt_contour(pts);
                if (c.nodes.size() >= 2) { infer_smooth(c); rg.contours.push_back(std::move(c)); }
            }
        } else {   // composite
            std::size_t i = 10;
            for (;;) {
                const unsigned flags = d.u16(i), gi = d.u16(i + 2);
                i += 4;
                double dx = 0, dy = 0;
                if (flags & 0x0001) { dx = d.i16(i); dy = d.i16(i + 2); i += 4; }
                else { dx = static_cast<std::int8_t>(d.u8(i)); dy = static_cast<std::int8_t>(d.u8(i + 1)); i += 2; }
                if (!(flags & 0x0002)) { ++point_matched; dx = dy = 0; }   // point-matched: no offset known
                double xx = 1, xy = 0, yx = 0, yy = 1;
                auto f2 = [&](std::size_t at) { return d.i16(at) / 16384.0; };
                if (flags & 0x0008) { xx = yy = f2(i); i += 2; }
                else if (flags & 0x0040) { xx = f2(i); yy = f2(i + 2); i += 4; }
                else if (flags & 0x0080) { xx = f2(i); xy = f2(i + 2); yx = f2(i + 4); yy = f2(i + 6); i += 8; }
                model::Component k;
                k.xx = xx; k.xy = xy; k.yx = yx; k.yy = yy; k.dx = dx; k.dy = dy;
                rg.components.push_back(k);
                rg.component_gids.push_back(static_cast<int>(gi));
                if (!(flags & 0x0020)) break;
            }
        }
    }
    if (point_matched)
        notes.push_back(std::to_string(point_matched) + " component(s) were placed by point matching; they sit at 0, 0 -- move them by hand.");
    return out;
}

void infer_smooth(model::Contour& c) {
    const std::size_t n = c.nodes.size();
    for (std::size_t i = 0; i < n; ++i) {
        model::Node& m = c.nodes[i];
        if (m.type != model::Node::Type::Corner) continue;
        const bool closed_or_inner = c.closed || (i > 0 && i + 1 < n);
        if (!closed_or_inner) continue;
        const model::Node& prev = c.nodes[(i + n - 1) % n];
        const model::Node& next = c.nodes[(i + 1) % n];
        // The direction out of the node on each side: its handle, else the straight segment.
        const double ix = m.has_in() ? m.cx1 - m.x : (prev.has_out() ? 0 : prev.x - m.x);
        const double iy = m.has_in() ? m.cy1 - m.y : (prev.has_out() ? 0 : prev.y - m.y);
        const double ox = m.has_out() ? m.cx2 - m.x : (next.has_in() ? 0 : next.x - m.x);
        const double oy = m.has_out() ? m.cy2 - m.y : (next.has_in() ? 0 : next.y - m.y);
        if (!m.has_in() && !m.has_out()) continue;   // two lines meet: a corner
        const double li = std::hypot(ix, iy), lo = std::hypot(ox, oy);
        if (li < 1e-9 || lo < 1e-9) continue;
        const double cross = (ix * oy - iy * ox) / (li * lo), dot = (ix * ox + iy * oy) / (li * lo);
        if (dot < 0 && std::abs(cross) < 0.02) m.type = model::Node::Type::Smooth;   // within about a degree
    }
}

}  // namespace granjon::reader
