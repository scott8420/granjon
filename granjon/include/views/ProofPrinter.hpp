#pragma once
// views::ProofPrinter (s10, print proof) -- sets a model::ProofPlan on pages:
// to a PDF file, or to a printer through GtkPrintOperation (the same drawing).
//
// Text is set through the COMPILED font (otf::compile_otf, the bytes Export
// writes) in a private Pango font map, as the Preview does, so what prints is
// the shipping font: kerning, marks, the feature file. The glyph pages draw the
// outlines from the model (every drawn glyph, unencoded ones too). Labels use
// the desktop's UI font. Pages: Letter or A4 (Settings proof_paper), 0.65 inch
// margins, a header (family, style, version, date) and a page number.
#include "model/Proof.hpp"

#include <cairo.h>

#include <memory>
#include <string>

namespace granjon::model { class Font; }

namespace granjon::views {

class ProofPrinter {
public:
    ProofPrinter(const model::Font& f, model::ProofPlan plan, bool kern = true);
    ~ProofPrinter();
    ProofPrinter(const ProofPrinter&) = delete;
    ProofPrinter& operator=(const ProofPrinter&) = delete;

    bool ok() const { return m_ok; }
    const std::string& why() const { return m_why; }

    // How many pages at this page size (points).
    int pages(double w, double h);
    // Draw page `page` (0-based) into cr, w x h points.
    void draw_page(cairo_t* cr, int page, double w, double h);
    // The whole proof as a PDF. `paper`: "letter" or "a4".
    bool write_pdf(const std::string& path, const std::string& paper, int* pages_out = nullptr);

    static void paper_size(const std::string& paper, double& w, double& h);

private:
    // Lays the proof out; draws only the elements of page `only` (-1: none,
    // just count). Returns the page count.
    int run(cairo_t* cr, double w, double h, int only);

    const model::Font& m_font;
    model::ProofPlan   m_plan;
    bool               m_kern = true;
    bool               m_ok = false;
    std::string        m_why;
    std::string        m_file;      // the compiled font on disk
    struct Fonts;
    std::unique_ptr<Fonts> m_fonts;
};

}  // namespace granjon::views
