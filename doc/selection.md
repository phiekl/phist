# Text selection and clipboard

Select words, paths and logical lines, and copy text to other programs.

## Expanding a selection

Drag with the left button to select text. Double-click to select the
smallest word or configured match under the pointer; further clicks at the
same cell grow through distinct matches from shortest to longest, up to the
complete logical line. A click that cannot grow the selection starts the
sequence over immediately. Hold Shift when selecting on a clickable URL.

The initial pair uses `doubleclicktimeout` (300 ms); later clicks use
`tripleclicktimeout` (600 ms). Moving to a different cell, changing
modifiers or dragging resets the sequence.

`selectionpatterns` contains POSIX extended regular expressions; the first
capture group supplies the selection. Defaults cover words and email
addresses, paths, assignments, phrases and complete lines. For example,
successive selections in `LC_ALL=C.UTF-8` grow from `LC_ALL` to
`LC_ALL=C.UTF-8` before including neighboring assignments. Paths may be absolute,
relative, home-relative or parent-relative, with single-character
components, final `.` or `..`, and optional trailing slashes. Internal
filename dots remain selected; sentence-ending periods are excluded.
Ordinary word selection remains a candidate for Unicode and punctuation.
Patterns compile at startup so errors appear before the shell starts.

Soft-wrapped rows are matched as one logical line, even beyond the visible
history viewport. UTF-8 matches map back to cells without losing wide
characters or wrapped spaces, and equal candidates are deduplicated.
Matching examines at most 4,096 cells on either side of the click, using
`REG_STARTEND` where available. A candidate touching a cut end is replaced
by the whole logical line, keeping long wrapped lines practical.

When regional scrolling moves selected text intact, the existing bounds move
without rerunning the regexes. If scrolling changes selected text or drops
selected rows across a margin, the selection is cleared. Selections
otherwise follow their text through history, screen switches and typing;
resizing ends them because text can reflow between rows.

Selection endpoints are clamped to the nearest screen cell before word
or line snapping. A column just beyond the right edge therefore cannot
read past the row.

Run `LC_ALL=C.UTF-8 sh tests/selection.sh` for terminal and mouse checks,
including these movement rules and multi-click expansion.

## Keyboard selection

Press Ctrl+Alt+V to select text with Vim's keys, without the mouse. A
cursor appears on the terminal's cursor, or on the bottom row when scrolled
back, and the window is tinted. A bar across the bottom names the mode until
the next key, and Ctrl+V or a change between `v`, `V` and normal brings it back.
Every key belongs to the mode until you leave it, so nothing typed reaches the
child process; only the font size shortcuts still work.

Move around the screen and the retained history; the view scrolls to follow.
Counts such as `3w` work on motions and `3n` on search repeats, up to 99,999.

