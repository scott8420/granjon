#pragma once
#include <string>
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

// The pick as text, for the undo journal's detail channel (s09: undo restores
// the pick). "glyph\n" then "contour index part;" per point. Parsing returns
// the glyph name and the points; junk lines are skipped.
std::string pick_to_text(const std::string& glyph, const Points& pts);
Points      pick_from_text(const std::string& text, std::string* glyph);

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

// ── transform (M15a) ───────────────────────────────────────────────────────
// An affine map in UFO's component order: x' = xx*x + yx*y + dx,
// y' = xy*x + yy*y + dy.
struct Affine {
    double xx = 1, xy = 0, yx = 0, yy = 1, dx = 0, dy = 0;
    Vec2   apply(Vec2 p) const { return {xx * p.x + yx * p.y + dx, xy * p.x + yy * p.y + dy}; }
    double det() const { return xx * yy - xy * yx; }
    bool   identity() const { return xx == 1 && xy == 0 && yx == 0 && yy == 1 && dx == 0 && dy == 0; }
};
Affine compose(const Affine& outer, const Affine& inner);   // outer after inner
// Scale (sx, sy as factors), then skew (degrees: horizontal slants right as y
// rises, vertical tilts up as x grows), then rotate (degrees, counter-
// clockwise), all about `origin`.
Affine make_transform(double sx, double sy, double rotate_deg, double skew_x_deg, double skew_y_deg, Vec2 origin);
// Apply m to the picked part of `at_press`, writing into g: picked nodes carry
// both handles; a picked handle alone moves alone; picked anchors move;
// picked components get m composed onto their own matrix. An EMPTY pick =
// the whole glyph (contours, anchors, components). A whole contour under a
// mirroring map (det < 0) is reversed so it keeps running the right way.
void transform_points(model::Glyph& g, const model::Glyph& at_press, const Points& pts, const Affine& m);
// Make node `index` the contour's start (M5e): a rotation, so every node
// keeps its handles and the travel direction holds. Closed contours only (an
// open one starts at its loose end); false when there is nothing to do.
bool set_start(model::Contour& c, int index);
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
// The Select tool (s08, the solid arrow): whole paths. Every node or handle
// ref grows to all the nodes of its contour; anchors and components stay.
Points whole_contours(const model::Glyph& g, const Points& pts);
// The contour whose own inside holds pt -- the smallest by area when several
// do (a counter inside its bowl), or -1. Closed contours only.
int contour_at(const model::Glyph& g, Vec2 pt);

}  // namespace granjon::geom
