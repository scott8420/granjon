#include "model/Spacing.hpp"

#include "geom/Edit.hpp"
#include "model/Bounds.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace granjon::model {

namespace {

// One code point from UTF-8 at s[i]; advances i. Bad bytes come back as U+FFFD.
char32_t next_cp(const std::string& s, std::size_t& i) {
    const auto b = static_cast<unsigned char>(s[i]);
    int len = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 0;
    if (len == 0 || i + len > s.size()) { ++i; return 0xFFFD; }
    char32_t cp = len == 1 ? b : len == 2 ? (b & 0x1F) : len == 3 ? (b & 0x0F) : (b & 0x07);
    for (int k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += len;
    return cp;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// An expression as stored / shown: trimmed, one leading '=' dropped.
std::string norm_expr(const std::string& s) {
    std::string t = trim(s);
    if (!t.empty() && t[0] == '=') t = trim(t.substr(1));
    return t;
}

bool name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
}

}  // namespace

// ── the strip text ─────────────────────────────────────────────────────────

std::vector<std::string> parse_strip(const Font& f, const std::string& text, std::vector<std::string>* missing) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '/') {
            if (i + 1 < text.size() && text[i + 1] == '/') {   // "//": a literal slash
                if (const Glyph* g = f.glyph_for(U'/')) out.push_back(g->name);
                else if (missing) missing->push_back("/");
                i += 2;
                continue;
            }
            std::size_t j = i + 1;
            while (j < text.size() && text[j] != '/' && text[j] != ' ') ++j;
            const std::string name = text.substr(i + 1, j - i - 1);
            if (!name.empty()) {
                if (f.glyph(name)) out.push_back(name);
                else if (missing) missing->push_back("/" + name);
            }
            i = (j < text.size() && text[j] == ' ') ? j + 1 : j;
            continue;
        }
        const std::size_t at = i;
        const char32_t cp = next_cp(text, i);
        if (const Glyph* g = f.glyph_for(cp)) out.push_back(g->name);
        else if (missing) missing->push_back(text.substr(at, i - at));
    }
    return out;
}

// ── kerning ────────────────────────────────────────────────────────────────

std::string kern_group(const Kerning& k, const std::string& glyph, int side) {
    const std::string prefix = side == 1 ? "public.kern1." : "public.kern2.";
    for (const auto& [name, members] : k.groups)
        if (name.starts_with(prefix) && std::find(members.begin(), members.end(), glyph) != members.end())
            return name;
    return {};
}

KernHit kern_lookup(const Kerning& k, const std::string& left, const std::string& right) {
    const std::string g1 = kern_group(k, left, 1), g2 = kern_group(k, right, 2);
    const KernKey order[] = {{left, right}, {left, g2}, {g1, right}, {g1, g2}};
    for (const auto& key : order) {
        if (key.first.empty() || key.second.empty()) continue;
        if (auto it = k.pairs.find(key); it != k.pairs.end()) return {key, it->second, true};
    }
    return {};
}

KernKey kern_target(const Kerning& k, const std::string& left, const std::string& right, bool exception) {
    if (exception) return {left, right};
    if (const auto hit = kern_lookup(k, left, right); hit.found) return hit.key;
    std::string g1 = kern_group(k, left, 1), g2 = kern_group(k, right, 2);
    return {g1.empty() ? left : g1, g2.empty() ? right : g2};
}

std::string kern_kind(const KernKey& key) {
    const bool a = key.first.starts_with("public.kern1."), b = key.second.starts_with("public.kern2.");
    if (a && b) return "group / group";
    if (a) return "group / glyph";
    if (b) return "glyph / group";
    return "glyph / glyph";
}

// ── layout ─────────────────────────────────────────────────────────────────

std::vector<Placed> layout_strip(const Font& f, const std::vector<std::string>& names) {
    std::vector<Placed> out;
    double x = 0;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const Glyph* g = f.glyph(names[i]);
        if (!g) continue;
        Placed p{names[i], x, g->advance, 0};
        if (!out.empty()) {
            p.kern = kern_lookup(f.kerning(), out.back().name, names[i]).value;
            p.x += p.kern;
        }
        x = p.x + g->advance;
        out.push_back(p);
    }
    return out;
}

// ── metrics keys ───────────────────────────────────────────────────────────

