# Appearance and text rendering

Configure the cursor and the way terminal text is drawn.

## Cursor and blinking

`cursorstyle` defaults to 1, a blinking block. Applications can request a
style with DECSCUSR (`CSI Ps SP q`):

| Style | Shape |
| --- | --- |
| 0 | Restore the configured default when requested by an application |
| 1 / 2 | Blinking / steady block |
| 3 / 4 | Blinking / steady underline |
| 5 / 6 | Blinking / steady bar |
| 7 / 8 | Blinking / steady phist glyph |

A configured value of 0 also means a blinking block. `stcursor` chooses the
optional glyph; the default is U+2603, the snowman. Applications such as Vim
and fish commonly request style 0 on exit, restoring your choice.

`blinktimeout` sets the interval for both the cursor and blinking text: 500
ms by default, or zero to disable blinking. The timer is independent of
incoming output and unrelated X events, so continuous output cannot prevent
a blink. An unfocused cursor is a steady hollow box; a hidden cursor is not
drawn. Neither wakes phist just to blink. Focus restarts the phase with the
cursor visible.

Nonempty input from keys, mapped keys, send actions or paste resets the
deadline and reveals blinking text. Modifier-only keys, empty input-method
events and local shortcuts leave the timer alone. Revealed text marks its
displayed rows dirty so it cannot remain invisible after input.

Run `LC_ALL=C.UTF-8 sh tests/blink.sh`. Checks cover continuous output,
phase changes, input reset, disabled blinking, hidden/unfocused cursors,
DECSCUSR 0, real Shift/Control events, alternate-screen history keys and
full redraws after a resize that leaves the cell grid unchanged.

## Underlines

Applications can choose colored underline styles without changing text
color:

| SGR | Effect |
| --- | --- |
| `4:1` | Single underline |
| `4:2` | Double underline |
| `4:3` | Curly underline |
| `4:4` | Dotted underline |
| `4:5` | Dashed underline |
| `4:0` or `24` | Disable underline |
| `58` | Set indexed or RGB underline color |
| `59` | Use the text foreground for the underline |

For example, this prints text with a red curly underline, then resets:

```sh
printf '\033[4:3;58:2::255:0:0munderlined\033[0m\n'
```

Style and color survive cursor saves, screen updates and history, and SGR
reset clears both. `UNDERCURL_STYLE` selects rounded, spiky or capped curls,
with spiky as the default. Strokes stay inside their cells; dotted and
dashed patterns align across runs and partial redraws. The `Su` terminfo
capability advertises underline-style support; install the updated `st.info`
entries for applications to discover it.

The parser keeps colon subparameters separate from following semicolon
attributes, including omitted fields. For foreground (38), background (48)
and underline (58), RGB forms include `38:2::r:g:b`, `38:2:0:r:g:b`,
`38:2:r:g:b` and `38;2;r;g;b`. Malformed or unsupported color spaces are
rejected without swallowing the following attribute; diagnostics identify
the relevant 38, 48 or 58 attribute.

Cells remain 16 bytes, checked at compile time: Unicode uses 21 bits,
attributes 18, foreground and background 25 each, underline color 26
including a default-color sentinel, and the OSC 8 link 13. Three attribute
bits hold underline style. At 16 bytes a cell, 65,536 full 200-column
history rows take about 200 MiB uncompressed.

The renderer creates one underline GC lazily and caches up to 16 RGB
underline colors. Eviction frees the old allocation; reloading the palette
clears the cache. Indexed colors and the foreground reuse existing pixels,
and allocation failure falls back to the foreground. If the GC cannot be
allocated, the text is drawn without its underline and the next draw tries
again.

Run `tests/colors.sh` and `tests/rendering.sh` with `LC_ALL=C.UTF-8 sh`.
They cover parsing, cell size, history round trips, malformed colors, cache
reuse and eviction, allocation failure and balanced X resources.

Curly underline coordinates clamp to X's 16-bit range, and empty glyph runs
draw no curl. Long underlines are split at the server's request limit. The
rendering suite checks each `UNDERCURL_STYLE`.

## Emoji presentation

`emojifont` defaults to `Noto Color Emoji`. Set it to `NULL` or an empty
string to disable the preference. It is tried for wide emoji unless followed
by U+FE0E (text presentation), and for narrow characters followed by U+FE0F
(emoji presentation), even when the main font has a monochrome glyph. ASCII
stays in the main font. Text-style symbols such as arrows, check marks, ©
and ™ normally keep the main font.

The font is loaded lazily, fitted into the existing one- or two-cell width
and reloaded on zoom. If it is missing, is not a color font or lacks the
glyph, normal font selection takes over. Joined emoji sequences, flags and
skin-tone combinations are not shaped.

