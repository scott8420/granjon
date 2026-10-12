#pragma once
// model::Proof (s10, print proof) -- WHAT a proof shows, GTK-free so the
// selftest walks it; views::ProofPrinter sets it on paper (PDF or a printer)
// through the compiled font, so kerning, marks and features print as they ship.
//
// Pages, in order:
//   1  waterfall      the sample at 72 .. 6 pt, then the paragraph at 14 .. 8 pt
//   2  spacing        each drawn letter / figure between its controls
//                     (nn?nn, HH?HH, 00?00), then the font's spacing text
//   3  kerning        every kerned pair (value != 0, both sides typable) -- only when there is kerning
//   4  features       each feature's inputs, off then on -- only when the feature file compiles
//   5+ glyphs         every drawn glyph (not parts), outlines with name and code point
#include <string>
#include <vector>

namespace granjon::model {

class Font;

struct ProofPlan {
    std::string              family, style, version;
    std::string              sample;
    std::string              paragraph;
    std::vector<double>      waterfall = {72, 48, 36, 24, 18, 14, 12, 10, 9, 8, 7, 6};   // points
    std::vector<double>      text_sizes = {14, 12, 10, 8};
    std::vector<std::string> spacing;          // lines of text
    std::vector<std::string> kerning;          // lines of pairs, "AV To Ty ..."
    int                      kern_pairs = 0;
    struct Feat { std::string tag; std::string text; };
    std::vector<Feat>        features;
    std::vector<std::string> glyphs;           // names, glyph order
    std::string              missing;          // sample / paragraph characters the font lacks
};

extern const char* const kProofParagraph;

ProofPlan proof_plan(const Font& f, const std::string& sample);

}  // namespace granjon::model
