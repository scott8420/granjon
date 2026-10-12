#pragma once
// otf::Layout (M16) -- the OpenType layout tables' common frame, shared by
// GPOS (kern, mark, mkmk) and GSUB (the features editor): coverage and class
// definitions, and the ScriptList / FeatureList / LookupList around a set of
// lookups. Scripts: DFLT and latn, each a default language system holding
// every feature.
#include "export/Sfnt.hpp"

#include <string>
#include <utility>
#include <vector>

namespace granjon::otf {

Bytes coverage(std::vector<int> gids);                            // format 1, sorted, unique
Bytes class_def(std::vector<std::pair<int, int>> gid_class);      // format 2; class 0 left out

struct Lookup {
    int                type = 0;      // GPOS 1..8 / GSUB 1..6
    int                flag = 0;
    std::vector<Bytes> subtables;
};
struct Feature {
    std::string      tag;             // "kern", "mark", "liga" ...
    std::vector<int> lookups;         // indices into the lookup list
};
// The whole table (GPOS or GSUB, version 1.0). `extension_type` is 9 (GPOS) or
// 7 (GSUB): when the lookups pass 64 KB together every subtable goes through
// an Extension, its data after the lookups. Features are written sorted by
// tag, as the spec asks. Empty when there are no lookups.
Bytes layout_table(std::vector<Feature> features, const std::vector<Lookup>& lookups, int extension_type);

}  // namespace granjon::otf
