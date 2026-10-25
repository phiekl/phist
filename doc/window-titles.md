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
