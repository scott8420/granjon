// model::Fea (M16c) -- see Fea.hpp.
#include "model/Fea.hpp"
#include "model/Font.hpp"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <map>
#include <set>

namespace granjon::model {

const char* const kFeaAutoBegin = "# --- automatic (Granjon: Generate replaces this block) ---";
const char* const kFeaAutoEnd   = "# --- end automatic ---";

namespace {

struct Tok {
    enum class K { Name, Class, Sym, Str, End };
    K           k = K::End;
    std::string s;
    int         line = 1;
};

bool name_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-' || c == '*' || c == '+'; }

std::vector<Tok> lex(const std::string& t) {
    std::vector<Tok> out;
    int line = 1;
    std::size_t i = 0;
    while (i < t.size()) {
        const char c = t[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == '#') { while (i < t.size() && t[i] != '\n') ++i; continue; }
        if (c == '"') {
            std::size_t j = i + 1;
            const int at = line;
            while (j < t.size() && t[j] != '"') { if (t[j] == '\n') ++line; ++j; }
            out.push_back({Tok::K::Str, t.substr(i + 1, j - i - 1), at});
            i = j + 1;
            continue;
        }
        if (c == '@' || c == '\\' || name_char(c)) {
            const bool cls = c == '@';
            std::size_t j = i + 1;
            if (c == '\\' && j < t.size() && t[j] == '@') ++j;
            while (j < t.size() && (name_char(t[j]) || (cls && t[j] == '@'))) ++j;
            std::string s = t.substr(i, j - i);
            if (!s.empty() && s[0] == '\\') s.erase(0, 1);
            // a lone '-' is the range sign; a name never starts with one
            if (s == "-") { out.push_back({Tok::K::Sym, "-", line}); i = j; continue; }
            out.push_back({cls ? Tok::K::Class : Tok::K::Name, cls ? s.substr(1) : s, line});
            i = j;
            continue;
        }
        out.push_back({Tok::K::Sym, std::string(1, c), line});
        ++i;
    }
    out.push_back({Tok::K::End, "", line});
    return out;
}

class Parser {
public:
    Parser(const std::string& text, const std::function<bool(const std::string&)>& exists)
        : m_t(lex(text)), m_exists(exists) {}

    FeaDoc run() {
        while (peek().k != Tok::K::End) top();
        return std::move(m_doc);
    }

private:
    std::vector<Tok> m_t;
    std::size_t      m_i = 0;
    std::function<bool(const std::string&)> m_exists;
    std::map<std::string, std::vector<std::string>> m_classes;
    std::map<std::string, int> m_named;   // lookup name -> index
    FeaDoc m_doc;

    const Tok& peek(std::size_t k = 0) const { return m_t[std::min(m_i + k, m_t.size() - 1)]; }
    const Tok& take() { const Tok& x = peek(); if (m_i < m_t.size() - 1) ++m_i; return x; }
    bool is(const char* s, std::size_t k = 0) const { return peek(k).s == s && peek(k).k != Tok::K::Str; }
    void error(int line, const std::string& s) { m_doc.errors.push_back({line, s}); }
    void warn(int line, const std::string& s) { m_doc.warnings.push_back({line, s}); }
    bool expect(const char* s) {
        if (is(s)) { take(); return true; }
        error(peek().line, std::string("expected '") + s + "', found '" + (peek().k == Tok::K::End ? "end of file" : peek().s) + "'");
        return false;
    }
    void skip_statement() {   // to the next ';' at this level (a {...} block skipped whole)
        int depth = 0;
        while (peek().k != Tok::K::End) {
            const Tok& x = take();
            if (x.k != Tok::K::Sym) continue;
            if (x.s == "{") ++depth;
            else if (x.s == "}") { if (--depth < 0) { --m_i; return; } }
            else if (x.s == ";" && depth == 0) return;
        }
    }
    void skip_block() {   // NAME { ... } NAME ;
        while (peek().k != Tok::K::End && !is("{")) take();
        if (!is("{")) return;
        int depth = 0;
        do {
            const Tok& x = take();
            if (x.s == "{") ++depth;
            else if (x.s == "}") --depth;
        } while (depth > 0 && peek().k != Tok::K::End);
        if (peek().k == Tok::K::Name) take();
        if (is(";")) take();
    }

    bool check_glyph(const std::string& g, int line) {
        if (m_exists && !m_exists(g)) { error(line, "unknown glyph '" + g + "'"); return false; }
        return true;
    }