KeyExpr parse_key_expr(const std::string& raw) {
    KeyExpr e;
    std::string s = trim(raw);
    if (!s.empty() && s[0] == '=') s = trim(s.substr(1));
    if (s.empty()) return e;
    // A plain number?
    {
        char* end = nullptr;
        const double v = std::strtod(s.c_str(), &end);
        if (end && end != s.c_str() && trim(end).empty()) {
            e.valid = true;
            e.number = v;
            return e;
        }
    }
    std::size_t i = 0;
    if (s[0] == '|') { e.side = '|'; i = 1; }
    const std::size_t a = i;
    while (i < s.size() && name_char(s[i])) ++i;
    std::string name = s.substr(a, i - a);
    // A trailing ".L" / ".R" names the side (glyph names may hold dots too).
    if (name.size() > 2 && name[name.size() - 2] == '.' &&
        (name.back() == 'L' || name.back() == 'R')) {
        if (e.side == '|') return e;   // "|n.L" is contradictory
        e.side = name.back();
        name.resize(name.size() - 2);
    }
    if (name.empty()) return e;
    e.ref = name;
    std::string rest = trim(s.substr(i));
    if (!rest.empty()) {
        if (rest[0] != '+' && rest[0] != '-') return e;
        const double sign = rest[0] == '-' ? -1 : 1;
        rest = trim(rest.substr(1));
        char* end = nullptr;
        const double v = std::strtod(rest.c_str(), &end);
        if (!end || end == rest.c_str() || !trim(end).empty()) return e;
        e.number = sign * v;
    }
    e.valid = true;
    return e;
}

bool parse_key_rule(const std::string& line_in, const std::string& target, MetricsKeys::Key* out,
                    std::string* why) {
    auto no = [&](const std::string& w) { if (why) *why = w; return false; };
    MetricsKeys::Key k;
    std::string line = trim(line_in);
    if (line.empty()) { *out = k; return true; }
    // "m: ..." names the target.
    if (auto c = line.find(':'); c != std::string::npos) {
        const std::string who = trim(line.substr(0, c));
        if (!target.empty() && who != target) return no("The rule names " + who + ", not " + target);
        line = trim(line.substr(c + 1));
    }
    // "b.L=l.L" / "m=n": a target-side assignment on the left of the first '='.
    if (!target.empty() && line.starts_with(target)) {
        const std::string rest = line.substr(target.size());
        if (rest.starts_with(".L=")) line = "L=" + rest.substr(3);
        else if (rest.starts_with(".R=")) line = "R=" + rest.substr(3);
        else if (rest.starts_with(".W=")) line = "W=" + rest.substr(3);
        else if (rest.starts_with("=")) line = rest;   // "m=n" -> "=n"
    }
    // A lone expression ("=n", "n", "=n+10", "60"): both sides.
    {
        const auto first_eq = line.find('=');
        const bool assigns = first_eq != std::string::npos && first_eq > 0 &&
                             (line.substr(0, first_eq) == "L" || line.substr(0, first_eq) == "R" ||
                              line.substr(0, first_eq) == "W");
        if (!assigns) {
            const KeyExpr e = parse_key_expr(line);
            if (!e.valid) return no("Can't read \"" + line + "\"");
            k.left = k.right = norm_expr(line);
            *out = k;
            return true;
        }
    }
    // Assignments, space separated; "L=R=60" chains.
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        if (i >= line.size()) break;
        std::size_t j = i;
        while (j < line.size() && line[j] != ' ') ++j;
        const std::string part = line.substr(i, j - i);
        i = j;
        std::vector<char> sides;
        std::size_t p = 0;
        while (p + 1 < part.size() && (part[p] == 'L' || part[p] == 'R' || part[p] == 'W') && part[p + 1] == '=') {
            sides.push_back(part[p]);
            p += 2;
        }
        const std::string expr = part.substr(p);
        if (sides.empty()) return no("Expected L=, R= or W= at \"" + part + "\"");
        if (!parse_key_expr(expr).valid) return no("Can't read \"" + expr + "\"");
        for (char s : sides) (s == 'L' ? k.left : s == 'R' ? k.right : k.width) = norm_expr(expr);
    }
    *out = k;
    return true;
}

