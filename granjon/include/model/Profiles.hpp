#pragma once
// model::Profiles (M8) -- edge profiles and the two pair readouts, GTK-free.
//
//   edge_profile   a glyph's leftmost / rightmost ink at sampled heights
//   read_pair      for (left, right) as set with their kern:
//                  contact gap -- the closest approach, with its two points
//                  optical gap -- the mean whitespace between the profiles in
//                                 the pair's band (x-height or cap height),
//                                 deep overhangs clamped
//                  control     -- the optical gap of the band's control pair
//                                 (n n for the x band, H H for the cap band)
//                  proposed    -- the kern that gives the pair the control's
//                                 optical gap (kern-to-gap)
//                  disagree    -- the two readouts split sharply (To, LT):
//                                 contact alone would misjudge the pair
#include "model/Font.hpp"

#include <optional>
#include <string>
#include <vector>

namespace granjon::model {

struct EdgeProfile {
    double y0 = 0, step = 10;              // sample i is at y0 + i * step
    std::vector<double> left, right;       // NaN where the glyph has no ink
    bool ink(std::size_t i) const;
    double y(std::size_t i) const { return y0 + static_cast<double>(i) * step; }
};
// Sampled on the font's grid (descender to ascender / cap height, plus two
// overshoots, rows mid-step so a flat edge never sits on one), every `step`
// units (0 = UPM / 100). Every glyph shares the grid, so pairs line up.
// Cheap (a hundred rows); computed on demand -- a cache comes when readouts
// go font-wide.
EdgeProfile edge_profile(const Font& f, const Glyph& g, double step = 0);

struct PairReading {
    bool   ok = false;                     // both glyphs have ink side by side somewhere
    int    kern = 0;                       // the kern in effect, as read
    double offset = 0;                     // the right glyph's origin: left advance + kern
    // contact
    double contact = 0;                    // the closest approach, font units (negative: they collide)
    double contact_lx = 0, contact_ly = 0; // the touching point on the left glyph (its frame)
    double contact_rx = 0, contact_ry = 0; // ... and on the right glyph (the left glyph's frame)
    // optical
    char   band = 'x';                     // 'x' (0..x-height) or 'H' (0..cap height)
    double band_lo = 0, band_hi = 0;
    double depth = 0;                      // the clamp: no deeper than this from the side's extreme in the band
    double optical = 0;                    // mean gap over the band
    std::vector<double> ys, lx, rx;        // the clamped edges sampled in the band (for drawing)
    // kern-to-gap
    std::string control;                   // "n n" / "H H" ("" when the font lacks them)
    double control_optical = 0;
    std::optional<int> proposed;           // the kern for the control's optical gap
    bool   disagree = false;
};
PairReading read_pair(const Font& f, const std::string& left, const std::string& right);
// The same with an explicit kern (the control pairs, what-ifs).
PairReading read_pair_at(const Font& f, const std::string& left, const std::string& right, int kern);

}  // namespace granjon::model
