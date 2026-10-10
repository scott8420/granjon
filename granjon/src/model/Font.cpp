#include "model/Font.hpp"

#include <algorithm>

namespace granjon::model {

std::optional<Record> Font::get(const Id& id) const {
    if (id == kInfo)        return Record{m_info};
    if (id == kMetrics)     return Record{m_metrics};
    if (id == kKerning)     return Record{m_kerning};
    if (id == kMetricsKeys) return Record{m_keys};
    if (id == kFeatures)    return Record{m_features};
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
    else if (v)                  m_glyphs[id] = std::get<Glyph>(*v);
    else                         m_glyphs.erase(id);
    m_cmap_ok = false;

    if (m_changed) m_changed();
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
    for (const char* id : {kInfo, kMetrics, kKerning, kMetricsKeys, kFeatures}) s.emplace(id, *get(id));
    for (const auto& [n, g] : m_glyphs) s.emplace(n, Record{g});
    return s;
}

void make_new_font(Font& f) {
    Font::Loading load(f);
    for (const auto& [id, r] : f.snapshot())
        if (id.empty() || id[0] != '@') f.put(id, std::nullopt);
    f.put(Font::kInfo, Record{FontInfo{}});
    f.put(Font::kMetrics, Record{Metrics{}});
    f.put(Font::kKerning, Record{Kerning{}});
    f.put(Font::kMetricsKeys, Record{MetricsKeys{}});
    f.put(Font::kFeatures, Record{Features{}});

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
