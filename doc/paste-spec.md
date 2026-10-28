# Paste confirmation spec

How phist asks before pasting text with newlines, tabs, control characters
or invalid UTF-8, and what each choice pastes.

## 1. Character classes

The paste is decoded as UTF-8 independently of the locale (strict: no
overlong forms, surrogates or code points above U+10FFFF). Each character
falls into exactly one class:

| Class   | Characters                                                  | Counted as  |
|---------|-------------------------------------------------------------|-------------|
| LF      | `\n` (0x0a) not preceded by CR                              | `LF`        |
| CRLF    | `\r\n` pair                                                 | `CRLF`      |
| TAB     | `\t` (0x09)                                                 | `TAB`       |
| DANGER  | lone `\r` (CR not followed by LF)                           | `DANGER!`   |
| DANGER  | other C0 controls 0x00-0x1f (incl. ESC, NUL, Ctrl+C), DEL 0x7f | `DANGER!` |
| DANGER  | C1 controls U+0080-U+009F (encoded as `c2 80`-`c2 9f`)      | `DANGER!`   |
| DANGER  | any byte that isn't part of a valid UTF-8 sequence          | `DANGER!`   |
| SPACE   | ` ` (0x20)                                                  | not counted |
| PLAIN   | everything else, including zero-width and bidi characters (e.g. U+200B, U+202E) | not counted |

Each counted character counts once (a CRLF pair counts once as `CRLF`).
Each invalid byte counts as one DANGER.

## 2. When the dialog appears

- Empty paste: nothing is sent.
- No LF, CRLF, TAB or DANGER: pasted immediately and unchanged (no trimming).
- Otherwise: the dialog appears and nothing is sent until a choice is made.

Dialog color:

| Condition                 | Color  | Text  | Title bar   | Title                                       |
|---------------------------|--------|-------|-------------|---------------------------------------------|
| any DANGER                | red    | gold  | dark red    | Paste contains dangerous characters         |
| else TAB and CRLF         | yellow | black | dark yellow | Paste contains tabs and CRLF line endings   |
| else TAB                  | yellow | black | dark yellow | Paste contains tabs                         |
| else CRLF                 | yellow | black | dark yellow | Paste contains CRLF line endings            |
| else (LF only)            | blue   | white | dark blue   | Paste contains multiple lines               |

## 3. Steps

"Blank" below means space, TAB or LF.

`s` mode:

1. Replace CR, LF, TAB and DANGER characters with spaces.
2. Collapse each run of spaces to one space.
3. Trim leading and trailing spaces, including indentation.

`p` mode:

1. Replace CRLF with LF.
2. Replace all TAB with 4 spaces.
3. Replace each run of DANGER with a single space, but drop it instead if
   the character directly before or after it is a blank, or if it is at
   the start or end of the paste.
4. Trim all space prior to any LF and at the end of the paste, then
   leave at most one LF at the very end.

`t` mode:

1. Replace CRLF with LF.
2. Do NOT replace TAB.
3. Replace each run of DANGER with a single space, but drop it instead if
   the character directly before or after it is a blank, or if it is at
   the start or end of the paste.
4. Trim all space/TAB prior to any LF and at the end of the paste, then
   leave at most one LF at the very end.

`K` mode:

No modification is done.

Delivery, for every choice:

1. Bracketed paste mode is read once, at the moment of sending. If it is
   on, the text is wrapped in `ESC[200~` ... `ESC[201~` and every ESC
   inside is removed (only K can still contain ESC).
2. Each LF is sent as CR, as usual for terminal input.

## 4. Which choices are shown, and their letters

- `s` is always displayed.
- `p` is always displayed.
- `t` is only displayed if there is a TAB in the original paste.
- `K` is only displayed when neither `p` nor `t` would end up the same as `K`.
- Choosing `K` (by key or Enter) selects it and does not paste yet: a
  box over the dialog, styled like it, has the title bar `Confirm pasting
  data unmodified`, centered with equal room on both sides, and two
  choices side by side and centered below it, `N No` on the left and
  `Y Yes` on the right, without the `▸` marker, with No selected.
  Everything behind the box, the dialog included, is darkened (black at
  about 63%) to show that it must be answered first.
  Left, Up and Home select No; Right, Down and End select Yes; Tab and
  Shift+Tab switch between them; Enter applies the selection; `y` pastes
  the original, `n` and Esc go back to the dialog, Ctrl+C cancels the
  paste. The box drops rows in small windows like the dialog.
