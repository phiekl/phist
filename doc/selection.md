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
addresses, paths, phrases and complete lines. Paths may be absolute,
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

Run `LC_ALL=C.UTF-8 sh tests/selection.sh` for terminal and mouse checks,
including these movement rules and multi-click expansion.

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
