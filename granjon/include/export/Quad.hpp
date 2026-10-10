#pragma once
// otf::Quad -- cubic -> quadratic for TrueType (M10). A port of fontTools'
// cu2qu (Behdad Esfahbod et al., Apache 2.0): each cubic becomes the fewest
// quadratics (1..100) whose spline stays within `tolerance` of it, written
// as a TrueType spline -- consecutive off-curve points with the on-curve
// points between them IMPLIED (their midpoints), so n quadratics cost n
// off-curve points and no extra on-curves.
#include "model/Outline.hpp"

#include <vector>

namespace granjon::otf {

struct QPoint {
    double x = 0, y = 0;
    bool   on = true;
    bool operator==(const QPoint&) const = default;
};
using QContour = std::vector<QPoint>;   // closed; starts on-curve

// The off-curve points of the spline for one cubic p0 c1 c2 p3 (the end
// points are not included). Never empty for a true curve.
std::vector<QPoint> cubic_to_quad(QPoint p0, QPoint c1, QPoint c2, QPoint p3, double tolerance);

// A model contour as a TrueType contour, in TrueType's direction (the
// REVERSE of the model's / CFF's: outers clockwise). Not rounded.
QContour quad_contour(const model::Contour& c, double tolerance);

}  // namespace granjon::otf
