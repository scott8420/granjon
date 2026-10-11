#pragma once
// io::Ufo -- the native source format: a UFO 3 folder (we name it
// Name.granjon; Name.ufo works the same and other tools read either).
//
//   metainfo.plist      creator io.github.granjon.Granjon, formatVersion 3
//   fontinfo.plist      names, version, license, vendor, UPM, vertical metrics
//   groups.plist        kerning classes
//   kerning.plist       pairs: first -> { second -> value }
//   features.fea        verbatim (only when there is some)
//   lib.plist           public.glyphOrder; granjon.* (metrics keys, overshoot)
//   layercontents.plist the one layer, public.default -> glyphs
//   glyphs/contents.plist + *.glif
//   images/*.png        background images (M13) -- only the ones a glyph places
//
// Saving writes a complete new folder beside the target and swaps it in, so a
// failed save never leaves a half-written font, and glyphs deleted since the
// last save don't linger as stale .glif files. Output is deterministic: the
// same font saves byte-identical.
//
// Loading reads everything into a fresh Font first and only then replaces
// the open one (under Font::Loading -- not an undoable step), so a bad file
// leaves the open font untouched. Errors name the file and say what's wrong.
#include "model/Font.hpp"

#include <string>

namespace granjon::io {

bool save_ufo(const model::Font& f, const std::string& path, std::string* err = nullptr);
bool load_ufo(const std::string& path, model::Font& into, std::string* err = nullptr);

// UFO 3's user-name-to-file-name rule: uppercase letters get "_" after them,
// illegal and leading '.' characters become "_", reserved names are
// prefixed, and a clash (case-insensitively) gets a numeric suffix.
std::string glif_file_name(const std::string& glyph, const std::vector<std::string>& taken_lower);

}  // namespace granjon::io
