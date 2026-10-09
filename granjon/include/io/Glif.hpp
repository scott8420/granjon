#pragma once
// io::Glif -- one glyph <-> one .glif file (GLIF format 2, UFO 3).
//
// The model's Node is an anchor with in/out handles (Curvz); GLIF is a list
// of points. The mapping, both ways:
//   * a closed contour is written starting at node 0 (an on-curve point);
//     the closing segment's off-curves come last, so start points survive;
//   * a segment with a handle on either end is two off-curves + "curve";
//     one with neither is "line"; an open contour starts with "move";
//   * smooth="yes" for Smooth and Symmetric nodes. Symmetric / Cusp (Curvz
//     editing constraints GLIF has no word for) ride in the glyph lib as
//     granjon.nodeTypes, written only when a contour has one;
//   * reading accepts "qcurve" (TrueType-flavoured UFOs): quadratics are
//     promoted to cubic exactly, implied on-curves restored at midpoints.
// Status and note: <note> is GLIF's own; status is granjon.status in the lib.
#include "model/Glyph.hpp"

#include <optional>
#include <string>

namespace granjon::io {

std::string write_glif(const model::Glyph& g);
// `order` is not in a .glif (it is lib.plist's public.glyphOrder); the reader
// leaves it 0.
std::optional<model::Glyph> read_glif(const std::string& xml, std::string* err = nullptr);

}  // namespace granjon::io
