# Granjon handoff — s05 -> s06

# s06_woff2_overlaps

## Files to upload
- `s06_woff2_overlaps.zip` (made by `scripts/granjon_backup.sh -o s06_woff2_overlaps`)
- `RULES.md`, `HANDOFF.md`, `ARCHITECTURE.md`, `ARC.md`, `CANON.md`
- `My-Termes-Regular.granjon` from the project's `fonts/` folder (not in the s05 upload; s05 rebuilt Termes in the sandbox from TeX Gyre the same way, but the M7 / M8 readouts on your copy are still unseen).
- **Curvz source** (`BooleanOpsClipper` and what it includes): M10b starts overlap removal from it.

---

## Status — s05 (written as we go)
- **M8 profiles:** crc (s05 open).
- **M9 export OTF: crc** (Scott, on r2: export via the dialog, GNOME Fonts reads the file, the Preview renders and follows the desktop's dark mode).
- **M10a export TTF: crc** (Scott). He noticed the OTF is slightly smaller than the TTF: same outline size (CFF 152 KB vs glyf 156 KB on Termes); the gap is post 2's stored names (~10 KB) and loca (~4 KB). CFF subroutines would shrink the OTF further (follow-on).
- **M9c drawing codes + Export SVG: built, shipped (`s05_m9c_codes_svg.zip` -- the one to install; every earlier s05 zip is superseded), awaiting Scott's crc.** Scott asked (s05) whether a font's drawing data can be seen, and for both a code view and outline export.
- M9c: the inspector's **CODE** card (glyph page, last card): Format = CFF (the OTF's charstring disassembled -- operands then operator, batched groups one per line), TrueType (ttx-style `<pt x y on>` lines after cu2qu and the direction flip), or SVG path (what Export SVG writes); size / count summary; Copy. Follows edits (after a drag, at release); the format is remembered (`inspector.code_format`). ☰ **Export glyph as SVG…** and **Export Spacing text as SVG…**: plain filled paths, y flipped, no transforms, one `<path id="gN-name">` per glyph, the strip's kerning applied, viewBox in font units, width / height at 100 px per em, the text in `<desc>`.
- M10a: cu2qu ported from fontTools (cubic -> fewest quadratics within 1/1000 em, implied on-curves; same points as fontTools on the test curves), glyf / loca (short when it fits) / maxp 1.0 / post 2 / gasp; every contour reversed to TrueType's direction; OVERLAP_SIMPLE set; the rest shared with the OTF. App: **Export TTF…** (☰, **Ctrl+Shift+E**).
- Selftest 717 / 0 (16 new for TTF, 11 for codes + SVG). Built against Ubuntu 24.04, g++ 13.3, gtkmm 4.10 / GTK 4.14, pango 1.52, RelWithDebInfo; the known GCC 13 / fmt false positive x3. **No new package.**
- Outside validation (`scripts/otf_check.py`, now OTF or TTF; TrueType outlines compared with fontTools' own cu2qu of the source, i.e. what fontmake would ship): Termes + groups and Termes at 2048 UPM as TTF, and a TTF exported through the app's dialog, pass OTS, fontTools, outlines (1090 glyphs), FreeType, HarfBuzz (25,579 pairs). OTF vs TTF rendered by FreeType at 160 px overlay with no visible difference. Termes TTF: ~55 ms, 217 KB, 52,715 points.
- Before M10a: Scott ran M9 r1 and asked that the Preview page follow the desktop's dark mode -> r2 (see decisions).

What to look at in M9c:
- Select a glyph; the inspector's last card, **CODE**. Switch CFF / TrueType / SVG; nudge a node in the editor and watch it follow. Copy, paste into a text editor. Compare with `ttx -t glyf` on an exported TTF: the <pt> lines should match.
- ☰ **Export Spacing text as SVG…** with a word in the Spacing strip; open the .svg in Inkscape / Illustrator: one path per letter, named, kerned, no font needed.

What to look at in M10a (crc):
- **Ctrl+Shift+E / ☰ Export TTF…:** proposes `Family-Style.ttf`; status line and log as for OTF (the log adds the glyf point count).
- Open the .ttf in GNOME Fonts beside the .otf: they should look the same. (Don't install a "TeX Gyre Termes" build -- it collides with the real one; My Termes is safe.)

What to look at in M9:
- **Ctrl+E / ☰ Export OTF…:** the dialog proposes `Family-Style.otf`, starts where the last export went (else the font's folder). The status line says glyphs / size / ms and the first note; all notes go to the log (`[io] export: ...`).
- **Install the .otf** (`~/.local/share/fonts/`, then `fc-cache`) and set text in another app (gedit, LibreOffice, Inkscape): names, kerning, metrics (line spacing = 1.2 em by the typo metrics).
- **Preview (Ctrl+4):** the waterfall and the paragraphs are the COMPILED font set by Pango, not Granjon's drawing. Kerning toggles the compiled GPOS. The page follows the desktop's light / dark (r2); the button, labelled with the other mode ("Light" on a dark desktop), flips it. The bar shows glyphs / KB / ms (tooltip: the notes) and "N missing" (tooltip lists them; they show as boxes). Edit something with the Preview showing (or come back to it): it recompiles a moment later.
- On Termes: whether the Preview's kerning matches the Spacing strip on the same pairs (it should, exactly -- the GPOS is built from the strip's lookup).

## Decisions made in s05
- M9c: the code view shows what the FILES carry (production name noted beside yours; the CFF width written absolute for reading; the TTF after conversion), read-only. SVG export is outlines only (no <text>, no font, no transforms) -- the logo hand-off; em = 100 px by default (a setting can follow). Two menu items (glyph / Spacing text) rather than guessing from the front view.
- TTF (M10a): cu2qu at 1/1000 em (fontmake's default); contours all reversed (consistent, so nonzero fill is unchanged -- not the depth-based direction fix); OVERLAP_SIMPLE on every glyph's first point (overlaps kept until M10b); components decomposed (TrueType composites are a size follow-on); no hints, gasp = smooth at all sizes; loca short when the glyf fits; post 2 carries the production names. The Preview stays on the OTF.
- Thin strokes (tie accents, outline (C) / (R)) lose ~1 % area to the 1-unit tolerance -- the same as fontTools'; the checker compares against fontTools' conversion, not the cubic.
- Export OTF is Ctrl+E (free in the shortcut registry) with its own section in ☰; it always asks where (an "export again to the same file" can follow if you want one).
- The Preview sets the compiled font with Pango in a PRIVATE font map (fontconfig holding only that font, a fresh cache file per compile, stale files from dead runs removed); its page follows the desktop's light / dark, live, with a button for the other mode (setting `preview.invert`, replacing r1's `preview.dark`) -- the desktop's text stack, no new package; direct HarfBuzz stays for the strip, pending the package answer.
- Export never touches the font: no undo step (a product made from the font, not a choice on it).
- Outlines go out **as drawn**: no silent direction correction (an overlapping contour can read as "inside" another and get reversed into a hole), no overlap removal (M10). Direction is the Checks report's job; CFF fills nonzero, as the editor shows.
- Components are decomposed; coordinates rounded after the transform.
- Production names: an encoded glyph whose name doesn't spell its first code point by the AGL rules goes out as uniXXXX / uXXXXX (what fontmake does); unencoded glyphs keep their names. Termes: 419 renamed (anglearc, baht, f_f on U+FB00, Delta on U+0394 ...).
- No .notdef -> a box is supplied and noted.
- Vertical metrics, the Google Fonts scheme: typo = ascender / descender, typo line gap to 1.2 em, hhea = typo, USE_TYPO_METRICS on, win = drawn bbox. OS/2 unicode-range and code-page bits from the cmap, never hand-set.
- Kerning: PairPos format 1 first (every glyph pair an entry with a glyph side reaches, at `kern_lookup`'s value -- the strip's own lookup -- minus pairs the classes answer the same; a 0 exception stays), then format 2 for group / group. Subtables split under 64 KB; Extension lookup only when needed. DFLT + latn.
- No CFF hints, no BlueValues (none in the model; M10's ttfautohint covers TTF).
- head.created / modified honour SOURCE_DATE_EPOCH (reproducible builds); the selftest fixes the time.

## s06 goal — Milestone M10b: WOFF2 + overlap removal
WOFF2 (brotli; the glyf / loca transform is optional -- start without), overlap removal on export (Clipper2 via Curvz's BooleanOpsClipper; curves flattened -> rebuilt, or kept by a curve-aware pass -- a fork to settle and surface), the ttfautohint hook. Needs the package answer first. **Stop at M10b.**

## s05 goal (done) — Milestone M9: Export OTF
(ARCHITECTURE milestone list.) An sfnt writer + CFF: head, hhea, maxp, OS/2, name, cmap, post, CFF (charstrings from the cubic outlines, components decomposed), hmtx; kerning as a GPOS pair table (class kerning -> class pairs, exceptions as glyph pairs) -- the first feature output. Validate by reloading with FreeType (and shaping a proof with HarfBuzz -- the moment HarfBuzz earns its place). **Packages** (Fedora: `harfbuzz-devel freetype-devel`; brotli waits for WOFF2): announce, check with Scott before relying on them. A Preview view (Ctrl+4, still a stub) showing the compiled font is the natural visual check. Settle forks inside; surface what changes the workflow.
**Stop at M9** for a compile / run report.

## Still open
- Packages, Fedora (asked in s05, answer pending): `brotli-devel` for WOFF2 (M10b), Clipper2 1.5.4 via FetchContent for overlap removal (M10b), `harfbuzz-devel freetype-devel` for the strip / import.
- A RULES.md addition proposed in s05, answer pending: "One zip to install" (the final message names the single zip to install; checkpoint zips carry `_wip`).
- M10b: WOFF2, overlap removal (Clipper2, from Curvz's BooleanOpsClipper), ttfautohint hook (optional external tool).
- granjon-cli scope (ARC backlog). `export_otf` is its first ready-made command.
- Editor follow-ons (pen, insert / delete node, node type change, Shift-constrained drags), composite follow-ons (Greek tonos, stacking, ogonek anchor, rebuild), overview follow-ons -- all in the ARC backlog.
- Profiles follow-ons: a profile cache + font-wide kern-to-gap over a pair list, per-class targets, collision check on proof strings.
- Spacing follow-ons: kerning groups editor (create / assign public.kern1 / kern2 from the UI), a strip text history / presets.
- Checks follow-ons (ARCHITECTURE §6): missing extreme points, overlaps, sidebearing consistency, stem drift, kerning class audit and outliers, collisions.
- Export follow-ons: italic angle / caret slope (no field in FontInfo yet), panose / family class, CFF subroutinization (size), PostScript blue zones from the metrics.
- The inspector panel is narrower than some of its rows in a 1280 px window (the OUTLINE card's "Reverse picked", the KERN hint): clipped, not wrapped. Check on Scott's screen.
- selftest.cpp's main() wants splitting per milestone (M9's part is its own file now; the rest still one function).
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
