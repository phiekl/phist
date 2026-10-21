# Scrollback and resizing

Keep earlier output accessible and rewrap it when the window changes size.

## Reading history

The main screen retains up to 65,536 history rows by default, controlled by
`scrollback_lines`. The mouse wheel scrolls five rows per step;
`mousescrolllines` changes that distance. Anything sent to the shell,
whether typed input or a reply to a query, returns the view to the live
screen.

While an alternate-screen program is running, scrolling back first reveals
the main screen it covered, then older history. Switching screens keeps the
same history rows in view. The last row of the main screen never wraps onto
the alternate screen.

Only output scrolling off the top of the screen enters history. Scrolling a
region below the top margin, deleting lines and explicit scroll-up requests
do not add rows. The oldest retained rows are evicted when the limit is
reached; a limit of zero disables retention.

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
