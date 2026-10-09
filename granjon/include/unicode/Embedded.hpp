#pragma once
// unicode::embedded -- the data files compiled into granjon_core at build
// time (tools/embed.cpp; the list is in CMakeLists.txt): the trimmed UCD,
// the AGL / AGLFN, the char sets. Keys are paths under resources/, e.g.
// "ucd/ucd.txt", "charsets/latin-1.txt". GTK- and GLib-free.
#include <cstddef>
#include <string_view>

namespace granjon::unicode::embedded {

struct File {
    const char* key;
    const char* data;
    std::size_t size;
    std::string_view view() const { return {data, size}; }
};

extern const File        kFiles[];
extern const std::size_t kFileCount;

// The file under `key`, or an empty view.
inline std::string_view get(std::string_view key) {
    for (std::size_t i = 0; i < kFileCount; ++i)
        if (key == kFiles[i].key) return kFiles[i].view();
    return {};
}

}  // namespace granjon::unicode::embedded