- Keys are shown in uppercase and work in either case.

## 5. Labels

A choice's label describes the changes made and the text retained, joined
by `, `. The first character is uppercase (e.g. `Keep LF, keep TAB`), with
no final period. Omit transformations that leave the text unchanged.

`s` mode:

- <CHARACTER_TYPE>[|<CHARACTER_TYPE>].. → SPACE (types present, in the
  count table order)
- squeeze all space (only for runs inside the text; a run at the end is
  just trimmed)
- trim surrounding space

`p` mode:

- DANGER → SPACE
- keep LF (when the paste has LF outside CRLF)
- CRLF → LF
- trim trailing space (also extra LF at the very end)
- TAB → 4x SPACE

`t` mode:

- DANGER → SPACE
- keep LF (when the paste has LF outside CRLF)
- CRLF → LF
- trim trailing space (also extra LF at the very end)
- keep TAB

`p` and `t` share every part but the last, so their labels only differ at
the end.

`K` mode:

It says what it keeps that the other choices would change, in this
order, each only if present:

- keep DANGER
- keep LF
- keep CRLF
- keep trailing space (when `p` would trim something, extra LF at the
  very end included)
- keep TAB

e.g. `Keep DANGER, keep CRLF, keep TAB` or `Keep LF, keep trailing space`.
A `p` or `t` choice that changes nothing gets the same kind of label,
e.g. `Keep LF` or `Keep LF, keep TAB`.

## 6. Dialog contents

- Title bar: the top row, in a darker shade of the dialog color, with the
  title from the table in section 2 in the text color. It is a little
  taller than a text row: it extends a quarter row above it, plus enough
  to balance the space above capitals against the font's descent, and a
  quarter row below it if a blank row follows. Nothing blinks.
- Chips: the size, the number of characters and the counts, each as a
  two-tone chip: the number on a block in the title bar's darker shade,
  the unit on a darker fill like the preview field's, both in the text
  color, the same size, and each part padded by a cell on each side.
  Given a blank row above, both parts reach up into it so that the space
  above the digits matches the font's descent below them. Chips are a
  cell apart. In the
  order `<n> bytes`, `<n> characters`, `LF`, `TAB`, `CRLF`, `DANGER!`,
  listing only classes present, e.g. `17 bytes` `16 characters` `1 TAB`.
  Characters are Unicode characters; each invalid byte counts as one and
  CRLF as two. `1 byte` and `1 character` are singular.
- Layout of each row: the marker at column 0, the key cap over columns
  2-4 with the key in column 3, the label from column 7. The title text
  and the first chip also start at column 2. Columns count from the
  dialog's inner edge, after its padding.
- Preview: `V View` followed by a sunken (darker) field with one line of
  the paste, escaped: `\n \r \t \\`, `\xNN` for controls and invalid
  bytes, `\uXXXX` for C1, non-printable and zero-width characters; `...`
  when truncated. Like the chips, the field reaches up into a blank row
  above it, so it is the same height as the chips. It is selectable like
  the choices; when selected, the field is inverted. In narrow dialogs the
  `V View` key and label are left out, leaving only the field.
- Choices, the first (safest) selected. The selected row (choice or
  preview) is marked with `▸`, and only its label (or the preview field)
  is inverted: text color background, dialog-color letters.
- Keys (`V`, `S`, `P`, `T`, `K`) are shown in uppercase on raised key caps
  three cells wide. Choices are a blank row apart, so like the chips and
  the preview field, each key cap and the selected inverted label reach
  up into the blank row above them and are as tall.
- No help text.
- Small windows drop, in this order: padding, the blank rows between
  choices, other blank lines, chips, title bar, preview. Choices then
  scroll, with ▲/▼ markers.
- Full view: the same title bar, the paste wrapped and split at newlines,
  and the scroll position (e.g. `42%`) flush right below it.

## 7. Keys

