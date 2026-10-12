#include "views/Arrange.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace granjon::views {

std::set<int> picked_contours(const ViewContext& ctx) {
    std::set<int> cs;
    const model::Glyph* g = ctx.doc.font.glyph(ctx.selected);
    if (!g) return cs;
    for (const auto& p : ctx.pick)
        if (p.contour >= 0 && p.contour < static_cast<int>(g->contours.size())) cs.insert(p.contour);
    return cs;
}

void arrange_picked(ViewContext& ctx, command::Arrange how) {
    const std::set<int> cs = picked_contours(ctx);
    std::vector<int> at;
    std::string why;
    if (!command::arrange_contours(ctx.doc.journal, ctx.doc.font, ctx.selected, {cs.begin(), cs.end()}, how, &at, &why)) {
        if (ctx.say) ctx.say(why, false);
        return;
    }
    // The picked paths keep their own order, so the k-th picked old index
    // is the k-th new one; the points picked inside them stay picked.
    std::map<int, int> to;
    std::size_t k = 0;
    for (int c : cs) to[c] = at[k++];
    geom::Points moved;
    for (auto p : ctx.pick) {
        if (p.contour >= 0) {
            const auto it = to.find(p.contour);
            if (it == to.end()) continue;   // (none: only picked contours have picked points)
            p.contour = it->second;
        }
        moved.insert(p);
    }
    ctx.pick = std::move(moved);
    ctx.pick_changed.emit();
    if (ctx.say) ctx.say(ctx.doc.journal.undo_label() + (cs.size() == 1 ? ": 1 path." : ": " + std::to_string(cs.size()) + " paths."), false);
}

namespace {
void copy_picked(ViewContext& ctx, double dx, double dy) {
    const std::set<int> cs = picked_contours(ctx);
    std::vector<int> at;
    std::string why;
    if (!command::duplicate_contours(ctx.doc.journal, ctx.doc.font, ctx.selected, {cs.begin(), cs.end()}, dx, dy, &at, &why)) {
        if (ctx.say) ctx.say(why, false);
        return;
    }
    ctx.pick.clear();
    if (const model::Glyph* g = ctx.doc.font.glyph(ctx.selected))
        for (int ci : at)
            for (int i = 0; i < static_cast<int>(g->contours[static_cast<std::size_t>(ci)].nodes.size()); ++i)
                ctx.pick.insert({ci, i, geom::PointRef::Part::Node});
    ctx.pick_changed.emit();
    if (!ctx.say) return;
    const std::string n = at.size() == 1 ? "1 path" : std::to_string(at.size()) + " paths";
    if (dx == 0 && dy == 0) ctx.say("Duplicated " + n + " in place, on top -- the copy is picked: drag or nudge it.", false);
    else ctx.say("Cloned " + n + ", offset " + std::to_string(static_cast<int>(dx)) + " right and down, on top -- picked.", false);
}
}  // namespace

double clone_offset(const ViewContext& ctx) {
    return std::max(1.0, std::round(ctx.doc.font.info().units_per_em * 0.02));
}

void duplicate_picked(ViewContext& ctx) { copy_picked(ctx, 0, 0); }

void clone_picked(ViewContext& ctx) {
    const double o = clone_offset(ctx);
    copy_picked(ctx, o, -o);
}

}  // namespace granjon::views
