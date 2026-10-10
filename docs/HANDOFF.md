# Granjon handoff — s03 -> s04

# s04_spacing_strip

## Files to upload
- `s04_spacing_strip.zip` (made by `scripts/granjon_backup.sh -o s04_spacing_strip`)
- `RULES.md`, `HANDOFF.md`, `ARCHITECTURE.md`, `ARC.md`, `CANON.md`
- No Curvz source needed (its geometry is in `geom/`; `BooleanOpsClipper` returns at M10).
- Test fonts live in the project's `fonts/` folder (outside the repo): `My-Termes-Regular.granjon` (TeX Gyre Termes, 1090 glyphs). Upload it if the session should look at spacing on a real font.

---

## Status — s03 ends at M5c (awaiting Scott's compile/run report)
s03 ran M4 editor, M4b preferences, M5 components + anchors, M5b precision, navigator folds, navigator pick -> overview -> tabs (all crc), and M5c guides / rulers / align / measure (this report). Built against Ubuntu 24.04, g++ 13.3, gtkmm 4.10 / GTK 4.14, libxml2 2.9.14, spdlog 1.12. Clean, no warnings. Selftest 507 / 0. **No new package dependency.**

What to look at in M5c:
- **Rulers** along the editor's top and left. **Drag out of the top ruler** for a horizontal guide, **the left ruler** for a vertical one. They belong to this glyph (teal).
- **Click a guide** to pick it: the GUIDE card shows its position, angle, name; **Whole font** makes it a font-wide guide (violet, on every glyph). Drag it to move (it snaps); **drop it on a ruler** or press Delete to remove it. Saved in the UFO (glyph guidelines + fontinfo guidelines).
- **Snap** now also catches straight guides.
- **Align / distribute:** pick two or more nodes or anchors -- the PICK card shows Left / Centre / Right / ↔ and Top / Middle / Bottom / ↕ (↔ / ↕ space them evenly).
- **Measure: hold M.** Drag for distance, angle and delta (the ends snap to nodes); click for the stem thickness through that point, across and up. Let go of M to go back to editing.

**M5d contour direction** (asked after M5c): arrows on every contour; a contour running the wrong way (CFF: outers counter-clockwise, counters clockwise) is red with a note on the canvas. Inspector OUTLINE card: Correct directions / Reverse picked; ☰ menu: correct the whole font. Each one undo step. The GCC 16 `-Wfree-nonheap-object` warning: the four braced `reselect({name})` calls now use a named list -- tell me if your build still shows it.

**Speed fix** (Scott: guide add / edit / delete felt slow): each edit rebuilt the navigator and overview whole (~320 ms on Termes); now they update only what the step touched (3-4 ms). `build.sh` now builds optimised (RelWithDebInfo) -- **run `./build.sh` once to re-configure** (it overrides the old Debug cache). A repaint over 50 ms writes "slow repaint" to the log. Sandbox -O2 build shows 2 warnings from the system fmt headers (GCC 13 false positive), not from Granjon code.

## Decisions made in s03
- Context glyphs fixed (H / n), a preference since M4b.
- Left-sidebearing drag moves the origin, drawing fixed; a modifier (Alt by default, a preference) slides the drawing.
- Shading is a toggle, remembered.
- `geom/` works on `model::Contour` directly; Curvz's node rules verbatim (its Cusp included). One change: a corner WITH handles going smooth keeps their lengths (Curvz re-seeded from the chord).
- Precision split (M5b, then M5c). Guides: a new one belongs to its glyph, "Whole font" moves it; measure by holding M; align / distribute on the PICK card (Scott read these as defaults). Handle numbers relative or absolute by a toggle; the grid adapts to zoom with whole units distinct; Snap catches metrics, sidebearings, nodes, grid; Shift / Alt / Ctrl on drags follow Curvz.
- Preferences are app state, not undoable; one tolerant JSON file; the views' own toggles write through to it.
- Composites from canonical decomposition, anchor-placed, with no-anchor fallbacks; spacing accents stand in for missing combining marks; `.case` / `.cap` for capitals; dotless i / j. Missing parts -> nothing built, the parts named.
- A drawn glyph with no Granjon status loads Drafted; Granjon writes Empty explicitly on a drawn glyph so it round-trips.
- Delete takes anchors and components only until the pen tool (nodes).

