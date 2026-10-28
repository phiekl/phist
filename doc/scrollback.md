# Scrollback and resizing

Keep earlier output accessible and rewrap it when the window changes size.

## Reading history

The main screen retains up to 65,536 history rows by default, controlled by
`scrollback_lines`. The mouse wheel scrolls five rows per step;
`mousescrolllines` changes that distance. Nonempty user input returns the
view to the live screen. Automatic protocol replies and empty input leave
the history view in place, and returning to the live screen keeps a
selection with its text.

While an alternate-screen program is running, scrolling back first reveals
the main screen it covered, then older history. Switching screens keeps the
same history rows in view. The last row of the main screen never wraps onto
the alternate screen.

Only output scrolling off the top of the screen enters history. Scrolling a
region below the top margin, deleting lines and explicit scroll-up requests
do not add rows. The oldest retained rows are evicted when the limit is
reached; a limit of zero disables retention.

Shift+PageUp and Shift+PageDown scroll by one screen minus one row, with a
minimum of one row. They also scroll main-screen history while an
alternate-screen program is active, so that program never receives these
keys. In configured scroll actions, a negative distance is relative to the
current screen height.

## Searching

Press Ctrl+/ and type to search the retained history and screen. Search is
literal and case-sensitive, starts with the last match in or before the
current view, and updates as you type. The current result is highlighted
in bright yellow, and other matches on screen in a muted amber.
Soft-wrapped text is searched as one line; matches never cross hard line
breaks or the boundary between the main and alternate screens. Unicode
text, wide characters and composed combining characters work as they do in
terminal output.

- Up or Ctrl+P finds the previous match; Down or Ctrl+N finds the next.
  Navigation wraps around at either end. Shift+Enter also finds the previous
  match.
- Home goes to the first match, the oldest in the history; End goes to
  the last.
- Page Up goes to the nearest match above the rows in view, and Page Down
  to the nearest below them, skipping any others in view: one match or a
  hundred. On the first or last page with matches, they go to the first or
  last match; unlike Up and Down, they do not wrap around.
- Enter closes search and keeps the result in view.
- Escape or Ctrl+C cancels and restores the view from before searching.
- Backspace or Delete removes the last character; Ctrl+U clears the query.
- Clipboard paste shortcuts insert into the query, using the usual paste
  confirmation when needed. Search input is never sent to the child process.

The search bar is styled like the paste dialog. Its sunken query field
shows a hint when empty. The right side shows the current match and total,
such as `3/17`, or `No match`. Matches are
counted from the top of the history, overlapping ones separately. The keys
follow on key caps, dropped in narrow windows to leave the field room. The
bar sits at the bottom of the window, or at the top while the match is
beneath it. Queries are limited to 1,023 UTF-8 bytes. Ctrl+/ also accepts
Shift for keyboard layouts that need it to type `/`; the bindings are in
`shortcuts` in `config.h`.

The colors are set in `config.h`, as `0xRRGGBB`, independently of the paste
dialogs:

| Setting | Meaning | Default |
| --- | --- | --- |
| `searchbarcolor` | The bar | `0x2563eb` |
| `searchbaroutlinecolor` | The bar's outline | `0x000000` |
| `searchfieldcolor` | Shade of the field | `0x000000` |
| `searchtextcolor` | The query | `0xffffff` |
| `searchcursorcolor` | The cursor | `0xffffff` |
| `searchcursortextcolor` | The hint's letter under the cursor | `0x2563eb` |
| `searchhintcolor` | The rest of the hint | `0xb4c6f4` |
| `searchstatuscolor` | The current match's number and the total, e.g. `2/5` | `0xb4c6f4` |
| `searchnomatchcolor` | `No match` | `0xffd75f` |
| `searchkeycolor` | Shade of the key caps | `0xffffff` |
| `searchkeytextcolor` | The keys' names | `0xffffff` |
| `searchmatchcolor` | Background of the current match | `0xfff028` |
| `searchmatchtextcolor` | Text of the current match | `0x000000` |
| `searchothermatchcolor` | Background of other matches | `0x705818` |
| `searchothermatchtextcolor` | Text of other matches | `0xffe4a0` |

