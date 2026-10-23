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
attributes 18, foreground and background 25 each, and underline color 26
including a default-color sentinel. Three attribute bits hold underline
style. At 16 bytes a cell, 65,536 full 200-column history rows take about
200 MiB uncompressed.

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