A variation selector is recorded on the preceding cell as one attribute bit
indicating a change from the default presentation for its width; cells
remain 16 bytes. For text presentation, a color fallback gives way to the
first non-color font covering the character. A cached color fallback is
reused only for the character for which it was cached.

Run `LC_ALL=C.UTF-8 sh tests/emoji.sh` and `LC_ALL=C.UTF-8 sh
tests/presentation.sh`. They check coverage, selectors, fallback choice,
missing fonts, fitting, styles, cache reuse and alignment.

## Color allocation

Colors beyond the palette, such as RGB, reversed and faint colors, are
allocated when a text run is drawn. On colormapped visuals, drawn pixels
still reference their color cells, so phist caches up to 128 colors and
redraws the screen when recycling them. Other visuals release temporary
colors after drawing. Allocation failure uses the default foreground or
background, or leaves faint text undimmed.
The fixed colors of concealed and low-contrast text, dialogs and bars use the
nearest palette color until they can be allocated. The palette itself is
required when phist starts. A later reset, by `ESC c` or `OSC 104`, allocates
the new palette first and keeps the one in use if that fails.
Resetting the entire palette with `OSC 104` marks the screen for redraw,
so existing text immediately uses the restored colors, just as for a
single-color reset.

`LC_ALL=C.UTF-8 sh tests/rendering.sh` checks all of these.

## Cell size

The primary font's metrics, scaled by `cwscale` and `chscale`, give the size of
a cell, which must be from 1 to 4,096 pixels. An ascent or descent beyond 4,096
pixels either way makes a font's metrics unusable, and such values are never
added up. A primary font without usable metrics is refused when phist starts,
an emoji font without them is not used, and zooming to a size that would be
outside these limits, or whose fonts cannot be opened, keeps the current fonts.
Zoom-out requests of 1 pixel or less are ignored; Ctrl+0 can still restore
a configured size of 1. X coordinates are 16-bit, so
columns and rows stop at what fits in 32,767 pixels, whatever size the window
or `-g` asks for, and the screen stops at 4,194,304 cells in total, about
128 MiB for the two screens. A larger window has blank space beyond them. These
limits apply before the terminal allocates anything for `-g`. If Xlib cannot
allocate the window manager's size hints, as when zooming, the window keeps the
ones it had. `LC_ALL=C.UTF-8 sh tests/emoji.sh` checks these limits.

Drawing buffers are limited to 32,767 pixels per dimension. If a replacement
cannot be allocated, phist reports the failure on stderr and keeps the
existing buffer and dimensions. Startup requires a buffer. A failed overlay
allocation skips the visual bell; dialogs still draw directly on the window.
Run `LC_ALL=C.UTF-8 sh tests/blink.sh` to check resize failures.

Pixmap allocation errors are trapped only for the request being made,
identified by its serial number. Errors from earlier requests delivered
by `XSync()` still go to the previous error handler.

## Fallback fonts

Characters missing from the primary font use a fallback found through
Fontconfig and kept in a cache, which is emptied when it reaches 1,024 entries,
or on zoom, and refills as text is drawn. A character that no font covers is
drawn as the primary font's missing glyph and tracked in a separate
16,384-slot table. These characters do not fill the font cache; repeated
lookups avoid a font search unless another character replaces their slot.

A fallback that cannot be loaded, for lack of memory or a damaged font file,
does not end the terminal. The character is drawn as U+FFFD from the primary
font, or as its missing glyph, and loading is tried again the next time it is
drawn.

`LC_ALL=C.UTF-8 sh tests/emoji.sh` checks the limit with a flood of uncovered
characters and the replacement after each kind of failure.

## Visual bell

The visual bell blends an animation over the current terminal content. Try
it in a new phist window with `printf '\a'`. Choose `vbelleffect` in
`config.h`:

| Value | Effect |
| --- | --- |
| `VBELL_GLOW` | Static gradient along the window edges |
| `VBELL_WAVE` | Vertical band travelling left to right |
| `VBELL_RIPPLE` | Circular wave expanding from the center |
| `VBELL_INWARD` | Rectangular wave moving inward from the edges |
| `VBELL_PULSE` | Whole-window tint that brightens and fades |

XRender gradients reach 32,767 pixels, so in a wider window a wave or ripple
past that point is not drawn.

Defaults select a 150 ms pulse at 50% peak opacity, transitioning from white
to black. The settings are:

| Setting | Meaning | Default |
| --- | --- | --- |
| `vbelltimeout` | Duration in ms; zero disables the effect | 150 |
| `vbellinterval` | Frame interval in ms | 16 |
| `vbellwidth` | Glow depth or band width in pixels | 512 |
| `vbellopacity` | Peak opacity from 0 to 1 | 0.5 |
| `vbellcolor` | Trail or outer-edge RGB color | `0xffffff` |
| `vbellinnercolor` | Crest or inner-edge RGB color | `0x000000` |

