#include "model/Glyph.hpp"

#include <cmath>

namespace granjon::model {

const char* status_name(GlyphStatus s) {
    switch (s) {
        case GlyphStatus::Empty:   return "empty";
        case GlyphStatus::Drafted: return "drafted";
        case GlyphStatus::Done:    return "done";
        case GlyphStatus::Review:  return "review";
    }
    return "?";
}

namespace {
double r(double v) { return std::round(v) + 0.0; }   // +0.0: no negative zero
bool   whole(double v) { return v == std::round(v); }
}  // namespace

void Glyph::snap() {
    advance = r(advance);
    for (auto& c : contours)
        for (auto& n : c.nodes) {
            n.x = r(n.x);     n.y = r(n.y);
            n.cx1 = r(n.cx1); n.cy1 = r(n.cy1);
            n.cx2 = r(n.cx2); n.cy2 = r(n.cy2);
        }
    for (auto& a : anchors) { a.x = r(a.x); a.y = r(a.y); }
    for (auto& k : components) { k.dx = r(k.dx); k.dy = r(k.dy); }
    for (auto& gd : guides) { gd.x = r(gd.x); gd.y = r(gd.y); }
}

bool Glyph::is_integral() const {
    if (!whole(advance)) return false;
    for (const auto& c : contours)
        for (const auto& n : c.nodes)
            if (!whole(n.x) || !whole(n.y) || !whole(n.cx1) || !whole(n.cy1) ||
                !whole(n.cx2) || !whole(n.cy2))
                return false;
    for (const auto& a : anchors) if (!whole(a.x) || !whole(a.y)) return false;
    for (const auto& k : components) if (!whole(k.dx) || !whole(k.dy)) return false;
    return true;
}

bool Glyph::uses(const std::string& base) const {
    for (const auto& k : components) if (k.base == base) return true;
    return false;
}

bool valid_glyph_name(const std::string& name) {
    if (name == ".notdef" || name == ".null") return true;
    if (name.empty() || name.size() > 63) return false;
    if (name[0] == '.' || (name[0] >= '0' && name[0] <= '9')) return false;
    for (char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

}  // namespace granjon::model
