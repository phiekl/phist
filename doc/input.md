# Keyboard input and terminal replies

How user input and automatic protocol replies reach the child process.

## Input and replies

Keyboard input, mapped keys, configured send actions and paste use
`ttyinput()`. Only nonempty user input returns a scrolled view to the live
screen; a selection remains attached to its text. Protocol replies use a
separate path, so cursor-position queries leave the history view in place.
OSC color replies are not locally echoed.

Run `LC_ALL=C.UTF-8 sh tests/replies.sh` for reply and empty/nonempty-input
regressions. History selection behavior is also covered by
`tests/history.sh`.

## Delete key

Delete and keypad Delete send the sequence advertised by the default
terminfo entries in either normal or application keypad mode:

| Key | Bytes |
| --- | --- |
| Delete | `ESC [ 3 ~` |
| Ctrl+Delete | `ESC [ 3 ; 5 ~` |
| Shift+Delete | `ESC [ 3 ; 2 ~` |

Previously, non-application mode sent `ESC [ P`, which shell prompts
expecting the advertised sequence could ignore. Existing `config.h` files
retain their old mappings; update them or regenerate from `config.def.h` and
rebuild.

The `phist-bs` and `phist-bs-256color` entries instead advertise DEL
(`\177`) for Delete and Backspace as `^H`. They match only a `config.h`
whose key mappings were changed to send those bytes; with the default
mappings, use `phist-256color`.

## Bracketed paste

When bracketed paste is enabled, pasted ESC bytes are removed before text is
wrapped with `ESC [ 200 ~` and `ESC [ 201 ~`. An embedded end marker
therefore cannot end the bracket early and turn the rest of a paste into
typed commands. Pastes also undergo [confirmation](pasting.md) independently
of this mode; the chosen text then follows the mode's delivery rules.

For a check that does not execute the pasted text, copy a crafted string,
then paste it into `cat -v` with Ctrl+Shift+V:

```sh
printf 'harmless\033[201~id\n' | xclip -selection clipboard
printf '\033[?2004h'; cat -v
```

Choose K and confirm Yes to keep the source bytes. The interior text appears
as `harmless[201~id`, with only phist's own ending `^[[201~` closing the paste.
Press Ctrl+C and run `printf '\033[?2004l'` afterwards. Run `LC_ALL=C.UTF-8
sh tests/paste.sh` for paste-path regressions.

## Short writes

A short write does not imply that child output is ready to read. Queued
output is retried after the tty becomes writable; output is read only when
`select()` reports readability. Reading immediately after a short write
could block forever with a silent child, and a second read using the same
readiness result could block too.

The reply suite forces a one-byte user-input write with a silent child and
checks that all input arrives without blocking: `LC_ALL=C.UTF-8 sh
tests/replies.sh`.

## Queued replies

Replies to DA, DECID, DSR/CPR and OSC color queries use a nonblocking queue
of up to 4,096 bytes. While replies are pending, the main loop also waits
for a writable tty, so a query followed by a read is answered promptly. If a
child keeps querying without reading and a reply does not fit, that reply is
dropped with a rate-limited diagnostic.

If phist cannot read the tty flags, enable nonblocking writes or restore
the original flags, it exits with a diagnostic rather than risk a
blocking write or leave incorrect flags in place.

Previously, a blocking write inside the parser could drain child output by
calling `ttyread()` recursively. It could reparse the outer call's static
buffer, recurse on further queries and mistake a zero-length read into a
full buffer for EOF. Query floods could garble output or close phist. The
parser no longer writes replies this way, and `ttyread()` refuses to read
into a full buffer.

Replies already pending when user input begins go first, including the rest
of a partially written reply. Replies generated while that input is being
written go afterwards. A key cannot split a reply, a reply cannot split a
paste, and continuous queries cannot indefinitely hold input back. User
input is also queued; see [queued user input](#queued-user-input).

The reply suite forces partial writes to check ordering. A bounded manual
stress case is `yes "$(printf '\033[6n')" | head -n 300000; echo survived`.
It should reach `survived` without closing phist. Replies that fit in the tty
and queue then reach the shell as input; Ctrl+C discards them.

## Queued user input

Keys and pastes enter a queue and are written without blocking as the child
reads them. The event loop watches tty writability whenever either input or
replies are pending, keeping drawing, keyboard handling and window-manager
interaction responsive even when a child reads slowly or not at all. Keys
typed while a paste is pending follow the paste.

Already queued replies precede new input; replies generated later follow it.
`ttywritepending()` and `ttyflushwrites()` manage both kinds of output,
replacing the reply-only helpers. Partial writes advance a read offset
rather than move the remainder on every write. The memory of a large paste
is released once it has been written. Writes never read output recursively,
so output cannot change bracketed-paste mode mid-write.

The previous synchronous paste path could keep phist unresponsive until a child
consumed the whole paste. With that delay removed, `pastemaxbytes` increases
from 4 MiB to 32 MiB. A limit still bounds memory: the transfer is buffered
whole, briefly with several copies, and an INCR owner could otherwise send
chunks without end. That limit applies to each incoming selection.

The queue as a whole stops growing at 64 MiB, so a child that stopped reading
cannot have phist buffer paste after paste. A paste that does not fit is
refused whole and explained in the paste failure box, since sending part of it
could run a command cut short. A key that does not fit is dropped and reported
on stderr at the rate of other terminal errors. An empty queue accepts one
input of any size, so a single large paste, which transformation or CR/LF
translation may make larger than the limit, is never refused for its size here.

Run `LC_ALL=C.UTF-8 sh tests/paste.sh` and `LC_ALL=C.UTF-8 sh
tests/replies.sh`. Paste checks queue 1 MiB to a non-reading child, require
an immediate return, then verify a later key follows the paste and that a
paste not fitting the queue sends nothing. Reply checks verify ordering with
queued input and the keys dropped from a full queue. Both suites also support
UndefinedBehaviorSanitizer through `CFLAGS`.
