#pragma once
// geom::Shapes (M14a) -- the drawing kit's primitives, GTK-free: a rectangle,
// an ellipse and a line (a filled bar of a set stroke width), each one closed
// contour, counter-clockwise (an outer contour in the model's direction).
//
// The drag rules live here too, so the canvas and the selftest agree:
//   box   press -> pointer spans the box; Shift = square (the larger side);
//         Alt = from the centre (the press is the middle).
//   line  press -> pointer is the bar's centre line, butt ends; Shift = 45
//         degrees; Alt = from the centre.
#include "geom/Vec2.hpp"
#include "model/Outline.hpp"

namespace granjon::geom {

// The handle length of a quarter ellipse as a fraction of its radius -- the
// usual 4 (sqrt 2 - 1) / 3, a circle within 0.03 %.
constexpr double kEllipseKappa = 0.5522847498;

struct Box { Vec2 lo, hi; };   // lo.x <= hi.x, lo.y <= hi.y

Box  drag_box(Vec2 press, Vec2 now, bool square, bool from_centre);
// The line's two ends: `now` constrained (Shift) and mirrored (Alt).
void drag_line(Vec2 press, Vec2 now, bool constrain, bool from_centre, Vec2& a, Vec2& b);

// Four corner nodes from the bottom-left, counter-clockwise.
model::Contour shape_rect(const Box& b);
// Four smooth nodes at the extremes (bottom, right, top, left) --
// counter-clockwise, extreme points by construction.
model::Contour shape_ellipse(const Box& b);
// A bar from a to b, `width` wide, butt ends: four corners,
// counter-clockwise. Empty when a == b or width <= 0.
model::Contour shape_bar(Vec2 a, Vec2 b, double width);

// Too small to be a shape (a click, not a drag): under one unit across.
bool degenerate(const Box& b);

}  // namespace granjon::geom
