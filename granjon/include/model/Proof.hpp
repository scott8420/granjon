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
#include <set>
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
    int                      glyph_columns = 8;
    bool                     glyph_subset = false;   // the glyph pages show a selection, not the font
    std::string              missing;          // sample / paragraph characters the font lacks
    bool empty() const {
        return waterfall.empty() && text_sizes.empty() && spacing.empty() && kerning.empty() && features.empty() && glyphs.empty();
    }
};

// r9: what to put in (Preferences, PROOF). A page turned off is left out
// of the plan (its lists empty). `only`: the glyph pages show just these
// (the navigator's selection), in glyph order; nullptr or empty = every glyph.
struct ProofOptions {
    bool waterfall = true, text = true, spacing = true, kerning = true, features = true, glyphs = true;
    std::vector<double> sizes = {72, 48, 36, 24, 18, 14, 12, 10, 9, 8, 7, 6};   // the waterfall, points
    int  glyph_columns = 8;                                                     // 5 large, 8, 12 small
    const std::set<std::string>* only = nullptr;
};

// "72 48 36, 24 10.5" -> sizes, 4 .. 300 pt, repeats dropped, order kept.
// Nothing usable -> the default list.
std::vector<double> parse_proof_sizes(const std::string& text);

extern const char* const kProofParagraph;

ProofPlan proof_plan(const Font& f, const std::string& sample, const ProofOptions& opt = {});

}  // namespace granjon::model
