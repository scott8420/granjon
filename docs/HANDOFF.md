# Granjon handoff — s04 -> s05

# s05_export_otf

## Files to upload
- `s05_export_otf.zip` (made by `scripts/granjon_backup.sh -o s05_export_otf`)
- `RULES.md`, `HANDOFF.md`, `ARCHITECTURE.md`, `ARC.md`, `CANON.md`
- No Curvz source needed (its geometry is in `geom/`; `BooleanOpsClipper` returns at M10).
- Test fonts live in the project's `fonts/` folder (outside the repo): `My-Termes-Regular.granjon` (TeX Gyre Termes, 1090 glyphs). Upload it: export wants a real font to compile, and the M7 / M8 readouts on Termes are still unseen.

---

## Status — s04 (written as we go)
- **M5e node navigation: crc.**
- **M6 spacing: crc.**
- **M7 checks + report: crc.**
- **M8 profiles: built, shipped (`s04_m8_profiles.zip`), awaiting Scott's crc.** If the run report brings fixes, they come first in s05.
- Selftest 658 / 0. Built against Ubuntu 24.04, g++ 13.3, gtkmm 4.10 / GTK 4.14, RelWithDebInfo; the same 2 GCC 13 false-positive warnings from system headers as s03. **No new package** (HarfBuzz waits for M9).
- New this session: headless screenshots under Xvfb (`xvfb-run` + `xdotool` + `import`, with `GRANJON_SAMPLE=1`) as a smoke check of drawing and gestures. It catches gross faults; it is not the visual channel -- that stays Scott's.

What to look at in M6 (Ctrl+3):
- **The strip:** type `nnHOHnn` or `/n/o/eacute` (remembered); the slider sets px per em (remembered; Ctrl+scroll too). Lines wrap at the width. Metric lines behind, advance edges faint, kerns above each join (blue negative, orange positive), LSB / RSB in the band under each line.
- **Click a glyph:** it becomes the selection (inspector follows); Left / Right step along the strip.
- **Click a join** (within 7 px): the pair is picked -- a blue line, the inspector's **KERN** page (preview, groups, the entry in effect, Value, Make exception, Remove). **Drag** or **Left / Right** (Shift x10) kern it; **Alt** writes an exception. A drag is one undo step; arrow presses merge.
- **The band under a glyph:** drag its left half for the LSB, its right half for the RSB (Alt slides the drawing). **Ctrl** on any drag = a tenth of the speed.
- **Metrics keys:** inspector METRICS card, "Key": `L=R=60`, `=n`, `L=n R=|o`, `W=500`, `=n+10`, `n.L`. The state line says in step / off by how much. **Apply key** (one step); ☰ **Apply metrics keys in every glyph** (one step; chains resolve; circular keys named). In the strip a glyph off its key gets a red triangle and red numbers; keyed and in step, a faint dot.
- The sample font's `n` carries a key (60 / 60) and is drawn 70 / 70, so it shows red until applied.

