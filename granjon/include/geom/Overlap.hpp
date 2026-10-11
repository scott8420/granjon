#pragma once
// geom::Overlap (M10c; booleans M14b) -- remove overlaps: the union of a glyph's closed
// contours (nonzero fill, as fonts render), rebuilt from the ORIGINAL cubics.
//
// Clipper2 (Angus Johnson; built with its Z channel) does the topology on a
// flattening of the contours -- Curvz's adaptive de Casteljau at 0.1 unit.
// Every flattened point is tagged with the contour, segment and t it came
// from; Clipper2's Z callback tags every crossing with the two edges that
// made it. The result is then rebuilt EXACTLY (the approach of fontmake's
// booleanOperations): each run of the outline is the original segment cut at
// its ends (de Casteljau), original nodes keep their place, handles and type,
// crossings are solved on both curves (Newton) and become corners.
//
//   * no overlaps (no crossing, every contour whole) -> the input, untouched
//   * open contours pass through untouched
//   * a safety net: if the rebuilt area strays from Clipper2's polygon area
//     (more than 0.2 % + 2 units), the input comes back and `fallback` is set
#include "model/Outline.hpp"

#include <string>
#include <vector>

namespace granjon::geom {

struct OverlapResult {
    std::vector<model::Contour> contours;
    bool changed = false;        // the outline is not the input
    bool fallback = false;       // the rebuild failed its check: input returned
    int  crossings = 0;
    std::string why;             // set with `fallback`
};

OverlapResult remove_overlaps(const std::vector<model::Contour>& in);

// M14b: the booleans on the same engine and rebuild. `subject` and `clip`
// each fill nonzero; Union = either, Subtract = subject minus clip,
// Intersect = both. The result is the rebuilt loops (curves kept, crossings
// corners), outers counter-clockwise and holes clockwise; open contours in
// either list come back as they were. `changed` with no closed contours =
// nothing left (an intersection of shapes that don't meet). On `fallback` the input (subject then
// clip) comes back with the reason, as remove_overlaps.
enum class BoolOp { Union, Subtract, Intersect };
OverlapResult boolean_op(const std::vector<model::Contour>& subject, const std::vector<model::Contour>& clip, BoolOp op);

}  // namespace granjon::geom