    // a - z, a.sc - z.sc: the names differ in one letter or digit.
    std::vector<std::string> range(const std::string& a, const std::string& b, int line) {
        std::vector<std::string> out;
        if (a.size() != b.size()) { error(line, "range '" + a + " - " + b + "': the ends differ in length"); return out; }
        std::size_t at = std::string::npos;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i]) {
                if (at != std::string::npos) { error(line, "range '" + a + " - " + b + "': the ends differ in more than one place"); return out; }
                at = i;
            }
        if (at == std::string::npos) return {a};
        const char c1 = a[at], c2 = b[at];
        const auto kind = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) ? 0 : std::islower(static_cast<unsigned char>(c)) ? 1 : std::isupper(static_cast<unsigned char>(c)) ? 2 : 3; };
        if (kind(c1) != kind(c2) || kind(c1) == 3 || c1 > c2) { error(line, "range '" + a + " - " + b + "' is not a run of letters or digits"); return out; }
        for (char c = c1; c <= c2; ++c) { std::string n = a; n[at] = c; out.push_back(n); }
        return out;
    }

    // One position: a glyph, @class or [ ... ].
    bool item(std::vector<std::string>& out) {
        const Tok& x = peek();
        if (x.k == Tok::K::Name) {
            take();
            if (!check_glyph(x.s, x.line)) return true;
            out.push_back(x.s);
            return true;
        }
        if (x.k == Tok::K::Class) {
            take();
            auto it = m_classes.find(x.s);
            if (it == m_classes.end()) { error(x.line, "unknown class '@" + x.s + "'"); return true; }
            out.insert(out.end(), it->second.begin(), it->second.end());
            return true;
        }
        if (x.s == "[") {
            take();
            while (peek().k != Tok::K::End && !is("]")) {
                const Tok& y = peek();
                if (y.k == Tok::K::Name && is("-", 1) && peek(2).k == Tok::K::Name) {
                    const std::string a = take().s;
                    take();
                    const std::string b = take().s;
                    for (const auto& n : range(a, b, y.line)) if (check_glyph(n, y.line)) out.push_back(n);
                    continue;
                }
                if (y.k == Tok::K::Name || y.k == Tok::K::Class) { item(out); continue; }
                error(y.line, "unexpected '" + y.s + "' in a glyph class");
                take();
            }
            expect("]");
            return true;
        }
        return false;
    }

    void class_def() {
        const Tok& c = take();   // @name
        const std::string name = c.s;
        if (!expect("=")) { skip_statement(); return; }
        std::vector<std::string> v;
        if (!item(v)) { error(peek().line, "a class is [ glyphs ] or @another"); skip_statement(); return; }
        m_classes[name] = v;
        expect(";");
    }

    // sub ... ; -> a rule (false: malformed, skipped)
    bool sub_rule(FeaRule& r) {
        const int line = take().line;   // sub / substitute
        r.line = line;
        std::vector<std::vector<std::string>> in, out;
        while (peek().k != Tok::K::End && !is("by") && !is("from") && !is(";")) {
            std::vector<std::string> v;
            if (!item(v)) { error(peek().line, "unexpected '" + peek().s + "' in a substitution"); skip_statement(); return false; }
            if (is("'")) { error(line, "contextual substitution (a ' mark) is not supported yet"); skip_statement(); return false; }
            in.push_back(std::move(v));
        }
        if (in.empty()) { error(line, "a substitution needs something to substitute"); skip_statement(); return false; }
        if (is(";")) { error(line, "'sub' without 'by' or 'from'"); take(); return false; }
        const bool from = take().s == "from";
        while (peek().k != Tok::K::End && !is(";")) {
            if (is("NULL")) { error(line, "deleting glyphs (by NULL) is not supported"); skip_statement(); return false; }
            std::vector<std::string> v;
            if (!item(v)) { error(peek().line, "unexpected '" + peek().s + "' after '" + (from ? "from" : "by") + "'"); skip_statement(); return false; }
            out.push_back(std::move(v));
        }
        expect(";");
        for (const auto& v : in) if (v.empty()) return false;   // an unknown glyph: already an error
        for (const auto& v : out) if (v.empty()) return false;
        if (from) {
            if (in.size() != 1 || in[0].size() != 1 || out.size() != 1) { error(line, "'from' takes one glyph and one class: sub a from [a.1 a.2];"); return false; }
            r.kind = FeaRule::Kind::Alternate;
            r.in = in;
            r.out = out[0];
            return true;
        }
        if (in.size() == 1 && out.size() == 1) {
            r.kind = FeaRule::Kind::Single;
            r.in = in;
            if (out[0].size() == in[0].size()) r.out = out[0];
            else if (out[0].size() == 1) r.out.assign(in[0].size(), out[0][0]);
            else { error(line, "single substitution: " + std::to_string(in[0].size()) + " glyphs to " + std::to_string(out[0].size()) + " -- the classes must match in size"); return false; }
            return true;
        }
        if (in.size() == 1 && out.size() >= 2) {
            if (in[0].size() != 1) { error(line, "multiple substitution takes one glyph (not a class) on the left"); return false; }
            for (const auto& v : out) if (v.size() != 1) { error(line, "multiple substitution: glyphs on the right, not classes"); return false; }
            r.kind = FeaRule::Kind::Multiple;
            r.in = in;
            for (const auto& v : out) r.out.push_back(v[0]);
            return true;
        }
        if (in.size() >= 2 && out.size() == 1) {
            if (out[0].size() != 1) { error(line, "a ligature substitutes ONE glyph"); return false; }
            r.kind = FeaRule::Kind::Ligature;
            r.in = in;
            r.out = out[0];
            return true;
        }
        error(line, "many glyphs by many glyphs is not a substitution OpenType has");
        return false;
    }

    // Statements of a lookup block or a feature. `feature` null = inside a named lookup.
    void body(FeaFeature* feature, int lookup) {
        int cur = -1;   // the anonymous lookup the rules are going into
        while (peek().k != Tok::K::End && !is("}")) {
            const Tok& x = peek();
            if (x.k == Tok::K::Class && is("=", 1)) { class_def(); continue; }
            if (x.k != Tok::K::Name) { error(x.line, "unexpected '" + x.s + "'"); take(); continue; }
            const std::string& w = x.s;
            if (w == "sub" || w == "substitute") {
                FeaRule r;
                if (!sub_rule(r)) continue;
                ++m_doc.rules;
                int target = lookup;
                if (feature) {
                    if (cur < 0 || m_doc.lookups[static_cast<std::size_t>(cur)].kind != r.kind) {
                        FeaLookup l;
                        l.kind = r.kind;
                        l.line = r.line;
                        m_doc.lookups.push_back(l);
                        cur = static_cast<int>(m_doc.lookups.size()) - 1;
                        feature->lookups.push_back(cur);
                    }
                    target = cur;
                }
                auto& L = m_doc.lookups[static_cast<std::size_t>(target)];
                if (!feature && !L.rules.empty() && L.kind != r.kind) {
                    error(r.line, "lookup '" + L.name + "' mixes kinds of substitution -- one lookup, one kind");
                    continue;
                }
                if (L.rules.empty()) L.kind = r.kind;
                L.rules.push_back(std::move(r));
                continue;
            }
            if (w == "lookup" && feature) {
                take();
                const Tok& n = take();
                if (is(";")) {   // a reference
                    take();
                    auto it = m_named.find(n.s);
                    if (it == m_named.end()) { error(n.line, "unknown lookup '" + n.s + "'"); continue; }
                    feature->lookups.push_back(it->second);
                    cur = -1;
                    continue;
                }
                const int li = named_lookup(n);
                if (li >= 0) feature->lookups.push_back(li);
                cur = -1;
                continue;
            }
            if (w == "rsub" || w == "reversesub" || w == "ignore") {
                error(x.line, "'" + w + "' (contextual) is not supported yet");
                skip_statement();
                continue;
            }
            if (w == "pos" || w == "position" || w == "enum" || w == "enumerate") {
                warn(x.line, "pos rules are skipped -- kerning comes from the KERN data, marks from ANCHORS");
                skip_statement();
                continue;
            }
            if (w == "script" || w == "language") {
                take();
                const Tok& v = take();
                if (v.s != "DFLT" && v.s != "latn" && v.s != "dflt")
                    warn(x.line, w + " '" + v.s + "' is skipped -- Granjon writes DFLT and latn (every feature in both)");
                skip_statement();
                continue;
            }
            if (w == "lookupflag" || w == "parameters" || w == "sizemenuname" || w == "feature" || w == "subtable") {
                warn(x.line, "'" + w + "' is skipped");
                skip_statement();
                continue;
            }
            if (w == "featureNames" || w == "cvParameters") {
                warn(x.line, "'" + w + "' is skipped");
                take();
                skip_statement();
                if (is(";")) take();
                continue;
            }
            if (w == "include") { error(x.line, "include is not supported -- paste the file in"); skip_statement(); continue; }
            error(x.line, "unexpected '" + w + "'");
            skip_statement();
        }
    }

    // lookup NAME [useExtension] { ... } NAME ;   (the NAME token taken)
    int named_lookup(const Tok& n) {
        if (is("useExtension")) take();
        if (m_named.count(n.s)) { error(n.line, "lookup '" + n.s + "' is defined twice"); skip_block(); return -1; }
        if (!expect("{")) { skip_statement(); return -1; }
        FeaLookup l;
        l.name = n.s;
        l.line = n.line;
        m_doc.lookups.push_back(l);
        const int li = static_cast<int>(m_doc.lookups.size()) - 1;
        m_named[n.s] = li;
        body(nullptr, li);
        expect("}");
        if (peek().s != n.s) error(peek().line, "lookup '" + n.s + "' ends with '" + peek().s + "'");
        else take();
        expect(";");
        if (m_doc.lookups[static_cast<std::size_t>(li)].rules.empty()) warn(n.line, "lookup '" + n.s + "' is empty");
        return li;
    }

    void top() {
        const Tok& x = peek();
        if (x.k == Tok::K::Class && is("=", 1)) { class_def(); return; }
        if (x.k != Tok::K::Name) { error(x.line, "unexpected '" + x.s + "'"); take(); return; }
        const std::string w = x.s;
        if (w == "languagesystem") { skip_statement(); return; }
        if (w == "feature") {
            take();
            const Tok& tag = take();
            if (tag.k != Tok::K::Name || tag.s.size() > 4 || tag.s.empty()) { error(tag.line, "a feature tag is 1-4 letters"); skip_block(); return; }
            if (is("useExtension")) take();
            if (!expect("{")) { skip_statement(); return; }
            FeaFeature f;
            f.tag = tag.s;
            while (f.tag.size() < 4) f.tag += ' ';
            f.line = tag.line;
            body(&f, -1);
            expect("}");
            if (peek().s != tag.s) error(peek().line, "feature '" + tag.s + "' ends with '" + peek().s + "'");
            else take();
            expect(";");
            if (!f.lookups.empty()) m_doc.features.push_back(std::move(f));
            return;
        }
        if (w == "lookup") {
            take();
            const Tok& n = take();
            named_lookup(n);
            return;
        }
        if (w == "table" || w == "anonymous" || w == "anon") {
            warn(x.line, "'" + w + "' block is skipped");
            skip_block();
            return;
        }
        if (w == "include") { error(x.line, "include is not supported -- paste the file in"); skip_statement(); return; }
        error(x.line, "unexpected '" + w + "' at the top level");
        skip_statement();
    }
};

}  // namespace