| Key                         | In the dialog                          | In the full view |
|-----------------------------|----------------------------------------|------------------|
| `s`, `p`, `t` (either case) | apply that choice                      | ignored          |
| `v` (either case)           | open the full view (no delay); the preview stays selected | back to the dialog |
| `k` (either case)           | ask before pasting the original        | ignored          |
| Up / Down                   | move the selection; Up from the first choice selects the preview | scroll one row |
| Tab / Shift+Tab             | like Down / Up                         | ignored          |
| Home / End                  | first / last choice                    | top / bottom     |
| PgUp / PgDn, Space          | -                                      | scroll a page    |
| mouse wheel                 | scroll terminal history                | scroll `mousescrolllines` rows |
| Enter, keypad Enter         | apply the selected choice (asking first for `K`), or open the full view on the preview | back to the dialog |
| `q`                         | -                                      | back to the dialog |
| Esc                         | cancel                                 | back to the dialog |
| Ctrl+C                      | cancel                                 | cancel           |
| other Ctrl/Alt combinations | ignored                                | ignored          |

Choices, by key or Enter, are ignored for 500 ms (`pasteenterdelay`)
after the dialog appears, and `y` or Enter on Yes for as long after the
question before pasting the original appears, so that a key press meant
for the terminal cannot choose. Moving the selection, the full view,
going back and cancelling are never delayed.

The dialog is modal: mouse clicks do nothing while it is open, neither
when pressed nor when released, even if it closes in between, and
further paste requests are ignored while a paste is being transferred or
confirmed. A drag begun before the dialog appeared still ends normally. The
wheel still scrolls, as in the table above, but its press and release
run no mouse binding either.

The dialog and failure box are drawn on a fresh copy of the terminal
buffer, shared with the visual bell. Terminal output behind them stays
live and redraws only dirty lines; the translucent tint never accumulates
in the terminal buffer. If the presentation pixmap or its drawing context
is unavailable, the boxes are drawn directly on the window with the
terminal's existing drawing context after copying the untinted buffer.
Closing a box restores the next frame without forcing a full terminal
repaint.

## 8. Limits and failures

- Pastes over 4 MiB (`pastemaxbytes`) are cancelled.
- A transfer making no progress for 3 s (`pastetimeout`) is cancelled.
- So is a transfer with invalid data, e.g. a reply of a format other
  than 8 bits or changing its type midway.

A cancelled paste sends nothing and is explained in a red box, styled
like the dialog, with the title bar `Paste failed` and the reason, both
centered:

| Cause                | Reason shown                                   |
|----------------------|------------------------------------------------|
| too large            | `The paste is larger than 4194304 bytes` (`pastemaxbytes`) |
| no progress          | `The selection owner stopped answering`        |
| invalid data         | `The selection owner sent invalid data`        |

The box shows for 5 s. Esc, Ctrl+C or Enter close it sooner, but not
within 500 ms (`pasteenterdelay`) of it appearing. While it is shown, it
is modal like the dialog: the wheel scrolls history, but other keys and
mouse clicks do nothing, except
that the paste bindings (Ctrl+Shift+V, Shift+Insert, middle-click, or
whatever config.h binds to clippaste and selpaste), with their
modifiers, retry at once, replacing the box. Such a click does nothing
else, e.g. start a selection. An empty or refused selection is not a failure and
shows nothing. The reason is also written to stderr, as `Paste failed:
<reason>`.

An incremental transfer cancelled for its size or invalid data is still
drained: its remaining chunks are deleted unread until the owner sends
the empty last one, or stops for 3 s. Owners such as xclip answer no
other request from phist until then, so a paste meanwhile waits for the
drain to end before it is requested. A drain ends after `pastetimeout` without a
chunk or four times that in all, whatever the owner sends, and then retires its
window, so a new owner can answer the waiting request.

Each selection request uses a fresh, nonzero X server timestamp, obtained
through a property notification even for programmatic pastes. If it equals
the previous request's time, phist waits for the next server tick. Replies,
including refusals, must echo that timestamp as specified by ICCCM; replies
with a different time or `CurrentTime` are ignored and eventually time out
if no matching reply arrives. Accepting such refusals would allow an old
request to cancel a new one.

Conversions are requested into the property `_ST_PASTE` of a small
unmapped window of phist's own. Completed requests, including refusals and
replies that were read but failed validation, reuse their window. A
request abandoned before its owner finished, by a timeout, a reply that
could not be read or a drain that did not complete, retires its window:
the abandoned owner may still write a late reply or INCR chunks to it,
even over a newer reply before phist reads it, so later requests use a new
window. Retired windows are kept, not destroyed, since an owner writing
to a destroyed window gets an error that some programs exit on. They are
freed when phist exits, unlike interned atoms, which last as long as the X
server.

