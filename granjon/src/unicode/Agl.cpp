#include "unicode/Agl.hpp"

#include "unicode/Embedded.hpp"

#include <charconv>
#include <cstdio>
#include <map>
#include <unordered_map>

namespace granjon::unicode {

namespace {

template <class Fn>
void each_line(std::string_view data, Fn&& fn) {
    std::size_t a = 0;
    while (a < data.size()) {
        std::size_t b = data.find('\n', a);
        if (b == std::string_view::npos) b = data.size();
        std::string_view line = data.substr(a, b - a);
        a = b + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line[0] == '#') continue;
        fn(line);
    }
}

bool parse_hex(std::string_view s, char32_t& out) {
    unsigned v = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v, 16);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) return false;
    out = static_cast<char32_t>(v);
    return true;
}

bool upper_hex(std::string_view s) {
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
    return !s.empty();
}

bool scalar(char32_t v) { return v <= 0x10FFFF && !(v >= 0xD800 && v <= 0xDFFF); }

// cp -> AGLFN name
const std::map<char32_t, std::string_view>& aglfn() {
    static const auto m = [] {
        std::map<char32_t, std::string_view> m;
        each_line(embedded::get("ucd/aglfn.txt"), [&](std::string_view line) {
            const auto a = line.find(';'), b = line.find(';', a + 1);
            char32_t cp;
            if (a != std::string_view::npos && b != std::string_view::npos && parse_hex(line.substr(0, a), cp))
                m.emplace(cp, line.substr(a + 1, b - a - 1));
        });
        return m;
    }();
    return m;
}

// AGL name -> codepoints
const std::unordered_map<std::string_view, std::vector<char32_t>>& agl() {
    static const auto m = [] {
        std::unordered_map<std::string_view, std::vector<char32_t>> m;
        each_line(embedded::get("ucd/glyphlist.txt"), [&](std::string_view line) {
            const auto semi = line.find(';');
            if (semi == std::string_view::npos) return;
            std::vector<char32_t> cps;
            std::string_view rest = line.substr(semi + 1);
            std::size_t a = 0;
            while (a < rest.size()) {
                std::size_t b = rest.find(' ', a);
                if (b == std::string_view::npos) b = rest.size();
                char32_t cp;
                if (parse_hex(rest.substr(a, b - a), cp)) cps.push_back(cp);
                a = b + 1;
            }
            m.emplace(line.substr(0, semi), std::move(cps));
        });
        return m;
    }();
    return m;
}

}  // namespace

std::string_view aglfn_name(char32_t cp) {
    const auto& m = aglfn();
    const auto it = m.find(cp);
    return it == m.end() ? std::string_view{} : it->second;
}

std::string nice_name(char32_t cp) {
    if (auto n = aglfn_name(cp); !n.empty()) return std::string(n);
    char buf[16];
    if (cp <= 0xFFFF) std::snprintf(buf, sizeof buf, "uni%04X", static_cast<unsigned>(cp));
    else              std::snprintf(buf, sizeof buf, "u%05X", static_cast<unsigned>(cp));
    return buf;
}

std::vector<char32_t> codepoints_for_name(std::string_view name) {
    std::vector<char32_t> out;
    if (const auto dot = name.find('.'); dot != std::string_view::npos) name = name.substr(0, dot);
    std::size_t a = 0;
    while (a <= name.size()) {
        std::size_t b = name.find('_', a);
        if (b == std::string_view::npos) b = name.size();
        const std::string_view part = name.substr(a, b - a);
        a = b + 1;
        if (part.empty()) { if (b == name.size()) break; continue; }
        if (const auto it = agl().find(part); it != agl().end()) {
            out.insert(out.end(), it->second.begin(), it->second.end());
        } else if (part.size() >= 7 && part.starts_with("uni") && (part.size() - 3) % 4 == 0 &&
                   upper_hex(part.substr(3))) {
            for (std::size_t i = 3; i < part.size(); i += 4) {
                char32_t cp;
                if (parse_hex(part.substr(i, 4), cp) && scalar(cp)) out.push_back(cp);
            }
        } else if (part.size() >= 5 && part.size() <= 7 && part[0] == 'u' && upper_hex(part.substr(1))) {
            char32_t cp;
            if (parse_hex(part.substr(1), cp) && scalar(cp)) out.push_back(cp);
        }
        if (b == name.size()) break;
    }
    return out;
}

}  // namespace granjon::unicode
