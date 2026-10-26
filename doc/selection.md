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
