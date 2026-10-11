#pragma once
// core::Primer (M12) -- the Primer's notes: short illustrated pieces on drawing
// type (overshoot, stems and contrast, spacing, joins, optical centre, drawing
// order). The notes are DATA -- resources/primer/notes.txt and the figures
// beside it, compiled in (tools/embed.cpp) -- parsed here, GTK-free, so the
// selftest checks every note has a figure that exists and links the window
// knows how to follow. The Primer window renders them; the Shell follows the
// links.
#include <string>
#include <string_view>
#include <vector>

namespace granjon::core {

struct PrimerLink {
    std::string label;
    std::string target;   // "tool:measure:o", "report:overshoot", "spacing:nnonoo", ...
};

struct PrimerNote {
    std::string id, title, figure;
    std::vector<PrimerLink>  links;
    std::vector<std::string> paragraphs;   // plain text; *word* = bold
};

// Parse a notes file. Problems go to `errors` ("line 12: ..."); a bad line is
// skipped, the rest still parses.
std::vector<PrimerNote> parse_primer(std::string_view text, std::vector<std::string>* errors = nullptr);

// The bundled notes, in order.
const std::vector<PrimerNote>& primer_notes();

// The figure's SVG text (resources/primer/<name>), empty when there is none.
std::string_view primer_figure(const std::string& name);

// A link target the window can follow: its kind and argument, and whether the
// kind / argument is one Granjon knows (the selftest holds every bundled link
// to this).
struct LinkTarget {
    std::string kind;   // "tool", "glyph", "view", "report", "spacing", "font", "action"
    std::string arg;    // "measure:o", "n,o,H", "overview", "overshoot", the text, "metrics", the action
    bool valid = false;
};
LinkTarget parse_link_target(const std::string& target);

// "*word*" -> Pango markup ("<b>word</b>"), everything else escaped.
std::string primer_markup(const std::string& paragraph);

}  // namespace granjon::core
