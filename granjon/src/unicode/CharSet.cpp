#include "unicode/CharSet.hpp"

#include "unicode/Embedded.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>

namespace granjon::unicode {

namespace {

CharSet parse(std::string id, std::string_view data) {
    CharSet s;
    s.id = std::move(id);
    s.name = s.id;
    std::size_t a = 0;
    while (a < data.size()) {
        std::size_t b = data.find('\n', a);
        if (b == std::string_view::npos) b = data.size();
        std::string_view line = data.substr(a, b - a);
        a = b + 1;
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (line.starts_with("# name: "))   s.name   = std::string(line.substr(8));
            if (line.starts_with("# source: ")) s.source = std::string(line.substr(10));
            continue;
        }
        unsigned v = 0;
        const auto end = line.find_first_of(" \t");
        const std::string_view h = line.substr(0, end);
        if (std::from_chars(h.data(), h.data() + h.size(), v, 16).ec == std::errc{}) s.cps.push_back(v);
    }
    std::sort(s.cps.begin(), s.cps.end());
    s.cps.erase(std::unique(s.cps.begin(), s.cps.end()), s.cps.end());
    return s;
}

}  // namespace

const std::vector<CharSet>& charsets() {
    static const std::vector<CharSet> all = [] {
        std::vector<CharSet> v;
        for (std::size_t i = 0; i < embedded::kFileCount; ++i) {
            const std::string_view key = embedded::kFiles[i].key;
            if (!key.starts_with("charsets/") || !key.ends_with(".txt")) continue;
            v.push_back(parse(std::string(key.substr(9, key.size() - 13)), embedded::kFiles[i].view()));
        }
        std::stable_sort(v.begin(), v.end(), [](const CharSet& a, const CharSet& b) { return a.cps.size() < b.cps.size(); });
        return v;
    }();
    return all;
}

const CharSet* charset(const std::string& id) {
    for (const auto& s : charsets())
        if (s.id == id) return &s;
    return nullptr;
}

}  // namespace granjon::unicode
