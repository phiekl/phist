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
text moved into history remains selected. At this point, `CSI 3 J` and a
full terminal reset still discard history and end its selection.

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
