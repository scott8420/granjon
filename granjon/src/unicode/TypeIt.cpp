// unicode::TypeIt (s08) -- see the header.
#include "unicode/TypeIt.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace granjon::unicode {

namespace {

// UTF-8 -> code points (enough for one Compose result).
std::vector<char32_t> decode(const std::string& s) {
    std::vector<char32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > s.size()) return {};
        char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += static_cast<std::size_t>(n);
    }
    return out;
}

// A keysym name as the key it is ("" = not one we can spell).
std::string spell(const std::string& k) {
    static const std::map<std::string, std::string> names = {
        {"space", "Space"}, {"exclam", "!"}, {"quotedbl", "\""}, {"numbersign", "#"}, {"dollar", "$"},
        {"percent", "%"}, {"ampersand", "&"}, {"apostrophe", "'"}, {"parenleft", "("}, {"parenright", ")"},
        {"asterisk", "*"}, {"plus", "+"}, {"comma", ","}, {"minus", "-"}, {"period", "."}, {"slash", "/"},
        {"colon", ":"}, {"semicolon", ";"}, {"less", "<"}, {"equal", "="}, {"greater", ">"}, {"question", "?"},
        {"at", "@"}, {"bracketleft", "["}, {"backslash", "\\"}, {"bracketright", "]"}, {"asciicircum", "^"},
        {"underscore", "_"}, {"grave", "`"}, {"braceleft", "{"}, {"bar", "|"}, {"braceright", "}"},
        {"asciitilde", "~"},
    };
    if (k.size() == 1 && std::isalnum(static_cast<unsigned char>(k[0]))) return k;
    if (auto it = names.find(k); it != names.end()) return it->second;
    return "";
}

}  // namespace

std::map<char32_t, std::string> parse_compose(const std::string& text) {
    std::map<char32_t, std::string> best;
    std::map<char32_t, int> len;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("<Multi_key>", 0) != 0) continue;
        const auto colon = line.find(':');
        const auto q0 = line.find('"', colon == std::string::npos ? 0 : colon);
        if (colon == std::string::npos || q0 == std::string::npos) continue;
        std::string val;
        std::size_t i = q0 + 1;
        for (; i < line.size() && line[i] != '"'; ++i) {
            if (line[i] == '\\' && i + 1 < line.size()) ++i;
            val += line[i];
        }
        if (i >= line.size()) continue;
        const auto cps = decode(val);
        if (cps.size() != 1) continue;
        // the keys between <Multi_key> and the colon
        std::string seq = "Compose";
        int n = 0;
        bool ok = true;
        std::size_t p = std::string("<Multi_key>").size();
        while (ok) {
            const auto a = line.find('<', p);
            if (a == std::string::npos || a > colon) break;
            const auto b = line.find('>', a);
            if (b == std::string::npos || b > colon) { ok = false; break; }
            const std::string k = line.substr(a + 1, b - a - 1);
            std::string s = spell(k);
            if (s.empty() && k.size() >= 5 && k[0] == 'U' && std::isxdigit(static_cast<unsigned char>(k[1])))
                s = "U+" + k.substr(1);
            if (s.empty()) ok = false;
            seq += " " + s;
            ++n;
            p = b + 1;
        }
        if (!ok || n == 0) continue;
        if (auto it = len.find(cps[0]); it == len.end() || n < it->second) {
            len[cps[0]] = n;
            best[cps[0]] = seq;
        }
    }
    return best;
}

const std::map<char32_t, std::string>& system_compose() {
    static const std::map<char32_t, std::string> table = [] {
        for (const char* path : {"/usr/share/X11/locale/en_US.UTF-8/Compose", "/usr/X11R6/lib/X11/locale/en_US.UTF-8/Compose"}) {
            std::ifstream f(path);
            if (!f) continue;
            std::stringstream ss;
            ss << f.rdbuf();
            return parse_compose(ss.str());
        }
        return std::map<char32_t, std::string>{};
    }();
    return table;
}

TypeIt type_it(char32_t cp, const std::map<char32_t, std::string>& compose) {
    TypeIt t;
    char buf[64];
    if (cp >= 0x20 && cp < 0x7F) {
        static const std::string shifted = "~!@#$%^&*()_+{}|:\"<>?";
        static const std::string base    = "`1234567890-=[]\\;',./";
        const char c = static_cast<char>(cp);
        if (c == ' ') t.key = "Space";
        else if (std::isupper(static_cast<unsigned char>(c))) t.key = std::string("Shift+") + c;
        else if (const auto k = shifted.find(c); k != std::string::npos) t.key = std::string("Shift+") + base[k];
        else t.key = std::string(1, c);
    }
    if (auto it = compose.find(cp); it != compose.end()) t.compose = it->second;
    std::snprintf(buf, sizeof buf, "%x", static_cast<unsigned>(cp));
    t.hex = std::string("Ctrl+Shift+U ") + buf + " Space";
    if (cp >= 0xA0 && cp < 0x100) {   // Alt+0nnn is Windows-1252: equal to Unicode from A0 up
        std::snprintf(buf, sizeof buf, "Alt+0%u (numeric keypad)", static_cast<unsigned>(cp));
        t.windows = buf;
    } else if (cp >= 0x80) {
        std::snprintf(buf, sizeof buf, "%04X then Alt+X (Word, WordPad)", static_cast<unsigned>(cp));
        t.windows = buf;
    }
    if (cp >= 0x80 && cp <= 0xFFFF) {
        std::snprintf(buf, sizeof buf, "Option+%04X (Unicode Hex Input)", static_cast<unsigned>(cp));
        t.mac = buf;
    }
    return t;
}

}  // namespace granjon::unicode
