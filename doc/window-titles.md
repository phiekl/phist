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