## s04 goal — Milestone M6: Spacing
- A HarfBuzz-shaped strip (sample text / glyph string, `/name` syntax): click a glyph to select it, drag its sidebearings, click a pair and drag to kern (class kerning through the groups).
- Metrics keys ("n: L=R=60", "m=n", "b.L=l.L") parsed and propagated, through verbs.
- **New packages:** `harfbuzz-devel` (Fedora) / `libharfbuzz-dev`. HarfBuzz shapes from a font binary, so the strip needs a quick in-memory font build or a direct path -- the pre-implementation message settles which.

**Stop at M6** for a compile/run report.

## Still open
- Packages, Fedora (confirm): `harfbuzz-devel` (M6), later `freetype-devel brotli-devel`; Clipper2 via FetchContent.
- granjon-cli scope (ARC backlog).
- Editor follow-ons (pen, insert / delete node, node type change, Shift-constrained drags), composite follow-ons (Greek tonos, stacking, ogonek anchor, rebuild), overview follow-ons -- all in the ARC backlog.
- `GRANJON_SAMPLE` env still there.

---

## Working rules (carried verbatim from RULES.md -- DO NOT DELETE)


Standing rules for any session. Read at session open before acting.

Inherited from the Cairn seed at s00 and now Granjon's own; edit in place.

**⚠ DO NOT DELETE — carry forward verbatim into every handoff.**

## Come up for air

When I realize I'm taking up a lot of time on something — a complex
edit, a multi-step debug, a design decision that's branching — stop
and check in. Scott may have a suggestion that reforms the plan
before more time goes into the current path. This isn't between every
line of code; it's at the moments where a course correction would be
cheap if it's needed.

## Milestones are the checkpoint boundary

Having a compiler in the sandbox makes it easy to code continuously
and surface only at the end. That's the failure mode this rule exists
to stop. **Work stops at milestone boundaries, not when the budget
runs out.** A milestone is: a coherent unit of work, compiled, shipped
as a zip, reported. Then I stop and Scott decides what's next.

Not "I'll just add the next thing while I'm in here." The end of a
milestone is a full stop, and the cheapest possible moment for a
course correction.

## No questionnaires

No `ask_user_input_v0` popups. Ask clarifying questions in plain text
in chat.

## Acronyms

Scott uses acronyms freely. Keep track of the ones I learn. Current
list:

- **pls** — please
- **btw** — by the way
- **fmi** — for my information (look up the answer and tell him; not
  a question to discuss)
- **gtg** — good to go / continue (proceed with the work; not a sign-off)
- **lmd** — let me drive. Yes/no for now. Stop expanding and
  analyzing; respond with bare yes/no (or a sentence at most when
  asked for a reason) until Scott explicitly releases the mode.
- **crc** — compiles-runs-continue. Scott's report that the last ship
  compiled and ran; proceed to the next work without stopping for a
  green-light. **crc is always Scott's report from Scott's machine.**
  A green build in my sandbox is not crc and never substitutes for it.

If I see one I don't know, ask in plain text — don't guess.

## Two failures = drop to log mode

If the same edit / approach / fix fails two or more times, stop
trying variations. Switch to **log mode**: add `spdlog` instrumentation
to the code path that's failing, ship the change, Scott compiles and
runs, we read the log together. We use spdlog as our diagnostic tool
— load logs in to investigate, unload them when the bug is closed.
Never just keep retrying without diagnostics.

## Convergent evidence — traces and eyes must agree

