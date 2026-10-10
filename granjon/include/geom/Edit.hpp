#pragma once
// geom::Edit -- Curvz's BezierPath editing rules, lifted GTK-free and run
// straight on model::Contour (the model already IS Curvz's node shape, so no
// conversion layer): segments, node / handle moves under the node-type
// constraints, and hit-testing a glyph's own drawing.
//
// Node types (Curvz's semantics, kept verbatim):
//   Corner     handles free
//   Smooth     dragging one handle keeps the other collinear, its length kept
//   Symmetric  dragging one handle mirrors the other
//   Cusp       handles locked to the arriving tangent; a handle drag changes
//              length only, and moving neighbours re-aims them
//
// All coordinates are font units, y-up. Tolerances come in font units too --
// the canvas divides its pixel tolerance by its scale.
#include "geom/CubicSegment.hpp"
#include "model/Font.hpp"
#include "model/Glyph.hpp"

#include <compare>
#include <optional>
#include <set>
#include <vector>

namespace granjon::geom {

int          segment_count(const model::Contour& c);
CubicSegment segment(const model::Contour& c, int i);   // i wraps on a closed contour

void move_node(model::Contour& c, int idx, Vec2 pos);   // handles ride along; cusps re-aim
void move_handle_in(model::Contour& c, int idx, Vec2 pos);
void move_handle_out(model::Contour& c, int idx, Vec2 pos);
void recompute_cusp_handles(model::Contour& c, int idx);
// Change a node's type and make its handles honour it (Curvz
// BezierPath::set_node_type): Corner leaves them; Cusp locks to the arriving
// tangent; Symmetric mirrors (seeding from the neighbours when both are
// absent; from Corner it keeps what is there); Smooth aligns (Cusp -> Smooth)
// or seeds handles along the neighbour chord (Corner -> Smooth).
void set_node_type(model::Contour& c, int idx, model::Node::Type type);

// ── a point the designer can pick ──────────────────────────────────────────
// A node's anchor, one of its handles, one of the glyph's anchors, or one of
// its components (for the last two `contour` is -1 and `index` is the
// anchor's / component's index; a component's "position" is its offset).
struct PointRef {
    enum class Part { Node, In, Out, Anchor, Component };
    int  contour = -1;
    int  index   = -1;
    Part part    = Part::Node;
    auto operator<=>(const PointRef&) const = default;
    bool operator==(const PointRef&) const  = default;
};
using Points = std::set<PointRef>;

Vec2 position(const model::Glyph& g, const PointRef& p);   // {0,0} if stale
bool valid(const model::Glyph& g, const PointRef& p);

// Move the picked points by d, starting from `at_press` (so a live drag is
// always "press + d", never cumulative). Picked nodes carry their handles; a
// picked handle whose node is not picked moves under its node's type rule;
// cusps next to a moved node re-aim. Stale refs are skipped.
// `free_handles` (Alt on a handle): a dragged handle moves alone -- its node
// becomes a Corner, because its handles are no longer in line.
void move_points(model::Glyph& g, const model::Glyph& at_press, const Points& pts, Vec2 d,
                 bool free_handles = false);

// Shift everything that draws (contours, anchors, component offsets) by dx.
void shift_x(model::Glyph& g, double dx);

// ── components ─────────────────────────────────────────────────────────────
// Apply a component's affine (UFO order) to a contour, in place.
void transform(model::Contour& c, const model::Component& k);
// A component's drawing as plain contours, nested components resolved (depth
// capped at 8 -- a cycle draws nothing more, it never hangs).
std::vector<model::Contour> component_contours(const model::Font& f, const model::Component& k, int depth = 0);
// The topmost component whose filled drawing contains pt (nonzero winding),
// or -1.
int component_at(const model::Font& f, const model::Glyph& g, Vec2 pt);
// Nonzero-winding inside test against contours (cubics flattened).
bool inside(const std::vector<model::Contour>& cs, Vec2 pt);

// ── precision (M5b) ────────────────────────────────────────────────────────
// The grid step for a zoom (px per font unit): the smallest of 0.1, 0.2,
// 0.5, 1, 2, 5, 10, 20, 50, 100 ... whose lines are at least `min_px` apart.
double grid_step(double px_per_unit, double min_px = 8.0);
// Snap one axis: the nearest target within tol, else the nearest grid line
// (grid > 0) within tol, else v. `snapped` (when given) gets what it snapped
// to, or nullopt.
double snap_axis(double v, const std::vector<double>& targets, double grid, double tol,
                 std::optional<double>* snapped = nullptr);
// Shift's axis lock: keep the larger component of d, zero the other.
Vec2 axis_lock(Vec2 d);

// ── guides + measuring (M5c) ───────────────────────────────────────────────
// Distance from pt to the guide's line.
double guide_distance(const model::Guide& g, Vec2 pt);
// Everything the glyph draws as plain contours (its own + components').
std::vector<model::Contour> all_contours(const model::Font& f, const model::Glyph& g);
// Where the outline crosses the line y = at (horizontal) or x = at, sorted.
std::vector<double> crossings(const std::vector<model::Contour>& cs, bool horizontal, double at);
// The stem through pt along one axis: the nearest crossing on each side.
// nullopt when pt is not between two crossings.
std::optional<std::pair<double, double>> stem_at(const std::vector<model::Contour>& cs, Vec2 pt, bool horizontal);

// ── direction (M5d) ────────────────────────────────────────────────────────
// Signed area of the contour's flattened outline (curves sampled, not just
// the anchors). Font units are y-up, so > 0 = counter-clockwise.
double signed_area(const model::Contour& c);
// Reverse the travel direction (Curvz BezierPath::reverse: handles swap, the
// node order flips) KEEPING the start node where it is.
void reverse(model::Contour& c);
// How many of the other contours enclose this one (even = an outer contour,
// odd = a counter).
int depth(const std::vector<model::Contour>& cs, int index);
// The contours running the wrong way for CFF / UFO: outers must be
// counter-clockwise, counters clockwise.
std::vector<int> wrong_way(const std::vector<model::Contour>& cs);

// ── hit testing ────────────────────────────────────────────────────────────
struct Hit {
    enum class Kind { None, Node, HandleIn, HandleOut, Anchor, Segment };
    Kind   kind     = Kind::None;
    int    contour  = -1;
    int    index    = -1;    // node / anchor index; the segment's index for Segment
    double t        = 0.0;   // on Segment
    double distance = 1e18;
};
// Nearest within tol: nodes first, then (when show_handles) handles, then
// anchors, then segments -- Curvz's priority.
Hit hit_test(const model::Glyph& g, Vec2 pt, double tol, bool show_handles = true);

// Picked points inside the box (anchors of nodes, and glyph anchors).
Points points_in(const model::Glyph& g, Vec2 a, Vec2 b);

}  // namespace granjon::geom
