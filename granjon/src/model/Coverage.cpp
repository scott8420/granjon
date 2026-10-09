#include "model/Coverage.hpp"

#include "unicode/Agl.hpp"

namespace granjon::model {

std::set<char32_t> mapped(const Font& f) {
    std::set<char32_t> s;
    for (const auto& n : f.glyph_order())
        for (char32_t c : f.glyph(n)->unicodes) s.insert(c);
    return s;
}

std::vector<char32_t> missing_from(const Font& f, const std::vector<char32_t>& cps) {
    const auto have = mapped(f);
    std::vector<char32_t> out;
    for (char32_t c : cps)
        if (!have.count(c)) out.push_back(c);
    return out;
}

std::vector<NewGlyph> glyphs_for(const Font& f, const std::vector<char32_t>& cps) {
    std::vector<NewGlyph> out;
    std::set<std::string> taken;
    for (char32_t c : cps) {
        const std::string base = unicode::nice_name(c);
        std::string name = base;
        for (int i = 1; f.glyph(name) || taken.count(name); ++i) name = base + "." + std::to_string(i);
        taken.insert(name);
        out.push_back({name, {c}});
    }
    return out;
}

}  // namespace granjon::model
