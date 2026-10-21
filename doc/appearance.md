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
