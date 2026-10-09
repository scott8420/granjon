# Resources & dialog lifetime — two paradigms, one exemplar

Two paradigms that every GTK app eventually needs, and that a future you would
otherwise rediscover the hard way. They're carried by a single running window
(`AboutWindow`) so proving one proves the other: a hide-on-close About box whose
logo is a symbolic icon resolved out of a compiled-in resource bundle.

Distilled from Optik (s9: the clean resource pipeline + born-modern dialog) and
Curvz (the original custom-About-window). CANON says the disciplines in prose;
this page says them in code that builds and links here.

## Paradigm 1 — the resource pipeline (symbolic icon, no disk install)

The app ships an asset *inside the binary* and lets GTK's icon machinery find
and recolour it. The chain:

1. `resources/cairn.gresource.xml` lists the SVG under an icon-theme-shaped
   prefix.
2. `glib-compile-resources` (a CMake `add_custom_command`) embeds it into a
   generated C source, `cairn-resources.c`.
3. `App::on_activate` calls `g_resources_register(cairn_resources_get_resource())`
   then `IconTheme::add_resource_path("/io/github/example/Cairn/icons")`.
4. Anywhere: `set_icon_name("cairn-logo-symbolic")`. GTK resolves it from the
   bundle and, because the name ends `-symbolic` and the SVG fills with
   `currentColor`, recolours it to the widget's foreground.

The result: no icon written to the user's disk, no allow-list to maintain, and
the logo follows light/dark for free.

### The gotchas (this is why the paradigm is worth banking)

- **C linkage.** The generated `cairn-resources.c` compiles as **C**, so its
  `cairn_resources_get_resource()` has C linkage. Two consequences: the CMake
  `project()` must enable `C` (`LANGUAGES C CXX`), and the call site declares the
  prototype `extern "C"`. Skip either and you get a mangled-name link error that
  reads like the function doesn't exist. We declare the prototype inline in
  `App.cpp` rather than `#include`-ing the generated header, so there's nothing
  to keep in sync.

- **The path shape is rigid, and mismatches fail *silently*.** GTK's icon lookup
  demands the `.../scalable/apps/` tail. The gresource `prefix` ends there; the
  `add_resource_path` argument stops one level short (`.../icons`) because GTK
  appends `scalable/apps/` itself. Get the join wrong and `set_icon_name` returns
  a broken/blank image with **no error** — the classic concatenated-path
  silent-failure surface (CANON: *"Concatenated paths are a silent-failure
  surface"*). Verify the join with the tool, not by trusting the build:
  `strings build/cairn | grep cairn-logo-symbolic` (or `gresource list`) must
  show the full `/io/github/example/Cairn/icons/scalable/apps/...` path.

- **`glib-compile-resources` PARSES the SVG, so the SVG's own XML must be
  valid — including its comments.** A `--` anywhere inside an XML comment is
  illegal (XML reserves it as the comment terminator), and the failure is a
  hard build error from a file you were treating as an asset:

  ```
  icons/scalable/apps/cairn-logo-symbolic.svg:3: parser error :
      Double hyphen within comment
  cairn.gresource.xml: Child process exited with code 1.
  ```

  This bites specifically because the house comment style uses `--` as an em
  dash everywhere else in the tree, so the habit carries into the icon and the
  file *looks* right. Use an en dash, a colon, or nothing. The wider point is
  that `preprocess="xml-stripblanks"` means the bundler is an XML consumer, not
  a byte-copier: anything it can't parse, it won't bundle. (Cost a build during
  the jot transplant, which is how it got written down.)

- **The prefix mirrors the app-id.** `/io/github/example/Cairn` tracks
  `io.github.example.Cairn`. Both move together in a one-pass rename — the
  CMake var, the `Application` string, the gresource prefix, and the
  `add_resource_path` argument, all at once. Splitting that rename is how the
  silent failure above gets introduced.

## Paradigm 2 — dialog lifetime (born-modern)

`Gtk::AboutDialog` is **deprecated in GTK4**. The forward path is a custom
`Gtk::Window` — which you'd want anyway the moment the About box needs
app-specific styling. The real decision is its *lifetime*:

- **Hide-on-close singleton (what Cairn builds).** `Shell` holds one as a
  `std::unique_ptr<AboutWindow>`, built lazily on first open and re-`present()`ed
  after; `set_hide_on_close(true)` means the X hides it. It never destructs
  mid-session, so its named children never re-register — no self-rebuild registry
  collision, no leak. This is the born-modern default (CANON: *"Lifetime shape is
  design"*).

- **Heap-allocated self-deleting (the legacy shape).** `new Gtk::Window()` +
  delete-on-close-request. It works, but a close-then-reopen can race two
  same-named instances into the registry, so it needs
  `unregistered`/force-unregister care. Reach for it only when rescuing old code,
  not by default.

### The gotcha

A `unique_ptr<AboutWindow>` member means `~Shell()` must see the **complete**
`AboutWindow` type to instantiate the deleter. Forward-declare in `Shell.hpp`,
but define `~Shell()` out-of-line in a `.cpp` that `#include`s `AboutWindow.hpp`
(here, `Shell.cpp`). Otherwise you get *"invalid application of sizeof to
incomplete type"* at the member's destruction point. The header stays light; the
one TU that destroys it carries the include.

## Why they compose

`AboutWindow` is the smallest thing that exercises both at once: it's opened as a
hide-on-close singleton (paradigm 2) via an action *and* a frameless headerbar
logo button whose icon is a bundle-resolved symbolic (paradigm 1). One stone,
two paradigms.

Deliberately **not** carried: branded About chrome — credits, licence text,
outbound links. That's product content, not a paradigm; a seed banks the shape,
not the copy. The window instead labels itself with the paradigms it embodies.

## Exercised, not faked

Consistent with Cairn's one non-negotiable, but read the boundary honestly:

- **Build + link** proves the pipeline's build side end-to-end — the gresource
  compiles, the C object links, and `strings build/cairn` shows the logo bundled
  at the exact `/io/github/example/Cairn/icons/scalable/apps/` path.
- **`cairn_selftest` stays 22/0** — the paradigm is GTK-only and never touches
  the GTK-free core, which is the point.
- **Runtime** (the icon actually resolving + recolouring, the window laying out)
  needs a display, so it's verified the same way as every other Cairn GTK
  paradigm: by running `./build/cairn` and clicking the logo. The headless
  selftest cannot and does not pretend to cover it.
