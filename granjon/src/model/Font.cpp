#include "model/Font.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>

namespace granjon::model {

std::optional<Record> Font::get(const Id& id) const {
    if (id == kInfo)        return Record{m_info};
    if (id == kMetrics)     return Record{m_metrics};
    if (id == kKerning)     return Record{m_kerning};
    if (id == kMetricsKeys) return Record{m_keys};
    if (id == kFeatures)    return Record{m_features};
    if (id == kSkips)       return Record{m_skips};
    const auto it = m_glyphs.find(id);
    if (it == m_glyphs.end()) return std::nullopt;
    return Record{it->second};
}

void Font::put(const Id& id, const std::optional<Record>& v) {
    // The gate: a write with the door shut, outside a load, is a stray.
    if (m_loading == 0 && m_door && !m_door()) ++m_strays;

    // Font records always exist; "absent" puts their default back.
    if (id == kInfo)        m_info     = v ? std::get<FontInfo>(*v)    : FontInfo{};
    else if (id == kMetrics)     m_metrics  = v ? std::get<Metrics>(*v)     : Metrics{};
    else if (id == kKerning)     m_kerning  = v ? std::get<Kerning>(*v)     : Kerning{};
    else if (id == kMetricsKeys) m_keys     = v ? std::get<MetricsKeys>(*v) : MetricsKeys{};
    else if (id == kFeatures)    m_features = v ? std::get<Features>(*v)    : Features{};
    else if (id == kSkips)       m_skips    = v ? std::get<CheckSkips>(*v)  : CheckSkips{};
    else if (v)                  m_glyphs[id] = std::get<Glyph>(*v);
    else                         m_glyphs.erase(id);
    m_cmap_ok = false;

    if (m_changed) m_changed(id);
}

const Glyph* Font::glyph(const std::string& name) const {
    const auto it = m_glyphs.find(name);
    return it == m_glyphs.end() ? nullptr : &it->second;
}

std::vector<std::string> Font::glyph_order() const {
    std::vector<std::string> out;
    out.reserve(m_glyphs.size());
    for (const auto& [n, g] : m_glyphs) out.push_back(n);
    std::stable_sort(out.begin(), out.end(), [this](const std::string& a, const std::string& b) {
        return m_glyphs.at(a).order < m_glyphs.at(b).order;
    });
    return out;
}

double Font::order_after(const std::string& name) const {
    const auto order = glyph_order();
    if (order.empty()) return 1.0;
    const auto it = std::find(order.begin(), order.end(), name);
    if (it == order.end()) return m_glyphs.at(order.back()).order + 1.0;   // append
    const double here = m_glyphs.at(*it).order;
    if (it + 1 == order.end()) return here + 1.0;
    return (here + m_glyphs.at(*(it + 1)).order) / 2.0;                    // between
}

const Glyph* Font::glyph_for(char32_t cp) const {
    if (!m_cmap_ok) {
        m_cmap.clear();
        for (const auto& n : glyph_order())          // first in glyph order wins
            for (char32_t u : m_glyphs.at(n).unicodes) m_cmap.emplace(u, n);
        m_cmap_ok = true;
    }
    const auto it = m_cmap.find(cp);
    return it == m_cmap.end() ? nullptr : &m_glyphs.at(it->second);
}

std::map<Font::Id, Record> Font::snapshot() const {
    std::map<Id, Record> s;
    for (const char* id : {kInfo, kMetrics, kKerning, kMetricsKeys, kFeatures, kSkips}) s.emplace(id, *get(id));
    for (const auto& [n, g] : m_glyphs) s.emplace(n, Record{g});
    return s;
}

// ── the image pool (M13) ───────────────────────────────────────────────────

std::string Font::add_image(std::string png, const std::string& stem) {
    for (const auto& [name, bytes] : m_images)
        if (bytes && *bytes == png) return name;   // stored once
    // A file name any file system takes: lowercase [a-z0-9._-], ".png".
    std::string base;
    for (char c : stem) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) base += static_cast<char>(std::tolower(u));
        else if (c == '-' || c == '_' || c == '.') base += c;
        else base += '_';
    }
    while (!base.empty() && base[0] == '.') base.erase(0, 1);
    if (base.size() > 4 && base.compare(base.size() - 4, 4, ".png") == 0) base.resize(base.size() - 4);
    if (base.empty()) base = "image";
    if (base.size() > 40) base.resize(40);
    std::string name = base + ".png";
    for (int n = 2; m_images.count(name); ++n) name = base + "-" + std::to_string(n) + ".png";
    m_images[name] = std::make_shared<const std::string>(std::move(png));
    return name;
}

Font::ImageBytes Font::image(const std::string& file) const {
    const auto it = m_images.find(file);
    return it == m_images.end() ? nullptr : it->second;
}

bool png_size(const std::string& png, int& w, int& h) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (png.size() < 24 || std::memcmp(png.data(), sig, 8) != 0 || png.compare(12, 4, "IHDR") != 0) return false;
    auto be32 = [&](std::size_t at) {
        const auto* b = reinterpret_cast<const unsigned char*>(png.data() + at);
        return (static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16) |
               (static_cast<std::uint32_t>(b[2]) << 8) | b[3];
    };
    const std::uint32_t pw = be32(16), ph = be32(20);
    if (pw == 0 || ph == 0 || pw > 100000 || ph > 100000) return false;
    w = static_cast<int>(pw);
    h = static_cast<int>(ph);
    return true;
}

void make_new_font(Font& f) {
    f.set_images({});
    Font::Loading load(f);
    for (const auto& [id, r] : f.snapshot())
        if (id.empty() || id[0] != '@') f.put(id, std::nullopt);
    f.put(Font::kInfo, Record{FontInfo{}});
    f.put(Font::kMetrics, Record{Metrics{}});
    f.put(Font::kKerning, Record{Kerning{}});
    f.put(Font::kMetricsKeys, Record{MetricsKeys{}});
    f.put(Font::kFeatures, Record{Features{}});
    f.put(Font::kSkips, Record{CheckSkips{}});

    // .notdef: the traditional hollow box, 500 wide. Outer counter-clockwise,
    // inner clockwise (y-up) -- the CFF direction.
    Glyph notdef;
    notdef.name    = ".notdef";
    notdef.advance = 500;
    notdef.order   = 1;
    notdef.status  = GlyphStatus::Drafted;
    Contour outer, inner;
    for (auto [x, y] : {std::pair{50, 0}, {450, 0}, {450, 700}, {50, 700}})
        outer.nodes.push_back(Node::corner(x, y));
    for (auto [x, y] : {std::pair{100, 50}, {100, 650}, {400, 650}, {400, 50}})
        inner.nodes.push_back(Node::corner(x, y));
    notdef.contours = {outer, inner};
    f.put(notdef.name, Record{notdef});

    Glyph space;
    space.name     = "space";
    space.unicodes = {U' '};
    space.advance  = 250;
    space.order    = 2;
    space.status   = GlyphStatus::Done;
    f.put(space.name, Record{space});
}

}  // namespace granjon::model
