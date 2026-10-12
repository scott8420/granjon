#pragma once
// otf::OtfParts -- the OTF compiler's inside, one builder per table. Internal
// to export/ and the selftest (which checks the pieces on their own).
#include "export/Otf.hpp"

#include <map>
#include <string>
#include <vector>

namespace granjon::otf {

// A glyph as it goes out: components flattened, rounded to units.
struct OutGlyph {
    std::string                 name;      // production name
    std::string                 source;    // designer name ("" for a supplied .notdef)
    int                         advance = 0;
    std::vector<model::Contour> contours;
    std::vector<char32_t>       unicodes;
    bool                        empty = true;
    int                         xmin = 0, ymin = 0, xmax = 0, ymax = 0;   // 0s when empty
    // M16b: the glyph's anchors -- its own, or (a composite with none) its
    // first component's, moved with it; a later component that is a mark
    // carrying "X" as well as "_X" lifts the composite's "X" onto itself
    // (so a mark typed after an accented letter stacks above the accent).
    std::vector<model::Anchor>  anchors;
};

// The font after the pre-export steps; every table builder reads only this.
struct Prepared {
    std::string family, style;          // as written (override applied)
    std::string ps_name;
    model::FontInfo info;
    model::Metrics  metrics;
    std::vector<OutGlyph>       glyphs;  // GID order, .notdef first
    std::map<std::string, int>  gid;     // designer name -> GID
    std::map<char32_t, int>     cmap;    // code point -> GID (first in glyph order wins)
    int xmin = 0, ymin = 0, xmax = 0, ymax = 0;   // font bbox
    std::int64_t time = 0;               // Unix seconds
};

Prepared prepare(const model::Font& f, const OtfOptions& opt, OtfReport& rep);
// One glyph through the same steps (flattened, overlaps removed (M10c),
// snapped, bbox, production name) -- the code view and Export SVG use it on
// their own. `fell_back`: overlap removal failed its check (outline as drawn).
OutGlyph prepare_glyph(const model::Font& f, const model::Glyph& g, bool remove_overlaps = true,
                       bool* changed = nullptr, bool* fell_back = nullptr);

// Type 2 charstring for one glyph (widths against the Private DICT's
// defaultWidthX / nominalWidthX).
Bytes charstring(const OutGlyph& g, int default_width, int nominal_width);

Bytes build_cff(const Prepared& p);
// loca_format: 0 short / 1 long offsets (TTF); 0 for CFF, which has no loca.
Bytes build_head(const Prepared& p, int loca_format = 0);
Bytes build_hhea(const Prepared& p);
Bytes build_maxp(const Prepared& p);
// max_context: OS/2 usMaxContext -- 2 with any pair / mark positioning, a
// ligature's length when longer (M16c).
Bytes build_os2(const Prepared& p, int max_context);
Bytes build_name(const Prepared& p);
Bytes build_cmap(const Prepared& p);
Bytes build_post(const Prepared& p);
Bytes build_hmtx(const Prepared& p);
// Empty when the font has no kerning that reaches two glyphs.
Bytes build_gpos(const model::Kerning& k, const Prepared& p, OtfReport& rep);
// M16c: the feature file's substitutions (model::parse_fea) -- empty when
// there are none, or when the file has an error (the report says which).
// `max_context` gets the longest ligature's length.
Bytes build_gsub(const std::string& fea, const Prepared& p, OtfReport& rep, int* max_context = nullptr);
// M16b: GDEF glyph classes (1 base, 3 mark) -- empty when the font has no marks.
Bytes build_gdef(const Prepared& p);
// M16b: a glyph's anchors after propagation (see OutGlyph::anchors).
std::vector<model::Anchor> glyph_anchors(const model::Font& f, const model::Glyph& g, int depth = 0);

// ── TrueType (M10) ──
// A glyph's TrueType outline: rounded quadratic contours, TrueType direction.
struct QPointI { int x = 0, y = 0; bool on = true; bool operator==(const QPointI&) const = default; };
using TtOutline = std::vector<std::vector<QPointI>>;
// Converts every glyph (tolerance in font units) and moves each glyph's bbox
// to its quadratic points' bbox -- what glyf, hmtx and head must agree on.
std::vector<TtOutline> quad_glyphs(Prepared& p, double tolerance, int* points = nullptr);
struct GlyfLoca { Bytes glyf, loca; int loca_format = 0; int max_points = 0, max_contours = 0; };
GlyfLoca build_glyf(const std::vector<TtOutline>& glyphs);
Bytes build_maxp_tt(const Prepared& p, const GlyfLoca& g);
Bytes build_post2(const Prepared& p);
Bytes build_gasp();

// Weight class and the RIBBI flags from the style name.
int  weight_class(const std::string& style);
bool is_bold(const std::string& style);
bool is_italic(const std::string& style);

}  // namespace granjon::otf