The field and key caps are translucent, and the outline partly so, at a fixed
opacity. Regenerate `config.h` to adopt these settings, as explained in
[building](building.md#build-and-configure).

While an alternate-screen program runs, search includes the main screen
beneath it and the retained history, as well as that program's screen.
Output keeps the current match highlighted while its text remains
retained; overwriting or evicting it removes the highlight. Other matches
on screen are found again for each frame, scanning only the rows in view,
so output can add or remove them. The count is from the last search; edit
the query or navigate again to search newer output. If output removes the
current match or adds matches after a failed search, the status is blank
until the next scan. Resizing or switching screens refreshes the query and
uses the resulting view as the cancellation point.

Run `LC_ALL=C.UTF-8 sh tests/search.sh` for core search regressions.
`tests/paste.sh` also checks the trigger, query editing, input isolation,
clipboard routing, prompt drawing and allocation-failure fallback;
`tests/rendering.sh` checks highlight colors without changing stored cells.

## Resize behavior

Soft-wrapped rows belong to one logical line, which is rewrapped to fit the
new width. Hard line breaks remain breaks. Reflow is skipped when it is
unnecessary, and repeated window-size events are collapsed into the latest
size just before drawing. Dragging a window edge therefore does not rebuild
history for every intermediate event.

The active cursor and saved cursor follow their text, including a cursor
waiting to wrap after the right margin. Shrinking restores rows around the
active cursor rather than keeping only the bottom rows and losing its text.
A saved cursor below the content does not keep empty rows on screen. Reflow
includes retained history through the last row that can remain visible with
the active cursor.

The alternate screen keeps its contents across resizes and drops top rows
only as needed to keep the cursor visible. Its copied rows do not retain
stale wrap flags. Modes 47 and 1047 share the active cursor; 1049 alone
saves and restores it. While the alternate screen is active, the main-screen
cursor is tracked separately from the DECSC saved cursor.

Tab stops stay inside the resized width. New columns continue the last
stop's spacing; a zero `tabspaces` setting is treated as one.

## Text, selections and printing

Selections follow text into history, through regional scrolling and across
screen switches, even while out of view. They end when their rows are
evicted, when text scrolls away with history disabled, or on resize, because
reflow can move text between rows.

Wide characters stay with their spacing cells, even through a one-column
window. Unused margin cells are marked as padding and omitted from copying,
printing and later reflow. Wide-character output wraps are soft breaks and
preserve text at the margin.

Unstyled trailing spaces on the default background are trimmed regardless of
foreground color. Reverse, underline and strikethrough padding remain.
Storage width, reflow, content-height scans and wide-character wrapping all
use this same padding rule.

Drawing damage, selection checks and blinking-text scans use the rows
actually displayed. Printing the screen likewise prints displayed rows; `CSI
1 i` prints the cursor's row. Dumps omit wide-character dummy cells.

## Storage and checks

History uses a ring buffer. Its retention limit is separate from the
capacity of the row index: resizing can temporarily push screen rows into
history and pop them back even with no retained history, or a limit smaller
than the screen. Rows omit trailing blank cells. Short rows are padded in a
small ring of scratch buffers when read, so viewing or selecting history
does not enlarge stored rows. Normal output, limit changes and reflow share
the same oldest-row eviction logic.

Run the headless regressions from the repository root:

```sh
LC_ALL=C.UTF-8 sh tests/history.sh
LC_ALL=C.UTF-8 sh tests/resize.sh
LC_ALL=C.UTF-8 sh tests/drawing.sh
LC_ALL=C.UTF-8 sh tests/printing.sh
```

The shared helpers in `tests/terminal.h` exercise the real terminal core.
Resize checks cover limits 0, 1, 4, 64 and 65,536, cursor mapping,
alternate-screen modes and contents, tabs, padding and wide characters.

## Clears and resets

A full main-screen erase preserves its used rows in history. This covers
`CSI 2 J` and `CSI 0 J` with the cursor at home, so `clear` and Ctrl+L leave
the previous view accessible. Trailing empty rows are omitted. The last
saved logical line is ended even if its final physical row was wrapped;
later output cannot continue a URL through the clear boundary.

The cursor position and scrolling margins stay intact. Partial erases keep
their normal behavior, and alternate-screen clears do not enter history. A
clear returns to the live view and invalidates the URL overlay. Selected
text moved into history remains selected.

`CSI 3 J` (ED 3, erase scrollback) is ignored and reports `erresc: ED 3
(erase scrollback) ignored`. Application output cannot use it to erase the
record of earlier output. The terminfo entry has no `E3` capability; `clear`
and Ctrl+L use `CSI H CSI 2 J`, which preserves the screen in history.

RIS (`ESC c`), also sent by `reset` through terminfo's `rs1=\Ec`, leaves the
alternate screen if needed and saves the main screen to history before
resetting modes, charsets, colors, tabs, cursor and scroll region. Existing
history remains available above the clean screen. The alternate screen's
contents are not added to history. Internally, startup's `treset()` still
discards history; RIS from output calls `tresetstate()`, which resets
everything but history.

Run `seq 1 200; printf '\033c'`, then scroll back: both earlier history and
the former main screen remain accessible. `tests/clear.sh` covers reset from
both screens as well as ED 3 and full-screen clearing.

To check ED 3, run `seq 1 200; printf '\033[3J'` and scroll back with the
wheel or Shift+PageUp: the earlier lines remain accessible.

Run `LC_ALL=C.UTF-8 sh tests/clear.sh` and `LC_ALL=C.UTF-8 sh
tests/colors.sh`. Checks cover small limits, wrapped URLs, underline
attributes, margins and erase modes, including underline round trips through
full-screen clearing.

## Compressed rows

Stored history separates Unicode text from runs of identical formatting, in
one allocation per row. Uniformly styled output no longer repeats a full
cell's attributes for every character. Each run is 16 bytes, matching the
cell attribute bit widths, with a 21-bit end offset. Rows fall back to raw
cells if encoding would cost more or their width exceeds that offset. The
65,536-row retention default is unchanged.

Rows decode for display, selection, screen restoration and resize reflow.
Word snapping reuses a decoded row, fetching its length and successor at row
boundaries rather than decoding it for every selected cell.

Run `LC_ALL=C.UTF-8 sh tests/history-storage.sh` for randomized codec,
clipping/padding, full-capacity storage and eviction checks. To compare
storage and core processing costs:

```sh
sh tests/history-bench.sh . plain
sh tests/history-bench.sh . colored
sh tests/history-bench.sh . varied
sh tests/history-bench.sh /path/to/older/source plain
```

The benchmark reports load time, peak resident memory, 10,000 scrolls and a
resize pair. It exercises parsing/history and URL scans headlessly,
excluding X rendering. The optional source tree lets the same workload
measure an older implementation; results depend on machine and workload,
especially how often formatting changes.
