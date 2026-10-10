#pragma once
// otf::Woff2 -- M10b: WOFF2 (the web font): the TrueType compile, Brotli-
// compressed (libbrotlienc; Fedora brotli-devel). See src/export/Woff2.cpp.
#include "export/Otf.hpp"

namespace granjon::otf {

// Any sfnt (TTF or OTF bytes) -> WOFF2 bytes; empty on failure (`why` says).
Bytes woff2_from_sfnt(const Bytes& sfnt, std::string* why = nullptr);
// The font as WOFF2 -- TrueType-flavoured, as the web fonts services ship it.
Bytes compile_woff2(const model::Font& f, const OtfOptions& opt = {}, OtfReport* rep = nullptr);
bool  export_woff2(const model::Font& f, const std::filesystem::path& to, const OtfOptions& opt = {},
                   OtfReport* rep = nullptr, std::string* why = nullptr);

}  // namespace granjon::otf