Colors use `0xRRGGBB`. The pulse moves between the two colors over time; the
glow blends them across its depth and fades into the content. For a short
red-to-yellow edge glow, try `VBELL_GLOW`, 50 ms, 96 pixels, opacity 0.6,
and colors `0xff0000` and `0xffff00`. Other color pairs are commented out in
`config.def.h`. Regenerate `config.h` to adopt changed defaults, as
explained in [building](building.md#build-and-configure).

Each frame starts with a clean copy of the terminal back buffer, so tint
does not accumulate. Text and cursor updates remain live underneath.
Receiving a bell records or restarts the effect; the event loop draws its
frames, including while idle. A burst does not force extra redraws or
postpone an already due frame. Frame deadlines remain bounded under
continuous output, expiry restores the normal image, hidden windows avoid
animation wakeups, and resizing recreates overlay resources.

Run `LC_ALL=C.UTF-8 sh tests/bell.sh`. Pixel-buffer checks cover all
effects, custom colors, geometry, alpha blending, clean redraws, deadlines,
repeated bells, expiry, resize and tiny windows. Test-owned colors keep
checks independent of configured defaults. Expose handling also verifies
that a pending resize is applied only once.

## Combining characters

A cell stores one Unicode character. A zero-width character is merged into
the previously printed cell when a canonical precomposed character of the
same width exists. This preserves accents that would otherwise be
overwritten by the next character. Sequences of marks and Hangul jamo
compose where possible; marks without a suitable composition are dropped.
This is not general combining-mark rendering or shaping.

Composition updates copied text and REP (repeat the preceding character). It
shares previous-cell lookup with variation selectors. Changing a selected
base, or either half of a selected wide cell, clears the selection; dropped
marks and changes elsewhere leave it intact.

`compose.h` contains Unicode 15.1.0 canonical compositions and is included
in build dependencies and source distributions. Run `LC_ALL=C.UTF-8 sh
tests/combining.sh` for composition, cursor boundaries, wide bases, REP and
selection checks.

## Concealed and low-contrast text

`revealhidden = 1` makes SGR 8 (concealed) text visible as white on dark
red. Fixed warning colors are independent of application palette changes.
Concealed text remains visible while blinking, and reverse video swaps the
warning colors so selections and URL-copy flashes remain apparent.

`mincontrast = 3.0` replaces a glyph foreground below a 3:1 contrast ratio
with whichever of black or white contrasts better against its background.
The check uses resolved palette or RGB colors after faint and reverse
attributes. It preserves backgrounds, including spaces, and leaves explicit
underline colors alone. Stored characters and copied text are unchanged:
these options change rendering only.

Set either option to zero to disable that behavior. When retaining an older
`config.h`, add both definitions from `config.def.h` before building.

The rendering suite covers concealment, partial selections, URL flashes, RGB
and palette colors, faint/reverse/blink combinations, opt-outs, preserved
backgrounds and unchanged copying. Run `LC_ALL=C.UTF-8 sh
tests/rendering.sh`; blink, colors, presentation, emoji, URL and selection
suites cover interactions with these features.

## Bell rate limit

BEL is accepted at most once every 100 ms (`BELLGAP`), limiting audible
bells, visual-bell restarts and urgency-hint updates to about ten per
second. An unfocused bell otherwise reads and rewrites the urgency hint,
adding two X round trips for every BEL. Flooded output can no longer do that
work for every byte.

An accepted bell still restarts the visual effect. Holding Backspace at an
empty Bash prompt therefore keeps the default 150 ms bell flashing, while
separate presses still ring. To check a flood, run this and focus another
window during the pause:

```sh
sleep 3; time (head -c 300000 /dev/zero | tr '\0' '\a')
```

It should finish promptly rather than wait on an X round trip for each byte.
With the window focused and audible bell disabled, even the old path was
inexpensive; test unfocused behavior to observe the difference. The
mouse/event suite checks bell throttling and repeated accepted bells.

## Reverse-video updates

DECSCNM (`CSI ? 5 h` / `CSI ? 5 l`) marks the screen dirty rather than
redrawing synchronously for every mode change. The next normal frame renders
the latest mode, so output alternating reverse video cannot force one full
redraw per sequence. The screen can still change at frame rate, as with
other application redraws.

A manual stress check is `timeout 3 yes "$(printf '\033[?5h\033[?5l')"`. The
mouse/event suite checks dirty marking and the following frame through the
terminal parser.

## Drawing resources

Startup requires a drawing buffer, an Xft drawing context and an X
graphics context. Failure to create any of them is reported before
drawing starts. The underline graphics context is optional and created
lazily; failure omits the underline while preserving the text and
releasing temporary colors.