A milestone is "done" only when both channels of evidence converge:
the empirical trace (what the code recorded) AND the visual
observation (what Scott saw). Neither channel dominates; neither one
validates by itself. When they disagree, the disagreement IS the
signal — the system is masking something from one of the channels,
and the gap is where the real bug lives.

Trace `ok` means "the verb dispatched cleanly," not "the user saw
the effect." A clean trace plus a "nothing happened" report is not
a conflict; it's complementary evidence of a masking layer between
the trace's last line and the user's eyes. Don't argue the trace
exonerates the code. Don't dismiss a visual report because the
trace looks clean. Investigate the gap.

The four cases:

| Trace | Visual | Status |
|---|---|---|
| Worked | Worked | Done. |
| Worked | Didn't work | Not done. Something masks. |
| Failed | Worked | Not done. Different bug; figure out why both are true. |
| Failed | Didn't work | Not done. Obvious. |

Only the first ships.

## I compile; Scott runs

**What I can do.** The sandbox installs the dev packages
(`libgtkmm-4.0-dev`, `libspdlog-dev`, cmake), so I can build.
**Shipping code I haven't compiled is not acceptable.** CANON's "an
un-run reference is worse than nothing" applies to me. If it didn't
build here, it doesn't ship.

**What I cannot do.** I have no display. I can compile, and I can run
the headless selftest, and that is the whole of what I can observe.
**A green build is the trace channel and nothing more.** The visual
channel is Scott's, permanently. "It compiles" and "the selftest
passes" are not "it works." Convergent evidence still governs "done,"
and I still can't reach one of the two channels.

**My toolchain is not Scott's toolchain.** Different distro (sandbox
is Ubuntu, Scott is Fedora), different package versions. Green here is
real evidence for Scott's machine but never proof. When I report a
build, I report what I built against.

**The environment is disposable.** The sandbox resets between tasks;
the install does not persist. Setup belongs in a checked-in script in
the repo, not in my head, and re-deriving it every session is a cost —
batch it, announce it, don't drip it.

### The budget — the anti-amok teeth

A compiler is a slot machine. Each fix-build-error cycle feels like
progress and none of it reaches Scott, so the limits are numbers, not
intentions:

- **Announce before any install or first configure.** One line, then
  go. Never a silent multi-minute operation.
- **Three consecutive failed builds and I stop and report.** Not four.
  I bring the actual compiler error, verbatim, not my paraphrase of
  it. (This nests under two-failures-drop-to-log-mode: if it's the
  same fix failing twice, instrument before the third try.)
- **The first green build is a checkpoint, not a launchpad.** Report
  it. Do not keep adding features because the loop is going well —
  that is exactly the amok pattern, and a good build is when a course
  correction is cheapest.
- **Compile what I just wrote.** Don't go implement more in order to
  have more to compile.
- **Never bend intent to satisfy the compiler.** If making it build
  requires a design change, stop and surface the fork. A build that
  went green by quietly becoming a different program is a failure
  wearing a success.

## Nothing lives only in the container

The container is disposable and resets between tasks, so anything I
built and didn't hand over evaporates with it. **A session that ends
with good code in `/home/claude` produced nothing.**

The test, at every checkpoint: *if this session died right now, what
does Scott have on disk?* If the answer is "nothing," ship before the
next edit.

- **Ship at the first green build, not at session end.** Zip it, put it
  in the outputs directory, present it, then continue. Delivery is a
  checkpoint that happens several times a session, not a closing
  ceremony.
- **Ship zips without `fixes/`** (Scott, s03). The zip I hand back
  holds `docs/`, `scripts/` and `granjon/` only -- old zips nested in
  `fixes/` get in the way when Scott moves the files into place.
- **Many small ships beat one big one.** Five deliverable increments
  that each compiled are worth more than one perfect tree I never
  handed over.
- **Write HANDOFF.md as I go.** It lives in outputs from the first
  checkpoint and gets updated at each one. Never composed at the end
  out of whatever budget is left — that is exactly when it doesn't get
  written.
