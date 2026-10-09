# The two-layer seam — proven three ways

The most re-litigated architectural claim is "keep the core free of the UI."
It's abstract until you've seen it pay off, so a future you is tempted to let a
widget type leak into the core "just here." This page is the evidence that the
seam is real, plus the two disciplines that keep it real.

## The seam

One rule: **the core is reached only from the UI side, and never reaches back.**
The core is pure data + operations (no gtkmm, no gdk). The UI constructs it,
resolves paths, calls it, and paints. In Cairn:

- `cairn_core` (the `core::` namespace) links **no gtkmm** — enforced by the
  build graph, so a stray UI include under `core/` fails to compile.
- because it's UI-free, it's **headless-testable**: `cairn_selftest` runs the
  pump and the mapping with no display (`87 pass / 0 fail` as of the undo stone). Headless-testability
  and UI-portability are the *same* property; you keep the seam for the former
  and get the latter free.

## The evidence: one engine, three heads

Folio's `folioedit/` engine is the proof this isn't theory. The **same pure
engine** runs under three different heads:

- `folioedit/cli/main.cpp` — a command-line head;
- `folioedit/tui/Tui.cpp` — an FTXUI terminal head;
- Folio's GTK app in `src/` — the desktop head.

The engine links none of them (its only "gtk" mention is a comment in `TextMap`
saying it deliberately has no FTXUI), and it ships **16 headless tests**
(`folioedit/tests/TEST_*.cpp`). Swapping the head rewrites the entire upper
layer and leaves the engine untouched. That is the seam, demonstrated — not
argued.

## Discipline 1 — the core owns its serializable shape

Folio's `Format` is the sharp version: the engine defines its OWN `Document` /
`Annotation` structs and its own `to_json` / `from_json`, **deliberately not**
the GTK app's `DocumentModel::Annotation`. The engine never links the view
model; the app's import glue maps between the two shapes at the boundary. A
serialize routine that reaches into the UI's view model has punched a hole in the
seam. Cairn's `core::Recents` pump is the same shape in miniature: the core owns
the on-disk JSON, the UI hands it a path.

Corollary worth stealing: Folio's `Format` **omits fields that hold their default**
(a fresh annotation's verdict/resolution) so an older file still hashes
byte-identically — no format bump. Round-trip fidelity includes *not gratuitously
rewriting* what you load.

## Discipline 2 — round-trip fidelity is the test the seam makes cheap

Because the core is headless, its contracts are asserted with plain asserts, no
display:

- **Recents** — save → load returns the same list (and prunes what vanished).
- **TextMap** — the forward mapping (a selection → a codepoint range) and the
  inverse (`map_range`, a codepoint range → line/col) are **exact inverses**:
  the selftest maps forward, maps back, and asserts byte-exact endpoints across
  single-line, soft-wrap, cross-paragraph, and UTF-8 spans. This is the exact
  problem an editor overlay or a click-to-word hit map has — and proving it in
  isolation is what lets you build the fragile custom widget on data you already
  trust (DESIGN's Option-2: prove the data, then build the widget).

The seam isn't a tax. It's what makes both disciplines cheap enough to actually
keep.