| Keys | Moves to |
|------|----------|
| `h` `j` `k` `l`, arrows | a neighboring cell or row |
| `w` `b` `e`, `W` `B` `E` | the next word, its start or its end; capitals count only blanks as separators |
| `0` `^` `$`, Home, End | the start, first text or last text of the logical line |
| `gg` `G` | the first row of the history, or the last with text; a count picks a row |
| `H` `M` `L` | the top, middle or bottom of the view |
| `f` `F` `t` `T` + a character | that character in the logical line, or just before it; `;` and `,` repeat |
| `/` `?`, then `n` `N` | a [search](scrollback.md#searching) result, and the next or previous one |

Ctrl+U and Ctrl+D move half a screen, Ctrl+B, Ctrl+F, Page Up and Page Down
nearly a whole one. Ctrl+Y and Ctrl+E scroll by a row, leaving the cursor where
it is unless that would put it out of view. The wheel also scrolls; clicks do
nothing.

`v` selects text, `V` whole lines, and Ctrl+V a block. Pressing another of them
changes the kind, the same one ends the selection, and `o` jumps to its other
end. `y` or Enter copies the selection to PRIMARY and CLIPBOARD and leaves the
mode, keeping the selection highlighted as after a drag. Without a selection,
`yy` or `Y` copies the logical line, or several with a count; soft wraps do
not consume the count. Whole lines always end in a newline when copied, even if
the last one fills the width.

Escape ends the selection, then the mode. `q`, `i`, `a` and Ctrl+C leave at
once. Leaving returns to the view the mode started from.

The appearance is set in `config.h`, independently of the search bar's. Colors
use `0xRRGGBB`:

| Setting | Meaning | Default |
| --- | --- | --- |
| `vimtintcolor` | Tint over the window | `0xe3436b` |
| `vimtintopacity` | The tint's opacity from 0 to 1 | 0.2 |
| `vimlabelcolor` | Background of the bar | `0xa976ba` |
| `vimlabeltextcolor` | The mode's name | `0x2d0d38` |
| `vimlabeloutlinecolor` | Outline of the bar | `0x633b70` |
| `vimlabelfadeopacity` | The bar's opacity at the right, from 0 to 1 | 0.05 |

Regenerate `config.h` to adopt them, as explained in
[building](building.md#build-and-configure). The selection and the cursor
reverse the cells' own colors, and a search opened here keeps its own.

Unlike Vim, the cursor moves freely over the blank cells past the end of a
line, like a mouse drag: a selection ending there includes the line break, as
do `V`, `yy` and `Y`. Soft-wrapped rows are one line to `0`, `^`, `$`, words,
`f` and `V`, but `j` and `k` move by screen row. There are no other operators,
registers, marks or text objects.

Output can continue meanwhile. The cursor and selection stay on their text,
and the view stops following output once the cursor would scroll out of it.
Overwriting selected text or evicting it from the history ends the selection,
as with the mouse. Resizing the window also ends it, as reflow moves the text,
and keeps the cursor at the same place on screen. Switching between the main
and alternate screens, or a terminal reset, ends the mode.

Run `LC_ALL=C.UTF-8 sh tests/vim.sh` for the motions and selections, including
wrapped and wide text, live output and random keys. `tests/paste.sh` checks
the keys, the mode's name and the copy, and `tests/rendering.sh` the cursor.

## Local mouse actions

Application mouse-tracking requests are ignored. The terminal keeps
left-click URL copying, dragging and multi-click selection, middle-click
PRIMARY paste, and wheel scrolling. Wheel steps use `mousescrolllines`
(default five), including main-screen history beneath an alternate-screen
program. Full-screen applications cannot receive mouse clicks or wheel
reports from phist.

The mouse-report encoder and its mode flags are removed, and `st.info` omits
`kmous`. Run `make install` to install the updated terminfo entries;
otherwise applications may still discover an older entry advertising mouse
support. [Focus reporting](terminal-behavior.md#focus-reporting) is also
ignored. Bracketed paste remains supported; actual paste returns to the live
screen.

Run `LC_ALL=C.UTF-8 sh tests/mouse.sh`. It verifies that tracking requests
emit no mouse bytes, local selection/URL feedback/paste/scrolling still
work, and all terminfo variants omit mouse capabilities.

## Sending large selections

PRIMARY and CLIPBOARD selections that fit in one X `ChangeProperty` request
are sent directly. Larger selections use INCR: phist announces an incremental
transfer, waits for the requestor to delete each property, sends the next
chunk and ends with an empty chunk. This applies both to terminal/history
selections and to copies from the paste's full view. There is no separate
copy-size refusal.

The maximum direct request is derived from `XExtendedMaxRequestSize()`, or
`XMaxRequestSize()` without BIG-REQUESTS, in four-byte units. The
calculation conservatively reserves 24 bytes for the request header and four
for the extended length. Typical ceilings are about 16 MiB with BIG-REQUESTS
and 256 KiB without it, but the server's reported limit is authoritative.
Previously exceeding it could trigger an X error or disconnect phist. Escaping
a paste can expand it fourfold, so even text within the paste limit can
exceed this ceiling.

Each active transfer owns a snapshot of the requested text; a later
selection change cannot alter its remaining chunks. Several transfers may
run concurrently, including to one window. Restarting a request for the same
window/property replaces that transfer. A transfer with no progress for five
seconds is dropped. This outgoing timeout is separate from the configurable
incoming-paste timeout.

As any client on the display can ask for transfers, and each owns a copy of the
text, at most 16 can run at once, 4 of them to one window, holding 256 MiB
together. A request beyond that is refused like any failed conversion, with a
`None` property in its reply. A transfer starting while no other runs may be of
any size, as long as there is memory for its copy. Selections sent directly
are not limited, as phist keeps no copy.

phist watches property changes on requestor windows and restores the event mask
it previously selected once the last transfer to that window ends. Existing
watches, including those on phist's own paste windows, survive. Requestors may
disappear at any time: the X error handler ignores `BadWindow` for other
clients' windows, and `BadDrawable` specifically for `GetGeometry` requests,
which fail that way if the window goes during an attribute lookup. Other errors,
including drawable errors from rendering and a bad window of phist's own such as
a stale `-w` ID, go to Xlib's previous handler.

Run `LC_ALL=C.UTF-8 sh tests/selsend.sh`. Checks cover BIG-REQUESTS and
ordinary limits, direct requests at the boundary, chunked snapshots,
unrelated property events, concurrent transfers to one window, existing
event masks, restarts, timeouts, limits, vanished requestors and error
handling.
`tests/paste.sh` covers copying from the full view.

The buffer for a selection is sized in `size_t`, so one spanning a very large
`scrollback_lines` is not limited by the range of an `int`. Run
`LC_ALL=C.UTF-8 sh tests/selection.sh` to check.