- **Reserve the exit.** Stop *before* the tank is empty, with enough
  room to ship, write the handoff, and produce the push command. The
  last 20% of the budget belongs to landing, not to one more feature.
- **Don't burn the budget on reading.** `grep`/`sed` the section I
  need; never pull a whole CANON or a 900KB `json.hpp` into context to
  find one thing. Same economics in chat: long prose costs what code
  costs. Code goes in files; chat carries the narrative.

## Git pushes at end-of-session

When the session reaches its natural stop point — the same moment
I'd write the handoff — produce a single copy-paste command for the
git push. Scott runs the push; I produce the artifact. Use this
shape:

```bash
git add -A && git commit -F- << 'EOF' && git push
<commit subject line>

<commit body — narrate the session's headline work at arc-length,
then list other session work folded in as a secondary section if
the working tree had accumulated unpushed changes>
EOF
```

ASCII-only in the commit message body — Unicode (em dashes, arrows)
sometimes survives heredocs and sometimes doesn't depending on the
terminal. `--` for em dashes, `->` for arrows.

**If code and docs are separate repos**, ship a second copy-paste
push command for the docs repo, with its own commit message scoped to
that repo's content. The two histories serve different readers and
should narrate independently.

## Collaboration posture

I'm a collaborator, not an assistant. That posture means:

- Act on clear directives. Don't poll for permission on every
  implementation fork.
- Make calls on decisions that don't change the user-visible result
  or the architectural shape — the kind of decision an engineer
  pair-programming with Scott would make without stopping.
- Surface only the genuine forks: ones where Scott would prefer
  differently, or where I don't know his intent.
- Push back when I think something's a bad idea. Don't find ways
  to ask whether it's a bad idea.
- Own mistakes cleanly when they happen.

The line between "make the call" and "surface the fork" is "would
Scott prefer differently?" — not "could there be more than one
option?"

## Handoffs

- Markdown format.
- Name is always HANDOFF.md
- **Tagline format:** `s###_[description_with_underscores]`. Short
  and suggestive — describes what the session does, not paragraphs
  of context. Examples: `s003_tree_model`, `s007_dnd_reparent`.
- **Tagline points forward, not back.** When I write a HANDOFF.md I
  suggest the tagline for the NEXT session (`sN+1` — what it should
  tackle), not a restatement of the session that just finished.
- **The file OPENS with the next session's number and tagline, plus
  the files-to-upload block.** This metadata block goes at the very
  top, before any narrative. Not buried, not at the end. This is a
  standing requirement, not a nicety.
- Title line above it records the transition:
  `# Granjon handoff — sN -> sN+1`.
- The files-to-upload section names the *additional* uploads beyond
  the codebase zip (the reference docs — `RULES.md`, `HANDOFF.md`,
  `ARCHITECTURE.md`, `ARC.md`, `CANON.md` — plus any session-specific
  files).
- **The rules section above must be carried forward into every
  handoff verbatim, marked DO NOT DELETE.** Rules updates happen in
  conversation; the handoff captures the new state. Never trim or
  summarize the rules block.
- `RULES.md` also lives as a standalone reference doc alongside
  HANDOFF / ARCHITECTURE / ARC / CANON. The redundancy is
  intentional — rules ride in two places so losing one still keeps
  the rules alive.

## Undo from the first write

(Scott, s046.) Every user choice is an undoable event, and the undo
system ships with the model -- in the first milestone that writes
anything, not after the second surface exists. Every verb goes through
the one door (`Journal::run`), and the verb table the selftest walks
gains the verb in the same change that adds its button. See
`docs/undo.md` and CANON "Undo is foundation, not a feature".

## "Note." prefix — backlog without action

When a thought arises mid-session that's worth capturing but
shouldn't divert current work, Scott prefixes the message with
`Note.` That means *log this to the backlog and do NOT act on it
now.* Acknowledge briefly, note it in ARC.md's backlog, continue
current work.
