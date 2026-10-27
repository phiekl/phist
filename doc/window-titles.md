# Window titles

Keep application titles suitable for task bars and window lists.

## Length and line breaks

Titles from OSC 0/1/2 and legacy `ESC k` are limited to 512 bytes without
splitting a UTF-8 sequence. C0 control characters, including tabs and
newlines, become spaces. U+2028 LINE SEPARATOR and U+2029 PARAGRAPH
SEPARATOR also become a space before the byte limit is applied. NEL (U+0085)
is a C1 control and already terminates the string.

Titles appear outside the terminal in task bars and window lists. Keeping
them short and on one line prevents embedded line breaks from creating
misleading entries and limits traffic from very large title strings.

To inspect a title, run the setter and `xprop` in the same shell command,
with a pause for phist to process it:

```sh
sh -c 'printf "\033]0;fake\nroot@host: ~\a"; sleep 0.5; xprop -id "$WINDOWID" _NET_WM_NAME'
sh -c 'printf "\033]2;first\342\200\250second\a"; sleep 0.5; xprop -id "$WINDOWID" _NET_WM_NAME | od -c'
```

The titles become `fake root@host: ~` and `first second`. `WINDOWID` is
exported by phist. A shell prompt, preexec or DEBUG hook can overwrite a test
title before a separate inspection command runs, so keep both inside `sh
-c`. `xprop` adds its own label and escaping; its output length is not the
raw title's byte length.

Run `LC_ALL=C.UTF-8 sh tests/sequences.sh` for title filtering and length
regressions.

## Bidirectional controls

Explicit bidi controls are removed from application titles: LRM, RLM and ALM
(U+200E, U+200F, U+061C), embeddings/overrides U+202A–U+202E and isolates
U+2066–U+2069. Left in, they would make window managers display text in a
misleading order, such as an executable name looking like a PDF. Ordinary
right-to-left text still uses the window manager's normal bidi rendering.

`strtitle()` decodes UTF-8 rather than relying on byte-pattern matching. For
an inspection example:

```sh
sh -c 'printf "\033]2;invoice\342\200\256fdp.exe\a"; sleep 0.5; xprop -id "$WINDOWID" _NET_WM_NAME | od -c'
```

The stored title is `invoicefdp.exe`, with no U+202E bytes (`342 200 256`).
Inspect the bytes, as the control itself is invisible, and keep setting and
inspecting the title in one shell command to avoid prompt hooks. The
sequence suite covers these removals.

## Invisible characters

Application titles drop default-ignorable characters that can make two
titles look identical while differing to name-matching scripts such as
`xdotool search --name`. Examples include U+200B ZERO WIDTH SPACE, U+2060
WORD JOINER, U+FEFF, U+00AD SOFT HYPHEN, Hangul fillers and tag characters
U+E0000–U+E007F. Subdivision flags using tags consequently appear as a plain
black flag.

U+200C/U+200D joiners and variation selectors are retained because emoji and
scripts such as Persian and the Indic scripts need them. This includes
U+FE00–U+FE0F, U+E0100–U+E01EF and Mongolian selectors U+180B–U+180D/U+180F.
The exact removal ranges are in `titledrop()`.

```sh
sh -c 'printf "\033]2;ro\342\200\213ot\a"; sleep 0.5; xprop -id "$WINDOWID" _NET_WM_NAME | od -c'
```

The title becomes plain `root`, without the ZERO WIDTH SPACE bytes `342 200
213`. Run the sequence suite for filtering regressions.

## Combining marks

At most four combining marks and joiners (U+200C/U+200D) are retained after
each character. A title can otherwise stack hundreds of marks even within
512 bytes, drawing over neighboring task-bar entries. Four allows common
stacked Vietnamese, Hebrew and Tibetan marks. Each emoji starts a new count,
so ordinary emoji ZWJ sequences are unaffected.

`combining.h` is a sorted table of Unicode categories Mn, Me and Mc,
generated from Python `unicodedata` using Unicode 15.1.0, matching
`compose.h`. The filter uses this table instead of `wcwidth()`: phist still
decodes UTF-8 in a C locale, where locale-dependent width lookup does not
recognize the marks. Tests check both C.UTF-8 and C.

To inspect, set a title containing `x` followed by many U+0336 marks in one
`sh -c`, wait, and read `_NET_WM_NAME` with `xprop` as above. Only four
marks should remain. Count characters, not bytes: `xprop` in the C locale
may show escaped octal bytes rather than Unicode. Run `LC_ALL=C.UTF-8 sh
tests/sequences.sh`.

## Update scheduling

`xsettitle()` and `xseticontitle()` retain the latest pending title; `run()`
applies it once per draw cycle. A flood of OSC 0/1/2 or `ESC k` updates
therefore sends the latest value at the normal drawing cadence, roughly 30
times per second with continuous output and the default `maxlatency` of 33
ms, rather than once per input sequence.

Pending titles flush even when the window is hidden, keeping task-bar
entries current, and before the window is first mapped. This reduces X
traffic and window-manager redraws without leaving the final title stale.

For a manual check, watch a task-bar entry while running:

```sh
timeout 3 sh -c 'i=0; while :; do printf "\033]2;flood %d\a" "$i"; i=$((i+1)); done'
```

The mouse/event suite (`LC_ALL=C.UTF-8 sh tests/mouse.sh`) checks pending
title replacement and draw-time flushing, in addition to parser filtering in
the sequence suite.
