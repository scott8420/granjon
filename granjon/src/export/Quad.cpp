// otf::Quad -- cu2qu, ported (fontTools.cu2qu.cu2qu: cubic_approx_spline and
// friends; Apache 2.0). Complex numbers stand for points, as in the original.
#include "export/Quad.hpp"
#include "geom/Edit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <optional>

namespace granjon::otf {

namespace {

using C = std::complex<double>;
constexpr int kMaxN = 100;

double dot(C a, C b) { return a.real() * b.real() + a.imag() * b.imag(); }

// The cubic split into n equal-t pieces.
std::vector<std::array<C, 4>> split_n(C p0, C p1, C p2, C p3, int n) {
    // Power basis: P(t) = a t^3 + b t^2 + c t + d.
    const C c = (p1 - p0) * 3.0;
    const C b = (p2 - p1) * 3.0 - c;
    const C d = p0;
    const C a = p3 - d - c - b;
    std::vector<std::array<C, 4>> out;
    const double dt = 1.0 / n, dt2 = dt * dt, dt3 = dt2 * dt;
    for (int i = 0; i < n; ++i) {
        const double t1 = i * dt, t1_2 = t1 * t1;
        // The piece's own power basis, then back to Bezier points.
        const C a1 = a * dt3;
        const C b1 = (3.0 * a * t1 + b) * dt2;
        const C c1 = (2.0 * b * t1 + c + 3.0 * a * t1_2) * dt;
        const C d1 = a * t1 * t1_2 + b * t1_2 + c * t1 + d;
        const C q0 = d1;
        const C q1 = c1 / 3.0 + d1;
        const C q2 = (b1 + c1) / 3.0 + q1;
        const C q3 = a1 + d1 + c1 + b1;
        out.push_back({q0, q1, q2, q3});
    }
    return out;
}

C approx_control(double t, C p0, C p1, C p2, C p3) {
    const C a = p0 + (p1 - p0) * 1.5;
    const C b = p3 + (p2 - p3) * 1.5;
    return a + (b - a) * t;
}

std::optional<C> intersect(C a, C b, C c, C d) {
    const C ab = b - a, cd = d - c;
    const C p = ab * C(0, 1);
    const double den = dot(p, cd);
    if (den == 0) {
        if (b == c && (a == b || c == d)) return b;
        return std::nullopt;
    }
    return c + cd * (dot(p, a - c) / den);
}

bool fit_inside(C p0, C p1, C p2, C p3, double tol, int depth = 0) {
    if (std::abs(p2) <= tol && std::abs(p1) <= tol) return true;
    const C mid = (p0 + 3.0 * (p1 + p2) + p3) * 0.125;
    if (std::abs(mid) > tol) return false;
    if (depth > 30) return true;   // (the original recurses until it decides; guard against pathologies)
    const C d3 = (p3 + p2 - p1 - p0) * 0.125;
    return fit_inside(p0, (p0 + p1) * 0.5, mid - d3, mid, tol, depth + 1) &&
           fit_inside(mid, mid + d3, (p2 + p3) * 0.5, p3, tol, depth + 1);
}

// The off-curves q1..qn of a spline of n quadratics, or nullopt.
std::optional<std::vector<C>> approx_spline(const std::array<C, 4>& cu, int n, double tol) {
    if (n == 1) {
        const auto q1 = intersect(cu[0], cu[1], cu[2], cu[3]);
        if (!q1) return std::nullopt;
        const C c1 = cu[0] + (*q1 - cu[0]) * (2.0 / 3.0);
        const C c2 = cu[3] + (*q1 - cu[3]) * (2.0 / 3.0);
        if (!fit_inside(0, c1 - cu[1], c2 - cu[2], 0, tol)) return std::nullopt;
        return std::vector<C>{*q1};
    }
    const auto pieces = split_n(cu[0], cu[1], cu[2], cu[3], n);
    std::vector<C> offs;
    C next_q1 = approx_control(0, pieces[0][0], pieces[0][1], pieces[0][2], pieces[0][3]);
    C q2 = cu[0];
    C d1 = 0;
    offs.push_back(next_q1);
    for (int i = 1; i <= n; ++i) {
        const auto& [c0, c1, c2, c3] = pieces[static_cast<std::size_t>(i - 1)];
        const C q0 = q2;
        const C q1 = next_q1;
        if (i < n) {
            const auto& nx = pieces[static_cast<std::size_t>(i)];
            next_q1 = approx_control(static_cast<double>(i) / (n - 1), nx[0], nx[1], nx[2], nx[3]);
            offs.push_back(next_q1);
            q2 = (q1 + next_q1) * 0.5;
        } else {
            q2 = c3;
        }
        const C d0 = d1;
        d1 = q2 - c3;
        if (std::abs(d1) > tol ||
            !fit_inside(d0, q0 + (q1 - q0) * (2.0 / 3.0) - c1, q2 + (q1 - q2) * (2.0 / 3.0) - c2, d1, tol))
            return std::nullopt;
    }
    return offs;
}

}  // namespace

std::vector<QPoint> cubic_to_quad(QPoint p0, QPoint c1, QPoint c2, QPoint p3, double tolerance) {
    const std::array<C, 4> cu{C(p0.x, p0.y), C(c1.x, c1.y), C(c2.x, c2.y), C(p3.x, p3.y)};
    for (int n = 1; n <= kMaxN; ++n)
        if (auto offs = approx_spline(cu, n, tolerance)) {
            std::vector<QPoint> out;
            for (const C& q : *offs) out.push_back({q.real(), q.imag(), false});
            return out;
        }
    // Nothing fits (degenerate input): the cubic's own handles' midpoint.
    return {{(c1.x + c2.x) / 2, (c1.y + c2.y) / 2, false}};
}

QContour quad_contour(const model::Contour& src, double tolerance) {
    model::Contour c = src;
    if (c.closed) {
        geom::reverse(c);   // start node kept
    } else {                // an open contour: from its other end
        std::reverse(c.nodes.begin(), c.nodes.end());
        for (auto& n : c.nodes) { std::swap(n.cx1, n.cx2); std::swap(n.cy1, n.cy2); }
    }
    QContour out;
    const auto& N = c.nodes;
    const std::size_t n = N.size();
    if (n == 0) return out;
    const std::size_t segs = c.closed ? n : n - 1;
    out.push_back({N[0].x, N[0].y, true});
    for (std::size_t i = 0; i < segs; ++i) {
        const auto& a = N[i];
        const auto& b = N[(i + 1) % n];
        const bool last = i + 1 == n;   // back to the start
        if (a.has_out() || b.has_in()) {
            for (const auto& q : cubic_to_quad({a.x, a.y}, {a.cx2, a.cy2}, {b.cx1, b.cy1}, {b.x, b.y}, tolerance))
                out.push_back(q);
        }
        if (!last) out.push_back({b.x, b.y, true});
    }
    return out;
}

}  // namespace granjon::otf