std::string format_key_rule(const MetricsKeys::Key& key) {
    const MetricsKeys::Key k{norm_expr(key.left), norm_expr(key.right), norm_expr(key.width)};
    if (k.width.empty() && !k.left.empty() && k.left == k.right) {
        const KeyExpr e = parse_key_expr(k.left);
        return e.ref.empty() ? "L=R=" + k.left : "=" + k.left;
    }
    std::string s;
    auto add = [&](const char* tag, const std::string& v) {
        if (v.empty()) return;
        if (!s.empty()) s += ' ';
        s += tag;
        s += v;
    };
    add("L=", k.left);
    add("R=", k.right);
    add("W=", k.width);
    return s;
}

std::optional<double> eval_key_side(const Font& f, const std::string& expr, char side) {
    const KeyExpr e = parse_key_expr(expr);
    if (!e.valid) return std::nullopt;
    if (e.ref.empty()) return e.number;
    const Glyph* g = f.glyph(e.ref);
    if (!g) return std::nullopt;
    if (side == 'W') return g->advance + e.number;
    char from = side;
    if (e.side == 'L' || e.side == 'R') from = e.side;
    else if (e.side == '|') from = side == 'L' ? 'R' : 'L';
    const auto sb = sidebearings(f, *g);
    if (!sb) return std::nullopt;
    return (from == 'L' ? sb->left : sb->right) + e.number;
}

KeyCheck check_key(const Font& f, const std::string& glyph) {
    KeyCheck c;
    const auto& keys = f.metrics_keys().keys;
    auto it = keys.find(glyph);
    const Glyph* g = f.glyph(glyph);
    if (it == keys.end() || !g) return c;
    const auto& k = it->second;
    c.has_key = !k.left.empty() || !k.right.empty() || !k.width.empty();
    const auto sb = sidebearings(f, *g);
    auto off = [](double a, double b) { return std::abs(a - b) > 0.5; };
    if (!k.left.empty()) {
        c.left = eval_key_side(f, k.left, 'L');
        if (c.left && sb) c.left_off = off(*c.left, sb->left);
    }
    if (!k.right.empty()) {
        c.right = eval_key_side(f, k.right, 'R');
        if (c.right && sb) c.right_off = off(*c.right, sb->right);
    }
    if (!k.width.empty()) {
        c.width = eval_key_side(f, k.width, 'W');
        if (c.width) c.width_off = off(*c.width, g->advance);
    }
    return c;
}

void set_lsb(const Font& f, Glyph& g, double v) {
    const auto sb = sidebearings(f, g);
    if (!sb) return;
    const double d = v - sb->left;
    geom::shift_x(g, d);
    g.advance = std::max(0.0, g.advance + d);
}

void set_rsb(const Font& f, Glyph& g, double v) {
    const auto sb = sidebearings(f, g);
    if (!sb) return;
    g.advance = std::max(0.0, g.advance + (v - sb->right));
}

std::optional<Glyph> apply_key(const Font& f, const std::string& glyph) {
    const KeyCheck c = check_key(f, glyph);
    if (!c.off()) return std::nullopt;
    Glyph g = *f.glyph(glyph);
    if (c.left_off)  set_lsb(f, g, std::round(*c.left));
    if (c.right_off) set_rsb(f, g, std::round(*c.right));
    if (c.width_off) g.advance = std::max(0.0, std::round(*c.width));
    if (g == *f.glyph(glyph)) return std::nullopt;
    return g;
}

std::vector<std::string> key_order(const Font& f, std::vector<std::string>* cyclic) {
    const auto& keys = f.metrics_keys().keys;
    std::map<std::string, std::set<std::string>> deps;
    for (const auto& [name, k] : keys) {
        if (!f.glyph(name)) continue;
        auto& d = deps[name];
        for (const std::string* s : {&k.left, &k.right, &k.width}) {
            const KeyExpr e = parse_key_expr(*s);
            if (e.valid && !e.ref.empty() && e.ref != name && keys.count(e.ref)) d.insert(e.ref);
        }
    }
    std::vector<std::string> out;
    std::map<std::string, int> state;   // 0 new, 1 visiting, 2 done
    std::function<void(const std::string&)> visit = [&](const std::string& n) {
        if (state[n] == 2) return;
        if (state[n] == 1) {
            if (cyclic && std::find(cyclic->begin(), cyclic->end(), n) == cyclic->end()) cyclic->push_back(n);
            return;
        }
        state[n] = 1;
        for (const auto& d : deps[n]) visit(d);
        state[n] = 2;
        out.push_back(n);
    };
    for (const auto& [n, _] : deps) visit(n);
    return out;
}

}  // namespace granjon::model