## 9. Examples

Each is checked by the tests (`tests/paste.c`).

| Paste                              | Color  | Choices                                                     |
|------------------------------------|--------|-------------------------------------------------------------|
| `test\nexample\n`                  | blue   | s `test example`; p original (`Keep LF`)      |
| `line1    \nline2\n   `            | blue   | s `line1 line2`; p `line1\nline2\n`; K original           |
| `  a\nb  `                         | blue   | s `a b`; p `  a\nb`; K original                           |
| `test\texample`                    | yellow | s `test example`; p `test    example`; t original         |
| `te\tst\r\nexample\r\n`            | yellow | s `te st example`; p `te    st\nexample\n`; t `te\tst\nexample\n`; K original |
| `a \tb  c\n\n d \n`                | yellow | s `a b c d`; p `a     b  c\n\n d\n`; t `a \tb  c\n\n d\n`; K original |
| `\t` (single tab)                  | yellow | s, p and t (empty); K original                            |
| `test\nexample\0`                  | red    | s `test example`; p `test\nexample`; K original |
| `a\rb\r\r\n`                       | red    | s `a b`; p `a b\n`; K original                  |
| `\e[31mred\e[0m\n`                 | red    | s `[31mred [0m`; p `[31mred [0m\n`; K original  |
| `a \0b\1\2c\3 d\n\4e\5`            | red    | s `a b c d e`; p `a b c d\ne`; K original       |
| `\tx\e\ty`                         | red    | s `x y`; p `    x    y`; t `\tx\ty`; K original |
| `caf\xe9 au lait\n` (Latin-1)      | red    | s `caf au lait`; p `caf au lait\n`; K original  |
| `\e` (single ESC)                  | red    | s and p (empty); K original                     |
| `line\n\n \n\r\n`                 | yellow | s `line`; p `line\n`; K original                          |
| `a\tb\n\n\n`                       | yellow | s `a b`; p `a    b\n`; t `a\tb\n`; K original             |

## 10. Implementation and verification

`paste.c` is included by `x.c` because it shares the window, drawing
context, static helpers and static `config.h` settings. It buffers a
complete selection before classifying it, so UTF-8 characters, CRLF and
control sequences split across INCR chunks are treated exactly like a
single property reply. An INCR announcement starts a transfer regardless
of its size hint; owners such as xclip may supply no useful hint.

The strict decoder rejects overlong forms, surrogates and values above
U+10FFFF. A locale decoder such as `mbrtowc()` would reject ordinary
non-ASCII text in C, while some implementations accept out-of-range
four-byte forms, which can hide dangerous bytes. The dialog's own arrow and
marker symbols occupy one column even where locale width lookup does
not recognize them.

X replies must have 8-bit format; STRING, TEXT, COMPOUND_TEXT and other
types are accepted, but all chunks must share a type. Reply targets are
not checked, since phist requests only one and some owners do not echo it.
Timestamp and requestor-window checks, draining and retirement in section
8 prevent late replies or refusals from corrupting a later request.

Buffering also means one bracket pair covers the entire transfer, rather
than one per chunk plus an empty pair at the end. Bracketed-paste mode
is captured once for filtering and both markers, so application mode
changes cannot leave inconsistent markers.

Run `LC_ALL=C.UTF-8 sh tests/paste.sh`. Mocked X transfers and drawing use
real terminal parsing and tty writes. Coverage includes every C0 control,
DEL, C1 characters and stray bytes, malformed UTF-8 and C-locale decoding;
every example and label with and without brackets; choice delays and
case, confirmation, modal mouse behavior and PRIMARY claims; split INCR
data and mode changes; reply formats, types, targets, timestamps and late
replies; requestor reuse/retirement, unreadable replies and draining;
limits, timeouts and failure/retry behavior; all window sizes through
80 by 20, fixed colors, fallback drawing, partial redraws and scrolling
a 1 MiB line. The suite can also run with UndefinedBehaviorSanitizer via
`CFLAGS='-g -O1 -fsanitize=undefined'`.
