#include "views/ImagePlace.hpp"

#include "command/Verbs.hpp"
#include "Log.hpp"

#include <gdkmm/texture.h>
#include <giomm/liststore.h>
#include <glibmm/bytes.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>

#include <filesystem>

namespace granjon::views {

namespace {

bool is_png(const std::string& b) { int w = 0, h = 0; return model::png_size(b, w, h); }

}  // namespace

bool place_image_file(ViewContext& ctx, const std::string& glyph, const Glib::RefPtr<Gio::File>& file) {
    auto& f = ctx.doc.font;
    const model::Glyph* g = f.glyph(glyph);
    if (!g || !file) return false;
    const std::string shown = file->get_basename();
    std::string bytes;
    try {
        char* data = nullptr;
        gsize len = 0;
        std::string etag;
        file->load_contents(data, len, etag);
        bytes.assign(data, len);
        g_free(data);
    } catch (const Glib::Error& e) {
        if (ctx.say) ctx.say("Couldn't read " + shown + ": " + e.what(), true);
        return false;
    }
    if (!is_png(bytes)) {
        // Anything GdkTexture reads (JPEG, TIFF, ...) goes into the font as PNG.
        try {
            auto gb = Glib::Bytes::create(bytes.data(), bytes.size());
            auto tex = Gdk::Texture::create_from_bytes(gb);
            auto png = tex->save_to_png_bytes();
            gsize n = 0;
            const auto* p = static_cast<const char*>(png->get_data(n));
            bytes.assign(p, n);
        } catch (const Glib::Error& e) {
            if (ctx.say) ctx.say(shown + " isn't an image Granjon can read (PNG or JPEG): " + e.what(), true);
            return false;
        }
    }
    int w = 0, h = 0;
    if (!model::png_size(bytes, w, h)) {
        if (ctx.say) ctx.say(shown + " isn't an image Granjon can read (PNG or JPEG).", true);
        return false;
    }
    const std::string stem = std::filesystem::path(shown).stem().string();
    model::GlyphImage im;
    im.file = f.add_image(std::move(bytes), stem.empty() ? glyph : stem);
    // A fresh image at the preference's opacity; a replacement keeps the old
    // one's opacity and colour.
    im.opacity = ctx.settings.image_opacity / 100.0;
    if (g->image) { im.opacity = g->image->opacity; im.color = g->image->color; }
    im = command::image_fitted(f, im, w, h);
    std::string why;
    if (!command::place_image(ctx.doc.journal, f, glyph, im, &why)) {
        if (ctx.say) ctx.say(why.empty() ? "That image is already there." : why, !why.empty());
        return false;
    }
    if (auto lg = log::get(log::Area::Io)) lg->info("image: {} ({} x {} px) placed on {}", im.file, w, h, glyph);
    if (ctx.say)
        ctx.say(ctx.doc.journal.undo_label() + ": " + std::to_string(w) + " x " + std::to_string(h) +
                    " px, descender to ascender. Drag it or its corners; lock it to trace.", false);
    return true;
}

void choose_image_file(ViewContext& ctx, Gtk::Window* parent, const std::string& glyph) {
    if (!ctx.doc.font.glyph(glyph)) return;
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Place an image behind " + glyph + "  (a scan or sketch: PNG / JPEG)");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto images = Gtk::FileFilter::create();
    images->set_name("Images (PNG, JPEG)");
    images->add_mime_type("image/png");
    images->add_mime_type("image/jpeg");
    for (const char* p : {"*.png", "*.PNG", "*.jpg", "*.JPG", "*.jpeg", "*.JPEG"}) images->add_pattern(p);
    filters->append(images);
    auto all = Gtk::FileFilter::create();
    all->set_name("All files");
    all->add_pattern("*");
    filters->append(all);
    dialog->set_filters(filters);
    auto done = [&ctx, glyph, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            if (auto file = dialog->open_finish(result)) place_image_file(ctx, glyph, file);
        } catch (const Glib::Error&) {
            // dismissed
        }
    };
    if (parent) dialog->open(*parent, done);
    else dialog->open(done);
}

}  // namespace granjon::views
