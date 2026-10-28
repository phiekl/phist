# Clickable URLs

Recognize, underline and copy URLs from the screen or history.

## Copying a URL

HTTP and HTTPS URLs are underlined on the main screen, alternate screen and
in scrollback, including soft-wrapped continuations beyond the viewport. A
plain left click copies the target to both PRIMARY and CLIPBOARD. Dragging
selects text; Shift-click bypasses URL copying. Application-supplied
underline styles take precedence over the overlay.

`urlprefixes` sets recognized schemes and `urlchars` the accepted
characters. Prefixes must look like `scheme://`: an initial letter, then
letters, digits, `+`, `-` or `.`, followed by `://`. Invalid entries are
ignored with a diagnostic; overlapping valid schemes work.

## OSC 8 hyperlinks

Applications can attach a target URI to a text label with OSC 8. Labels
are shown as blue, underlined text on their original background. Click a
label to reveal its actual target in a single-row bar across the window, as tall
as the search bar. The target appears as a gold, underlined URL, starting just
below the clicked label, or further left if it would not otherwise fit. The bar
is solid behind it and fades out towards both edges of the window. If it cannot
fit below the label, it opens above it instead.
Click the displayed URI or press Enter to copy the complete target to both
PRIMARY and CLIPBOARD. The URL flashes with inverse colors for
`urlflashtimeout` before the bar disappears.
With the timeout set to zero, it closes immediately.
Clicking elsewhere dismisses the bar without acting on the terminal beneath
it. Click the original label again to reopen it.

Escape closes the bar without sending a key to the child. Typing any
character closes it and processes that key normally, including Unicode,
control characters and Alt-modified input. Other keys, such as Left/Right,
Home/End, function keys and paste shortcuts, also dismiss the bar and run
normally. Plain Enter activates the URL instead; Enter with Ctrl, Alt or Shift
is like any other key. Modifier keys alone leave it open.

A target wider than the window shows a window's width at a time. The wheel,
Up/Down and Page Up/Page Down move through it; with a modifier, or when the
target already fits, those keys are like any other too. The bar adapts to
resizing, including a one-cell window. Output can continue while it is open; it
keeps the target that was clicked, even if the original label is overwritten.

The appearance is set in `config.h`, with colors as `0xRRGGBB`:

| Setting | Meaning | Default |
| --- | --- | --- |
| `urlboxcolor` | The bar | `0x2563eb` |
| `urlboxtextcolor` | The target and its underline | `0xffd75f` |
| `urlboxflashcolor` | The bar while flashing after a copy | `0xffd75f` |
| `urlboxflashtextcolor` | The target while flashing | `0x2563eb` |
| `urlboxfadeopacity` | The bar's opacity at the window's edges, from 0 to 1 | 0.05 |

Regenerate `config.h` to adopt these settings, as explained in
[building](building.md#build-and-configure).

The fade uses XRender gradients with coordinates clamped to its signed
16.16 range. Segments whose endpoints coincide after clamping are
skipped.

Try this inside phist:

```sh
printf '\033]8;;https://example.com/actual-target\033\\Project\033]8;;\033\\\n'
```

Clicking `Project` reveals `https://example.com/actual-target`. Dragging to
select the label, or using Shift-click, copies the visible text through
normal selection. Explicit links take precedence over automatically
detected URLs in their labels. `urlprefixes` and `urlchars` configure only
automatic text detection; OSC 8 supplies its target directly.

The protocol is `OSC 8 ; params ; URI ST`; an empty URI closes the link.
BEL is also accepted as a terminator. The optional `id=name` parameter
groups cells with the same target and ID; other parameters are ignored.
Semicolons in the URI are preserved. SGR changes, including SGR 0, keep
the active hyperlink. Saved cursors retain it, while terminal reset closes
it. Links on retained history cells survive reset along with their text.

Targets are limited to 2,048 bytes and must be ASCII URIs with a scheme,
no literal whitespace or control bytes, and valid percent escapes. Encode
non-ASCII filenames and spaces in the URI. IDs are limited to 256 bytes.
A malformed or oversized open closes the previous link and leaves the
following label as ordinary text. Targets are copied as data; phist does
not invoke a browser or URI handler.

Link metadata travels with cells through scrolling, compressed history,
wide-character wrapping, insert/delete operations and reflow. It uses the
existing 16-byte cell allocation. The target table is bounded to 8,191
entries and 4 MiB of URI/ID strings. Repeated targets without an ID share a
table entry, while adjacent labels retain separate bounds. Lookups hash the
target and optional ID rather than scanning all retained entries, with a seed
chosen at startup so that output cannot prepare colliding targets. Targets with
different explicit IDs retain separate entries. When either limit is reached,
further opens leave their labels as ordinary text; retained targets stay intact.
An entry is reclaimed once no cells or cursor attributes reference it,
making space available for subsequent links.

## Matching and bounds

Scheme matching ignores case. Parentheses and brackets are balanced and
trailing prose punctuation is removed. Hard newlines, non-ASCII cells and
wide-character dummy cells stop a match. Each target is limited to 2,048
ASCII characters; scanning then continues for further URLs. The copied
string is exactly the bounded target that is underlined.

Matching uses visible rows plus up to 2,048 characters of wrapped context on
either side, rounded to complete rows. Text changes, scrolling, history
changes and geometry changes recompute the overlay; only rows whose URL
cells changed need redrawing. Stored glyph attributes are untouched, and no
cross-update cache can retain a stale target after an edit or history
eviction.

The cap bounds allocations, matching and row lookups by the viewport rather
than the full length of a wrapped line. One tradeoff remains: chains of
nested URLs that repeatedly hit the cap can be segmented differently when
the viewport moves.

## Checks

```sh
LC_ALL=C.UTF-8 sh tests/urls.sh
LC_ALL=C.UTF-8 sh tests/hyperlinks.sh
LC_ALL=C.UTF-8 sh tests/paste.sh
LC_ALL=C.UTF-8 sh tests/rendering.sh
```

The URL suite compares every visible cell against a fresh update and an
independent whole-line parser, except for the capped-chain ambiguity above.
It covers edits, eviction, wrapping, colors, alternate screens, resizes,
random parser input and long-line scrolling, with 32 seeds over 18 history
limits and seven sizes. A 4 MB wrapped-line case checks allocation and
row-lookup bounds through the test-only `ROWVISIT()` hook in `viewline()`
and `sb_get()`. Rendering checks cover foreground-colored URL underlines.

The hyperlink suite checks labels, IDs, wide cells, selection, edits,
compressed history, reflow and both storage limits, accounting for every
target reference to catch leaks and premature reuse. The paste suite also
checks target-box colors, clicking, Escape, input isolation, long targets,
typing through, timed copy feedback, tiny windows and allocation-failure
rendering.

## Copy feedback

A copied URL briefly reverses colors, including all its wrapped portions.
`urlflashtimeout` sets the duration in milliseconds: 150 by default, or zero
to disable it. Only the clicked occurrence flashes, even if the same target
appears elsewhere. Original attributes return when the flash ends, including
while phist is idle.

The flash remembers the target text and position, rather than a changing
overlay index, and is cancelled if the target there changes. Drawing runs
split at occurrence boundaries, so adjacent capped targets with identical
attributes still flash independently.

The URL and rendering suites above check duplicate targets, wrapped
feedback, reordered overlay IDs, changed text and attribute restoration
through the real parser and glyph-run renderer.
