#pragma once
// model::Font -- the font as data, AND the undo journal's store.
//
// The store contract (core/Undo.hpp): get / put by id, by value. A record is
// either a font-level record under a reserved "@" id, or a glyph under its
// name (AGL names never start with "@", so the two can't collide):
//
//     @info  @metrics  @kerning  @metricskeys  @features   -- font records
//     A  eacute  uni0416  .notdef                          -- glyphs
//
// The glyph's place in the glyph order is IN its value (Glyph::order), so
// deleting a glyph and undoing puts it back in the same slot.
//
// One-door guard, as Cairn's Stones: put() with the journal's door shut,
// outside a Loading scope, counts a stray; the selftest fails on any stray.
#include "core/Undo.hpp"
#include "model/FontRecords.hpp"
#include "model/Glyph.hpp"

#include <functional>
#include <map>
#include <memory>
#include <unordered_map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace granjon::model {

using Record = std::variant<FontInfo, Metrics, Kerning, MetricsKeys, Features, CheckSkips, Glyph>;

class Font {
public:
    using Id    = std::string;
    using Value = Record;

    static constexpr const char* kInfo        = "@info";
    static constexpr const char* kMetrics     = "@metrics";
    static constexpr const char* kKerning     = "@kerning";
    static constexpr const char* kMetricsKeys = "@metricskeys";
    static constexpr const char* kFeatures    = "@features";
    static constexpr const char* kSkips       = "@skips";

    // ── the journal's contract ──────────────────────────────────────────────
    std::optional<Record> get(const Id& id) const;
    void put(const Id& id, const std::optional<Record>& v);

    // ── typed reading ───────────────────────────────────────────────────────
    const FontInfo&    info()         const { return m_info; }
    const Metrics&     metrics()      const { return m_metrics; }
    const Kerning&     kerning()      const { return m_kerning; }
    const MetricsKeys& metrics_keys() const { return m_keys; }
    const Features&    features()     const { return m_features; }
    const CheckSkips&  skips()        const { return m_skips; }
    const Glyph*       glyph(const std::string& name) const;
    std::size_t        glyph_count() const { return m_glyphs.size(); }
    std::vector<std::string> glyph_order() const;          // by Glyph::order
    double order_after(const std::string& name) const;     // a slot just after `name` (end if "")
    const Glyph* glyph_for(char32_t cp) const;              // first glyph mapping cp

    // ── the image pool (M13) ────────────────────────────────────────────────
    // PNG bytes by file name (the UFO's images/ folder). NOT a record and not
    // undoable: a glyph's image is a reference into the pool (that part is
    // undoable); the bytes are append-only for the life of the document --
    // undoing a placement drops the reference, the bytes stay. Save writes
    // only the referenced files. Same bytes -> the same name, so placing one
    // scan in ten glyphs stores it once.
    using ImageBytes = std::shared_ptr<const std::string>;
    std::string add_image(std::string png, const std::string& stem);
    ImageBytes  image(const std::string& file) const;
    const std::map<std::string, ImageBytes>& images() const { return m_images; }
    void set_images(std::map<std::string, ImageBytes> pool) { m_images = std::move(pool); }

    // Every record, by id -- for snapshots (the selftest's undo checks).
    std::map<Id, Record> snapshot() const;

    // ── the one-door guard ──────────────────────────────────────────────────
    void set_door(std::function<bool()> open) { m_door = std::move(open); }
    int  strays() const { return m_strays; }
    // Opening or creating a document is not a user choice and is not undoable.
    struct Loading {
        explicit Loading(Font& f) : m_f(f) { ++m_f.m_loading; }
        ~Loading() { --m_f.m_loading; }
        Font& m_f;
    };

    // Called after every put (the UI queues a repaint here).
    // Called after every put, with the record's id -- so the UI can repaint
    // only what a step touched (a guide edit must not rebuild 1000 cells).
    void set_on_change(std::function<void(const Id&)> f) { m_changed = std::move(f); }

private:
    FontInfo                     m_info;
    Metrics                      m_metrics;
    Kerning                      m_kerning;
    MetricsKeys                  m_keys;
    Features                     m_features;
    CheckSkips                   m_skips;
    std::map<std::string, Glyph> m_glyphs;
    std::map<std::string, ImageBytes> m_images;
    int                          m_strays  = 0;
    int                          m_loading = 0;
    std::function<bool()>        m_door;
    std::function<void(const Id&)> m_changed;
    // glyph_for's map (code point -> first glyph in glyph order), built on
    // first use, dropped by every put. Without it each lookup sorted the
    // whole glyph order (~14 ms per call on a 1000-glyph font).
    mutable std::unordered_map<char32_t, std::string> m_cmap;
    mutable bool                                      m_cmap_ok = false;
};

using FontJournal = core::Journal<Font>;

// A PNG's pixel size from its IHDR; false if the bytes aren't a PNG.
bool png_size(const std::string& png, int& w, int& h);

// A fresh font: default info + metrics, .notdef and space. Loaded, not a step.
void make_new_font(Font& f);

}  // namespace granjon::model