What to look at in M7 (Ctrl+5):
- **The report:** one card per check with its why-note, a count, **Fix all N** and **Skip all**; rows with Fix and Skip. Top bar: summary, **Show skipped** (greyed rows with Unskip), **Re-check**, **Fix all** (every check). Each click is one undo step.
- **Checks:** contour direction; open contours; **near the metric lines** (a flat top / bottom 1-3 units off a line -> snap); **overshoot** (a round top / bottom with none, or not the font's overshoot -> move it there); **origin and grid** (off-grid coordinates -> round; drawn with no width; drawing outside its width); **metrics keys** off.
- **Click a finding:** the glyph opens in the editor with the spot picked (the node, or the whole contour).
- **Skips are saved with the font** (lib `granjon.checkSkips`) and undoable.
- On Termes: how many findings, how noisy the overshoot check is (it flags every round off the font's overshoot -- O vs o differences included), and whether "report: full check N ms" shows in the log.

What to look at in M8 (Ctrl+3, pick a pair):
- **In the strip:** the optical area shaded between the two glyphs inside the band, and the contact line (closest approach) with its value. Amber instead of blue when the two readouts disagree.
- **KERN page, GAP card:** Contact, Optical (x-height or cap band), Control (n n or H H), a note when they disagree, and **Kern to gap: N** -- one step, writes the entry in effect or the groups' pair.
- On Termes: try To, LT, AV, ro, nn -- whether the proposed kerns are in the right direction and size, and whether the disagreement note lands on the open shapes.

## Decisions made in s04
- M5e: handles-included walk on ` / Shift+` (Alt+Tab is the desktop's); Make first on F; the pick follows the node to index 0; Tab from a handle goes to the node on that side.
- M6 kern target: an edit writes the entry **in effect** (so an existing exception keeps answering), else the groups' pair (a side without a group kerns as itself); Alt writes glyph / glyph. Refines the s03 decision ("kerning a pair writes group kerning") for pairs that already have an exception.
- M6 sidebearing drags live in the band under the line, not on the glyph edges: the edge between two glyphs is also the pair's join, so the two gestures would overlap.
- Metrics keys are one rule line per glyph (stored per side in `granjon.metricsKeys`, as before). References resolve against the font as it is now; font-wide apply goes referenced-first. Keys only change things when applied -- no live propagation on every edit (that would turn one edit into many glyphs' steps behind Scott's back). Surface if Scott wants auto-propagation.
- The pair belongs to the Spacing view: leaving it drops the pair.
- M8: contact is the 2D closest approach (sees T's bar over the o); optical is the band's mean gap with a 20 % clamp; the target comes from the font's own control pair (n n / H H), not a setting -- kern-to-gap matches the spacing the designer already chose. Profiles are computed on demand (sub-millisecond for a pair); a cache comes with font-wide readouts.
- M7: skips are font data (a `@skips` record, undoable, saved), not view state -- "every user choice is an undoable event". Zones only look at flat nodes, overshoot only at round extremes, so no node is reported twice. Overshoot is strict (any round off the font's overshoot); if Termes shows that as noise, a per-glyph tolerance or "round tops may vary by N" preference is the next step. Origin's no-width and outside-the-box findings are report-only (no safe mechanical fix).

## Decisions made in s03
- Context glyphs fixed (H / n), a preference since M4b.
- Left-sidebearing drag moves the origin, drawing fixed; a modifier (Alt by default, a preference) slides the drawing.
- Shading is a toggle, remembered.
- `geom/` works on `model::Contour` directly; Curvz's node rules verbatim (its Cusp included). One change: a corner WITH handles going smooth keeps their lengths (Curvz re-seeded from the chord).
- Scott (end of s03): "questions you can determine" -- forks inside a milestone are mine to settle and record; surface only what changes his workflow or the architecture.
- Precision split (M5b, then M5c). Guides: a new one belongs to its glyph, "Whole font" moves it; measure by holding M; align / distribute on the PICK card (Scott read these as defaults). Handle numbers relative or absolute by a toggle; the grid adapts to zoom with whole units distinct; Snap catches metrics, sidebearings, nodes, grid; Shift / Alt / Ctrl on drags follow Curvz.
- Preferences are app state, not undoable; one tolerant JSON file; the views' own toggles write through to it.
- Composites from canonical decomposition, anchor-placed, with no-anchor fallbacks; spacing accents stand in for missing combining marks; `.case` / `.cap` for capitals; dotless i / j. Missing parts -> nothing built, the parts named.
- A drawn glyph with no Granjon status loads Drafted; Granjon writes Empty explicitly on a drawn glyph so it round-trips.
- Delete takes anchors and components only until the pen tool (nodes).

## s05 goal — Milestone M9: Export OTF
(ARCHITECTURE milestone list.) An sfnt writer + CFF: head, hhea, maxp, OS/2, name, cmap, post, CFF (charstrings from the cubic outlines, components decomposed), hmtx; kerning as a GPOS pair table (class kerning -> class pairs, exceptions as glyph pairs) -- the first feature output. Validate by reloading with FreeType (and shaping a proof with HarfBuzz -- the moment HarfBuzz earns its place). **Packages** (Fedora: `harfbuzz-devel freetype-devel`; brotli waits for WOFF2): announce, check with Scott before relying on them. A Preview view (Ctrl+4, still a stub) showing the compiled font is the natural visual check. Settle forks inside; surface what changes the workflow.
**Stop at M9** for a compile / run report.

## Still open
- Packages, Fedora (confirm before M9 relies on them): `harfbuzz-devel freetype-devel` for M9, `brotli-devel` with WOFF2; Clipper2 via FetchContent (M10).
- granjon-cli scope (ARC backlog).
- Editor follow-ons (pen, insert / delete node, node type change, Shift-constrained drags), composite follow-ons (Greek tonos, stacking, ogonek anchor, rebuild), overview follow-ons -- all in the ARC backlog.
- Profiles follow-ons: a profile cache + font-wide kern-to-gap over a pair list, per-class targets, collision check on proof strings.
- Spacing follow-ons: kerning groups editor (create / assign public.kern1 / kern2 from the UI), a strip text history / presets.
- Checks follow-ons (ARCHITECTURE §6): missing extreme points, overlaps, sidebearing consistency, stem drift, kerning class audit and outliers, collisions.
- The inspector panel is narrower than some of its rows in a 1280 px window (the OUTLINE card's "Reverse picked", the KERN hint): clipped, not wrapped. Seen in the headless shots; check on Scott's screen.
- selftest.cpp's main() wants splitting per milestone (GCC 16's var-tracking note is silenced by a per-file flag for now).
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
