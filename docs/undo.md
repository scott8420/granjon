# Undo -- every user choice is a step, through one door

> "In Folio and Curvz we got to a point that the discussion on undo was
> important. But, we should integrate a system for undo early in a project
> and make any user choice an undoable event." -- Scott, jot s046

**Compiled and exercised here:** `include/core/Undo.hpp` (the journal),
`core/Stones.*` (a demo model + its verbs + the verb table), the gate and the
undo tests in `cairn_selftest`, and the **Undo** page in the app.

## When

**With the model, in the first milestone that writes anything.** Before a
second surface can change the data. Three projects reached undo late:

- **Curvz** -- one command class per verb (~65 of them, a 4000-line header).
  Group / ungroup, layer ops and palette edits were never undoable until a
  scripting arc needed them; each was its own retrofit milestone (s242 m1,
  s243 m1). Commands held raw `SceneNode*` and needed a scrub pass when a
  node died.
- **jot** -- undo arrived at s045 for the tree's verbs; s046 is spent finding
  the dozen other doors (Note details fields, card ticks, Inbox verbs,
  captures, imports) that were built without it.
- **Folio** -- the undo discussion came when it was already expensive.

The journal is ~250 lines. The retrofit is the cost.

## The shape

```
 user choice -> verb (core) -> Journal::run(label, ids, op) -> store.put(...)
                                   |  before: store.get(ids)
                                   |  op()
                                   |  after:  store.get(ids + created)
                                   v
                              Step { label, entries[id, before, after], selection }
 Ctrl+Z  -> put every BEFORE back (backwards)      Ctrl+Shift+Z -> every AFTER (forwards)
```

1. **A step is two pictures, not an inverse.** No per-verb `undo()` to write.
   A verb is undoable the day it goes through `run()`. (Curvz's own
   `BooleanOpCommand` -- clone the originals and the results, swap them whole
   -- was this shape, and Curvz called it "the template for future
   structural ops".)
2. **By id and by value, never by pointer.** Nothing a step holds can dangle.
3. **The record's place is in the value.** Order (`pos`), parent, index --
   whatever says *where* it is. Putting the old value back puts it back
   *there*: delete + undo lands in the same slot under the same id.
4. **One door, and a gate.** The store's `put()` asks `journal.open()` and
   counts a write made with the door shut. The selftest walks
   `stones::verbs()` -- every verb the UI offers -- and fails on a stray, on a
   verb that is not a step, on an undo that does not put it back exactly, on
   a redo that does not reproduce it. Mutation-checked: a rename that skips
   the door fails 7 checks.
5. **Loading is not a user choice.** `Stones::Loading` says so, by scope.

## What the journal adds (each from a real project)

| Need | API | Where it came from |
|---|---|---|
| A drag is ONE step | `begin()` at press, writes, `commit()` at release; `cancel()` on Escape; Ctrl+Z mid-drag commits first | Curvz: one `EditPathCommand` per path, pushed on drag-end |
| Key repeats / typing fold together | `run(..., merge_key)` inside `merge_window` (1500 ms); never into an undone step; there-and-back is no step | Curvz: inspector edits coalesce per object, 1500 ms |
| A no-op is not a step | before == after -> dropped | jot s045 |
| Selection comes back with the step | `set_selection_source`; `Result::selection`; `reselect()` when the UI moves it after the verb (a delete selects the neighbour) | Curvz `preserves_selection()`; jot s046a |
| Menus name the step | `undo_label()` / `redo_label()`; `set_on_change` to re-label and grey | jot s045 ("Undo Delete “Taxes”") |
| A rebuild must not record | the repaint is not a verb; fields set by a repaint are guarded (`m_painting`) | Curvz `m_suppress_push` |

## The keys -- the part that bit jot

- **Not an accelerator.** A `set_accels_for_action` chord fires before the
  focused widget, so Ctrl+Z as an accel takes undo away from every Entry and
  TextView. The registry carries Ctrl+Z / Ctrl+Shift+Z as **doc-only rows**.
- **Not bound to one widget's focus.** jot s045 bound Ctrl+Z to the tree. A
  delete from a menu left the keyboard on the closed menu and Ctrl+Z reached
  nobody -- Scott's s046 report was exactly "undo a delete".
- **The window hears it in the BUBBLE phase** -- only keys nothing else took
  -- and steps aside when the focus is in a text box (`focus_is_text()`),
  which keeps its own undo even when it had nothing to undo.
- After a verb that removes the focused thing, **give the keyboard back**
  (the neighbour's row) after the repaint, so the next key lands.
- `activate_action("win.undo")` -- the full name. `"undo"` alone does nothing
  and says nothing (jot s046a, found by driving it).

## Where a real app goes further

- **Part of the record has its own undo** (a note body in a TextView): split
  it out of the Value, or restore only the fields that differ between the
  step's two pictures, so undoing a rename does not also undo typing done
  since (jot `core::apply` with `other`).
- **Trees:** record subtrees parent-first; undo walks backwards, so children
  are removed before parents and put back after them.
- **Documents:** `clear()` on switching documents -- ids of the old one mean
  nothing in the new one.
- **History across sessions:** not here, deliberately. Saved undo is a file
  format.

## Try it (the Undo page)

1. Select Jade, **Delete**. Onyx is selected. **Ctrl+Z** without clicking:
   Jade is back, in its place, selected.
2. Drag the size slider end to end; **Ctrl+Z** once: back.
3. Click the slider, tap Right four times; **Ctrl+Z** once: back.
4. Rename in the field, Enter. Ctrl+Z *in the field* is the field's own; click
   the list, Ctrl+Z: the rename is undone.
5. Menu: "Undo Rename “Onyx”" -- the step named; greyed when there is none.
