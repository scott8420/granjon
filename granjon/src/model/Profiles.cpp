#include "model/Profiles.hpp"

#include "geom/Edit.hpp"
#include "model/Spacing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace granjon::model {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kDepth = 0.2;       // the clamp, as a share of the band height
constexpr double kDisagree = 0.5;    // contact under half the optical gap -> flag

// The font's sampling grid: every glyph on the same rows, so pairs line up.
void grid(const Font& f, double step, double& y0, std::size_t& n) {
    const auto& m = f.metrics();
    const double lo = std::min(m.descender, 0) - 2.0 * m.overshoot;
    const double hi = std::max({m.ascender, m.cap_height, m.x_height}) + 2.0 * m.overshoot;
    y0 = std::floor(lo / step) * step + step / 2;   // mid-rows: never exactly on a flat edge
    n = static_cast<std::size_t>(std::max(1.0, std::ceil((hi - y0) / step)));
}

double default_step(const Font& f) { return std::max(16, f.info().units_per_em) / 100.0; }

// The optical part of a reading (no control pair -- that would recurse).
void optical(const Font& f, const EdgeProfile& L, const EdgeProfile& R, double offset, PairReading& r) {
    const auto& m = f.metrics();
    auto top = [](const EdgeProfile& p) {
        double t = -1e9;
        for (std::size_t i = 0; i < p.left.size(); ++i) if (p.ink(i)) t = std::max(t, p.y(i));
        return t;
    };
    const double mt = std::min(top(L), top(R));
    r.band = mt <= m.x_height + 2.0 * m.overshoot + 2.0 * L.step ? 'x' : 'H';
    r.band_lo = 0;
    r.band_hi = r.band == 'x' ? m.x_height : m.cap_height;
    r.depth = kDepth * (r.band_hi - r.band_lo);
    double lext = -1e18, rext = 1e18;
    std::vector<std::size_t> rows;
    for (std::size_t i = 0; i < L.left.size(); ++i) {
        if (L.y(i) < r.band_lo || L.y(i) > r.band_hi) continue;
        rows.push_back(i);
        if (L.ink(i)) lext = std::max(lext, L.right[i]);
        if (R.ink(i)) rext = std::min(rext, R.left[i]);
    }
    r.ys.clear(); r.lx.clear(); r.rx.clear();
    if (rows.empty() || lext < -1e17 || rext > 1e17) { r.optical = r.contact; return; }   // no ink in the band
    double sum = 0;
    for (std::size_t i : rows) {
        const double le = L.ink(i) ? std::max(L.right[i], lext - r.depth) : lext - r.depth;
        const double re = R.ink(i) ? std::min(R.left[i], rext + r.depth) : rext + r.depth;
        r.ys.push_back(L.y(i));
        r.lx.push_back(le);
        r.rx.push_back(offset + re);
        sum += offset + re - le;
    }
    r.optical = sum / static_cast<double>(rows.size());
}

PairReading read(const Font& f, const Glyph& a, const Glyph& b, int kern, bool with_control);

}  // namespace

bool EdgeProfile::ink(std::size_t i) const { return i < left.size() && !std::isnan(left[i]); }

EdgeProfile edge_profile(const Font& f, const Glyph& g, double step) {
    EdgeProfile p;
    p.step = step > 0 ? step : default_step(f);
    std::size_t n = 0;
    grid(f, p.step, p.y0, n);
    p.left.assign(n, kNaN);
    p.right.assign(n, kNaN);
    const auto cs = geom::all_contours(f, g);
    if (cs.empty()) return p;
    for (std::size_t i = 0; i < n; ++i) {
        const auto xs = geom::crossings(cs, true, p.y(i));
        if (xs.empty()) continue;
        p.left[i] = xs.front();
        p.right[i] = xs.back();
    }
    return p;
}

namespace {

PairReading read(const Font& f, const Glyph& a, const Glyph& b, int kern, bool with_control) {
    PairReading r;
    r.kern = kern;
    r.offset = a.advance + kern;
    const EdgeProfile L = edge_profile(f, a), R = edge_profile(f, b);
    // Contact: a collision (the profiles cross on some row) reads as the
    // most negative row gap; otherwise the closest approach between the
    // left glyph's right edge and the right glyph's left edge, in 2D --
    // the tangent, which sees T's bar reach over the o.
    double worst = 1e18;
    std::size_t wi = 0;
    for (std::size_t i = 0; i < L.left.size(); ++i)
        if (L.ink(i) && R.ink(i)) {
            const double g = r.offset + R.left[i] - L.right[i];
            if (g < worst) { worst = g; wi = i; }
        }
    bool any_l = false, any_r = false;
    for (std::size_t i = 0; i < L.left.size(); ++i) { any_l = any_l || L.ink(i); any_r = any_r || R.ink(i); }
    if (!any_l || !any_r) return r;
    r.ok = true;
    if (worst < 0) {
        r.contact = worst;
        r.contact_lx = L.right[wi];
        r.contact_rx = r.offset + R.left[wi];
        r.contact_ly = r.contact_ry = L.y(wi);
    } else {
        double best = 1e36;
        std::vector<std::pair<std::size_t, std::size_t>> ties;   // straight stems tie on every row
        for (std::size_t i = 0; i < L.left.size(); ++i) {
            if (!L.ink(i)) continue;
            for (std::size_t j = 0; j < R.left.size(); ++j) {
                if (!R.ink(j)) continue;
                const double dx = r.offset + R.left[j] - L.right[i], dy = R.y(j) - L.y(i);
                const double d = std::sqrt(dx * dx + dy * dy);
                if (d < best - 0.25) { best = d; ties.assign(1, {i, j}); }
                else if (d <= best + 0.25) ties.emplace_back(i, j);
            }
        }
        // The middle of the tied run: the line sits mid-height on two stems.
        const auto [i, j] = ties[ties.size() / 2];
        r.contact_lx = L.right[i]; r.contact_ly = L.y(i);
        r.contact_rx = r.offset + R.left[j]; r.contact_ry = R.y(j);
        r.contact = best;
    }
    optical(f, L, R, r.offset, r);
    r.disagree = r.optical > 0 && r.contact < kDisagree * r.optical;
    if (!with_control) return r;
    const Glyph* c = f.glyph_for(r.band == 'x' ? U'n' : U'H');
    if (!c) return r;
    r.control = c->name + " " + c->name;
    const PairReading cr = read(f, *c, *c, kern_lookup(f.kerning(), c->name, c->name).value, false);
    if (!cr.ok) return r;
    r.control_optical = cr.optical;
    r.proposed = kern + static_cast<int>(std::lround(cr.optical - r.optical));
    return r;
}

}  // namespace

PairReading read_pair_at(const Font& f, const std::string& left, const std::string& right, int kern) {
    const Glyph* a = f.glyph(left);
    const Glyph* b = f.glyph(right);
    if (!a || !b) return {};
    return read(f, *a, *b, kern, true);
}

PairReading read_pair(const Font& f, const std::string& left, const std::string& right) {
    return read_pair_at(f, left, right, kern_lookup(f.kerning(), left, right).value);
}

}  // namespace granjon::model