FeaDoc parse_fea(const std::string& text, const std::function<bool(const std::string&)>& exists) {
    return Parser(text, exists).run();
}

// ── generate ───────────────────────────────────────────────────────────────

std::string generate_fea(const Font& f) {
    std::set<std::string> names;
    std::map<char32_t, std::string> by_cp;
    for (const auto& n : f.glyph_order()) {
        const Glyph* g = f.glyph(n);
        if (!g || g->skip_export || n == ".notdef" || n.empty() || n[0] == '_') continue;
        names.insert(n);
        for (char32_t cp : g->unicodes) by_cp.emplace(cp, n);
    }
    std::map<std::string, std::vector<std::string>> feat;   // tag -> rule lines
    std::map<std::string, std::vector<std::string>> salt;   // base -> its alternates
    // ligatures
    for (const auto& n : names) {
        if (n.find('_') == std::string::npos || n.find('.') != std::string::npos) continue;
        std::vector<std::string> parts;
        std::size_t a = 0;
        for (std::size_t b; (b = n.find('_', a)) != std::string::npos; a = b + 1) parts.push_back(n.substr(a, b - a));
        parts.push_back(n.substr(a));
        if (parts.size() < 2) continue;
        bool all = true;
        for (const auto& p : parts) all = all && !p.empty() && names.count(p);
        if (!all) continue;
        std::string line = "    sub";
        for (const auto& p : parts) line += " " + p;
        feat["liga"].push_back(line + " by " + n + ";");
    }
    static const std::map<std::string, std::string> kSuffix = {
        {"sc", "smcp"}, {"onum", "onum"}, {"lnum", "lnum"}, {"tnum", "tnum"}, {"pnum", "pnum"},
        {"sups", "sups"}, {"subs", "subs"}, {"sinf", "sinf"}, {"numr", "numr"}, {"dnom", "dnom"},
        {"case", "case"}, {"swsh", "swsh"}};
    for (const auto& n : names) {
        const auto dot = n.rfind('.');
        if (dot == std::string::npos || dot == 0) continue;
        const std::string base = n.substr(0, dot), suf = n.substr(dot + 1);
        if (!names.count(base)) continue;
        if (auto it = kSuffix.find(suf); it != kSuffix.end()) {
            feat[it->second].push_back("    sub " + base + " by " + n + ";");
            if (suf == "sc")
                if (const Glyph* g = f.glyph(base); g && !g->unicodes.empty()) {
                    const char32_t cp = g->unicodes.front();
                    const auto up = static_cast<char32_t>(std::towupper(static_cast<wint_t>(cp)));
                    if (up != cp)
                        if (auto u = by_cp.find(up); u != by_cp.end()) feat["c2sc"].push_back("    sub " + u->second + " by " + n + ";");
                }
            continue;
        }
        if (suf.size() == 4 && suf.rfind("ss", 0) == 0 && std::isdigit(static_cast<unsigned char>(suf[2])) &&
            std::isdigit(static_cast<unsigned char>(suf[3])) && suf != "ss00" && std::stoi(suf.substr(2)) <= 20) {
            feat[suf].push_back("    sub " + base + " by " + n + ";");
            continue;
        }
        if (suf == "salt" || suf == "alt" || (suf.rfind("alt", 0) == 0 && suf.size() > 3 &&
                                              std::all_of(suf.begin() + 3, suf.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })))
            salt[base].push_back(n);
    }
    for (const auto& [base, alts] : salt) {
        if (alts.size() == 1) feat["salt"].push_back("    sub " + base + " by " + alts[0] + ";");
        else {
            std::string l = "    sub " + base + " from [";
            for (std::size_t i = 0; i < alts.size(); ++i) l += (i ? " " : "") + alts[i];
            feat["salt"].push_back(l + "];");
        }
    }
    if (feat.empty()) return {};
    std::string out = std::string(kFeaAutoBegin) + "\nlanguagesystem DFLT dflt;\nlanguagesystem latn dflt;\n";
    // a sensible order: substitutions people turn on first, then the rest by tag
    std::vector<std::string> order = {"liga", "smcp", "c2sc", "case", "onum", "lnum", "pnum", "tnum",
                                      "sups", "subs", "sinf", "numr", "dnom", "salt", "swsh"};
    for (const auto& [tag, _] : feat)
        if (std::find(order.begin(), order.end(), tag) == order.end()) order.push_back(tag);
    for (const auto& tag : order) {
        auto it = feat.find(tag);
        if (it == feat.end()) continue;
        out += "\nfeature " + tag + " {\n";
        if (tag == "salt") {   // singles and sets are different lookup kinds: singles first
            for (const auto& l : it->second) if (l.find(" from ") == std::string::npos) out += l + "\n";
            for (const auto& l : it->second) if (l.find(" from ") != std::string::npos) out += l + "\n";
        } else {
            for (const auto& l : it->second) out += l + "\n";
        }
        out += "} " + tag + ";\n";
    }
    out += std::string(kFeaAutoEnd) + "\n";
    return out;
}

std::string replace_auto_block(const std::string& text, const std::string& generated) {
    const auto a = text.find(kFeaAutoBegin);
    const auto b = a == std::string::npos ? std::string::npos : text.find(kFeaAutoEnd, a);
    if (a != std::string::npos && b != std::string::npos) {
        std::size_t end = b + std::string(kFeaAutoEnd).size();
        if (end < text.size() && text[end] == '\n') ++end;
        if (generated.empty() && end < text.size() && text[end] == '\n') ++end;   // and the blank line Generate put after it
        return text.substr(0, a) + generated + text.substr(end);
    }
    if (generated.empty()) return text;
    if (text.empty()) return generated;
    return generated + "\n" + text;
}

}  // namespace granjon::model
