// selftest_image -- M13: the background image. The pool (one copy per PNG,
// file names any file system takes), the GLIF <image> both ways (UFO tools'
// files read, ours written as they expect), images/ on save and load (only
// what some glyph places), the verbs (one step each, undo / redo), the drag
// (offset snapped, scale kept), the scan riding with sidebearing moves, and
// export never seeing it.
#include "command/Document.hpp"
#include "command/Verbs.hpp"
#include "export/Otf.hpp"
#include "geom/Edit.hpp"
#include "io/Glif.hpp"
#include "io/Ufo.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>

namespace model   = granjon::model;
namespace command = granjon::command;
namespace io      = granjon::io;
namespace fs      = std::filesystem;

void selftest_image(const std::function<void(const std::string&, bool, const std::string&)>& check_) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_(n, ok, d); };
    std::cout << "M13 background image\n";
    const std::string& png = command::sample_png();

    {   // the PNG header and the pool
        int w = 0, h = 0;
        check("image: png_size reads the IHDR", model::png_size(png, w, h) && w == 4 && h == 2);
        check("image: png_size refuses a JPEG / junk", !model::png_size("\xFF\xD8\xFF\xE0 not a png at all....", w, h));
        model::Font f;
        model::make_new_font(f);
        const std::string a = f.add_image(png, "My Scan");
        const std::string b = f.add_image(png, "another name");
        const std::string c = f.add_image(png + "x", "My Scan");
        check("image: pool names are safe (lowercase, .png)", a == "my_scan.png", a);
        check("image: the same bytes are stored once", b == a && f.images().size() == 2);
        check("image: a different image with the same name gets a suffix", c == "my_scan-2.png", c);
        check("image: a .png stem isn't doubled", f.add_image(png + "y", "sheet.png") == "sheet.png");
        model::make_new_font(f);
        check("image: a new font starts with an empty pool", f.images().empty());
    }

    {   // GLIF both ways
        model::Glyph g;
        g.name = "a";
        g.advance = 500;
        g.image = model::GlyphImage{"scan.png", 0.5, 0.1, -0.1, 0.5, 20, -150, "1,0,0,0.5", 0.65, true};
        const std::string xml = io::write_glif(g);
        auto back = io::read_glif(xml);
        check("image: GLIF round trip keeps file, transform, color, opacity, lock", back && back->image == g.image, xml);
        check("image: <image> sits before guidelines and outline (GLIF order)",
              xml.find("<image fileName=\"scan.png\"") != std::string::npos &&
              xml.find("<image") > xml.find("<advance") && xml.find("granjon.image") != std::string::npos);
        g.image = model::GlyphImage{"plain.png"};
        const std::string xml2 = io::write_glif(g);
        check("image: defaults write a bare <image> and no lib entry",
              xml2.find("<image fileName=\"plain.png\"/>") != std::string::npos &&
              xml2.find("granjon.image") == std::string::npos, xml2);
        auto other = io::read_glif("<?xml version=\"1.0\"?><glyph name=\"b\" format=\"2\">"
                                   "<image fileName=\"b.png\" xScale=\"2\" yScale=\"2\" yOffset=\"-200\"/></glyph>");
        check("image: another tool's <image> reads (opacity default, unlocked)",
              other && other->image && other->image->file == "b.png" && other->image->xx == 2 &&
              other->image->dy == -200 && other->image->opacity == model::GlyphImage{}.opacity && !other->image->locked);
        check("image: <image> without a file name is refused",
              !io::read_glif("<glyph name=\"c\" format=\"2\"><image xScale=\"2\"/></glyph>"));
    }

    {   // save / load: images/
        model::Font f;
        command::make_fixture(f);
        f.add_image(png + "unused", "orphan");   // in the pool, placed nowhere
        const fs::path dir = fs::temp_directory_path() / "granjon_selftest_image.granjon";
        fs::remove_all(dir);
        std::string err;
        const bool saved = io::save_ufo(f, dir.string(), &err);
        check("image: the font saves", saved, err);
        std::ifstream in(dir / "images" / "scan.png", std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        check("image: images/scan.png is written byte for byte", ss.str() == png);
        check("image: an image no glyph places is not written", !fs::exists(dir / "images" / "orphan.png"));
        model::Font g;
        const bool loaded = io::load_ufo(dir.string(), g, &err);
        check("image: the font loads", loaded, err);
        check("image: o's image comes back as it was", loaded && g.glyph("o")->image == f.glyph("o")->image);
        check("image: the pool holds the file again", loaded && g.image("scan.png") && *g.image("scan.png") == png);
        // A font with no image: no images/ folder.
        model::Font plain;
        model::make_new_font(plain);
        const fs::path dir2 = fs::temp_directory_path() / "granjon_selftest_image2.granjon";
        fs::remove_all(dir2);
        io::save_ufo(plain, dir2.string(), &err);
        check("image: no image, no images/ folder", !fs::exists(dir2 / "images"));
        fs::remove_all(dir);
        fs::remove_all(dir2);
    }

    {   // the verbs
        granjon::command::Document doc;
        auto& f = doc.font;
        auto& j = doc.journal;
        command::make_fixture(f);
        j.clear();
        std::string why;
        check("image: placing a file the pool lacks is refused, with a reason",
              !command::place_image(j, f, "n", {"nope.png"}, &why) && !why.empty() && !f.glyph("n")->image, why);
        const auto fit = command::image_fitted(f, {"scan.png"}, 4, 2);
        const auto& m = f.metrics();
        check("image: a fresh image spans descender to ascender",
              std::abs(fit.dy - m.descender) < 1e-9 && std::abs(fit.yy * 2 - (m.ascender - m.descender)) < 1e-9 &&
              fit.xx == fit.yy && fit.dx == 0);
        check("image: place is one step", command::place_image(j, f, "n", fit) && j.undo_label().find("Place image") == 0,
              j.undo_label());
        j.undo();
        check("image: undo takes it away", !f.glyph("n")->image);
        j.redo();
        check("image: redo puts it back", f.glyph("n")->image == fit);
        command::place_image(j, f, "n", {"scan.png", 3, 0, 0, 3, 0, 0});
        check("image: placing again says Replace", j.undo_label().find("Replace image") == 0, j.undo_label());
        check("image: opacity is clamped and one step", command::set_image_opacity(j, f, "n", 1.7) &&
              f.glyph("n")->image->opacity == 1.0);
        check("image: lock is one step, and refused when unchanged",
              command::set_image_locked(j, f, "n", true) && !command::set_image_locked(j, f, "n", true));
        check("image: a flat (zero-scale) frame is refused",
              !command::set_image_frame(j, f, "n", {"", 0, 0, 0, 3, 0, 0}));
        check("image: remove is one step", command::remove_image(j, f, "n") && !f.glyph("n")->image);
        check("image: removing nothing is refused, with a reason", !command::remove_image(j, f, "n", &why) && !why.empty());
        j.undo();
        check("image: undo remove -> locked image back", f.glyph("n")->image && f.glyph("n")->image->locked);

        // the drag: one step, offset whole units, scale as dragged
        j.clear();
        command::image_drag_begin(j, f, "o", "Scale image");
        command::set_image_frame(j, f, "o", {"", 112.5, 0, 0, 112.5, 33.6, -139.7});
        command::set_image_frame(j, f, "o", {"", 125.25, 0, 0, 125.25, 40.4, -130.2});
        const bool kept = command::drag_end(j, f);
        const auto& im = *f.glyph("o")->image;
        check("image: a drag is one step", kept && j.undo_label() == "Scale image", j.undo_label());
        check("image: the drag's offset snaps to whole units, the scale doesn't",
              im.dx == 40 && im.dy == -130 && im.xx == 125.25);
        j.undo();
        check("image: undo the drag -> as placed", f.glyph("o")->image->xx == 100 && f.glyph("o")->image->dx == 20);

        // the left sidebearing: the drawing stays put on screen, so does the scan
        const double before = f.glyph("o")->image->dx - f.glyph("o")->contours[0].nodes[0].x;
        command::drag_begin(j, f, {"o"}, "Left sidebearing");
        command::drag_sidebearing(j, f, command::Side::Left, 30, false);
        command::drag_end(j, f);
        const double after = f.glyph("o")->image->dx - f.glyph("o")->contours[0].nodes[0].x;
        check("image: the scan moves with the drawing on a sidebearing drag", before == after);
    }

    {   // export never sees it
        setenv("SOURCE_DATE_EPOCH", "1700000000", 1);
        model::Font f;
        command::make_fixture(f);
        const auto with = granjon::otf::compile_otf(f);
        model::Font g;
        command::make_fixture(g);
        {
            model::Font::Loading l(g);
            model::Glyph o = *g.glyph("o");
            o.image.reset();
            g.put("o", model::Record{o});
        }
        const auto without = granjon::otf::compile_otf(g);
        check("image: the OTF is the same with or without the image", !with.empty() && with == without);
    }
}
