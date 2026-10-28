# Pasting text

Review multiline text, tabs and control characters before sending them to a
program.

## What prompts for confirmation

PRIMARY and CLIPBOARD paste use the same path, including middle-click,
Shift+Insert and Ctrl+Shift+V. Empty pastes send nothing. Ordinary text
without newlines, tabs, controls or invalid UTF-8 is sent immediately and
unchanged. Other pastes are collected in full and shown in a dialog before
anything reaches the child.

The check works whether or not bracketed paste is enabled. Applications can
turn that mode off, and background output can do so while a shell is waiting
at its prompt; bracketed paste alone cannot guarantee that a multiline paste
will be reviewed before its lines are executed.

The dialog is blue for LF newlines only, yellow for tabs or CRLF, and red
for dangerous characters. Dangerous means other controls (including lone CR,
ESC, NUL, DEL and C1 controls) or invalid UTF-8 bytes. Classification uses
strict UTF-8 independently of the locale, including across incoming transfer
chunks. Zero-width and bidi characters are ordinary paste text, not a
separate danger class.

## Choosing what to send

| Choice | Treatment |
| --- | --- |
| S | Join into one line, replacing special characters with spaces, squeezing spaces and trimming both ends |
| P | Keep lines, normalize CRLF, expand tabs to four spaces, remove dangerous runs and trim trailing whitespace |
| T | Like P, retaining tabs; offered when tabs exist |
| K | Keep the original; offered when it differs from P and T, with an additional No/Yes confirmation |

Dangerous runs in P/T become one space unless next to a blank or at an end,
where they are dropped. P/T keep at most one final LF. Labels say which
transformations actually occur, so a choice may instead say `Keep LF` when
it already preserves the original.

Use the letters in either case, arrows, Tab, Home/End or Enter. Esc or
Ctrl+C cancels without sending text. Choice keys and Enter wait 500 ms
(`pasteenterdelay`) after the dialog opens; the Yes confirmation has the
same guard. Navigation, viewing and cancellation are immediate. V opens the
scrollable escaped full view; V, Q, Esc or Enter returns from it. Ctrl+C
cancels from the full view too.

The dialog is modal for keys and mouse clicks. Further paste requests are
ignored while a transfer or confirmation is active. The wheel scrolls
terminal history under the dialog, or the paste in its full view, without
running mouse bindings. A drag begun before the dialog still ends normally;
clicks cannot reclaim an old PRIMARY selection underneath.

LF becomes CR on delivery, as for ordinary terminal input. If bracketed
paste is enabled when sending begins, one pair of markers surrounds the
whole paste and ESC bytes inside are removed, including with K. K thus keeps
source bytes before the terminal's delivery rules are applied.

## Limits and failures

`pastemaxbytes` limits the buffered paste to 32 MiB. `pastetimeout` cancels
a transfer after three seconds without progress. A paste is also refused,
sending nothing, while the program has not read enough earlier input for it
to fit within a 64 MiB queue. `pasteenterdelay` sets the
500 ms accidental-choice guard. Add these definitions from `config.def.h`
when retaining an older `config.h`.

A malformed, oversized or stalled transfer sends nothing. A red `Paste
failed` box explains why for five seconds, and stderr records the reason.
After the guard delay, Esc, Ctrl+C or Enter dismisses it. A paste binding
can retry immediately, replacing the box. Empty or refused selections are
silent, not failures. Cancelled incremental transfers are drained before a
new request is sent, because some owners will not answer until the previous
transfer finishes. Draining ends after `pastetimeout` without progress or
four times that duration in total.

## Example and further details

At a Bash prompt, this background job disables bracketed paste after the
next prompt has appeared:

```sh
(sleep 2; printf '\033[?2004l'; sleep 60) &
```

Wait two seconds and paste two `echo` commands with a trailing newline. The
dialog appears before either line is sent. S joins them into one line, P
keeps the newlines (which can execute the commands), and Esc sends nothing.
The last sleep avoids a job-completion notification redisplaying the prompt
and reenabling bracketed paste during the check.

[The paste specification](paste-spec.md) defines exact character classes,
labels, key behavior, layout, transformations and X transfer handling. Run
`LC_ALL=C.UTF-8 sh tests/paste.sh` for regression coverage.

## Copying from the full view

In V's full view, drag Button1 to select text, including across rows and in
either direction. Escaped characters such as `\x1b` are indivisible units.
Any drag includes its starting character, even if it never leaves that
character; the final pointer position on release sets its end. The selection
stays highlighted while scrolling and becomes PRIMARY on release. A click
without a drag clears the highlight and leaves PRIMARY as it was. Dragging above
or below the text, even outside the window, scrolls a row per motion. Closing
the view during a drag does not claim PRIMARY.

C or the configured copy binding (normally Ctrl+Shift+C) copies the
selection, or the whole paste if nothing is selected, to CLIPBOARD. Ctrl+C
without Shift still cancels. The footer changes from `Copy all` to `Copy
selection` and then `Copied to clipboard` until the next key. Narrow windows
omit the label, then the key cap, when they cannot fit.

Copies use the escaped text shown: tabs, CR, ESC and backslashes stay
escaped, so a literal backslash followed by `n` cannot be mistaken for a
newline. Actual newlines are copied as newlines although the view shows
`\n`. The copied text contains no other control characters.

Copies have no separate size limit. Escaping may turn a 32 MiB paste into
128 MiB of text; phist sends selections too large for one X request
[incrementally](selection.md#sending-large-selections), for both PRIMARY and
CLIPBOARD.

The paste suite checks drags within escaped characters, rows and window
edges; release positions; scrolling, clicks and closing mid-drag; drawing
and narrow layouts; both copy keys with and without a selection; escaped
output and cancellation. `tests/selsend.sh` checks large outgoing transfers
and X request-size boundaries.
