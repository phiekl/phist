# Terminal behavior and limits

How phist handles control sequences and bounds work triggered by application
output.

## Parser bounds

Charset designators and escape final characters retain their full `Rune`
values through `eschandle()`, `tdeftran()`, `tdefutf8()` and `tdectest()`.
NUL and non-ASCII charset designators are rejected before `strchr()`;
non-ASCII CSI input becomes an invalid byte. Unicode input must never
truncate into an ASCII command, charset selector or parameter. Diagnostic
character formatting is bounded too.

Negative CSI parameters become zero; large positive values saturate at
65,535 before narrowing to `int`. This prevents signed overflow in cursor
arithmetic while still moving to the screen edge for very large requests.

Overwriting a wide character tests `ATTR_WIDE` as a bit, including on styled
cells. A new wide glyph one column before an existing styled wide glyph
clears that displaced glyph's trailing dummy instead of leaving an orphan.

These fixes address fuzz-discovered charset reads past the table, wide cell
overwrites and signed parameter overflow. Run `LC_ALL=C.UTF-8 sh
tests/sequences.sh` for escape/charset truncation, parameter bounds and
wide-cell regressions, then [fuzzing](testing.md#fuzzing) for broader
coverage.

## Event loop

The main loop waits with `poll()`, which takes descriptors of any number, so
a program that starts phist with over 1,024 descriptors open cannot push the
tty or X connection out of a fixed-size `fd_set`. Timeouts round up to whole
milliseconds. A tty that hung up counts as ready, so its read reports it. X
events of types beyond the core ones, which extensions deliver, are ignored.
Run `LC_ALL=C.UTF-8 sh tests/events.sh`.

Only a `WM_PROTOCOLS` client message for phist's window, in format 32 and naming
`WM_DELETE_WINDOW`, closes the window. This keeps other messages from being
misread; it does not authenticate other X clients.

XEMBED focus messages must also name phist's window before they can
change its focus state.

## Exit status

phist exits with 0 if its shell exits with 0, and with 1 for any other shell
status or signal termination. Before exiting, it drains remaining pty
output and flushes the `-o` queue. A descendant still writing to the pty gets
one second; flushing queued output gets another. A three-second alarm ends
phist if the main loop is stuck, for example waiting for the X server.

A shell that closes the pty but keeps running causes phist to exit after
one second with a diagnostic and status 1. Run `LC_ALL=C.UTF-8 sh
tests/shellexit.sh` to check these cases.

The shell's exit handler is installed before forking. `SIGCHLD` remains
blocked until the shell PID is recorded, then is unblocked even if the
launcher blocked it. The shell also starts with `SIGCHLD` unblocked.

## Media Copy

Application-controlled Media Copy sequences work only when phist was started
with `-o FILE`. Without it, `iofd` is phist's own stdout, potentially the
launching terminal or a session log; output from any application could
otherwise turn on raw copying with `CSI 5 i` or dump the screen/selection
with `CSI 0/1/2 i`. Print Screen keyboard shortcuts are disabled.

To check, run these inside a phist started without `-o`:

```sh
printf '\033[5i'; echo secret-output; printf '\033[4i'
printf '\033[0i'
```

The launching terminal receives neither raw output nor a screen dump. With
`phist -o FILE`, Media Copy still writes to `FILE`. See `phist(1)` and run
`LC_ALL=C.UTF-8 sh tests/printing.sh` for printing regressions.

If `FILE` is a pipe whose reader goes away, printing stops with an error on
stderr; the terminal and its shell carry on. phist ignores `SIGPIPE`, also
without `-o` for the sake of stderr, but programs in the terminal still get
the default.

A regular file takes output directly. Other output, including a terminal or
socket, goes through a queue in the main loop to a small writer process over
a nonblocking pipe. Only that process waits for the reader, so neither a
stalled reader nor another writer filling the same pipe stops the terminal.
`O_NONBLOCK` on the output itself would be shared with the launching shell
for `-o -`.

If 1 MiB is waiting in the queue, printing ends with a message on stderr, so
the record stops rather than having gaps; a single dump larger than 1 MiB
ends it straight away. Flushing after the shell exits gets up to a second,
as above; output still in the queue after that is lost, as is any when
phist's window is closed. What the writer already has still reaches the
reader, so the writer stays until the reader takes it or goes away.

A FIFO without a reader doesn't hold up startup either. The writer waits for a
reader, while output collects in the queue, and gives up when phist exits. A
reader that comes later gets the output from the start.

The writer closes everything it inherits, so it can't keep the X connection open
after phist exits. That takes `/proc/self/fd`. Without it, as in a chroot with
no `/proc`, printing to anything but a regular file stops with a message on
stderr.

## Keyboard locking

KAM (`CSI 2 h` / `CSI 2 l`) is ignored. An application or printed file
cannot disable all keyboard input, including local shortcuts, and leave the
user unable to type `reset`.

For a bounded manual check, type during the pause:

```sh
printf '\033[2h'; sleep 5; printf '\033[2l'
```

Keystrokes are delivered and appear when the prompt returns. Previously the
lock discarded them. The key path and removed mode are covered by
`LC_ALL=C.UTF-8 sh tests/mouse.sh`.

## String sequences

DCS, OSC, PM and APC payloads are limited to 1 MiB (`STR_MAX_SIZ`). A longer
sequence is consumed up to its terminator, but excess bytes are dropped and
the entire sequence is ignored. This prevents an unterminated string from
growing its buffer until memory is exhausted. The limit still allows
substantial OSC 52 clipboard data when `allowwindowops` is enabled.

A rejected sequence reports, for example, `erresc: ESCP string longer than
1048576 bytes ignored`. A long open DCS should keep string-buffer memory
near the limit rather than near the payload size. If measuring RSS manually,
send the whole open string and its terminator inside one `sh -c`: an
interactive shell's title-setting hook can emit an ESC between commands,
terminating the string early and sending the remaining payload into
screen/history storage instead.

OSC opcodes and palette indexes must be whole decimal numbers that fit an `int`,
so `OSC 4294967298` is not `OSC 2`. Any other field is an unknown opcode or an
invalid index. An empty opcode still means 0.

OSC 52 decodes base64 padding the same way whether plain `char` is signed or
not, so the sequence suite also runs with `-funsigned-char`.

Run `LC_ALL=C.UTF-8 sh tests/sequences.sh` for overflow and recovery checks.

## Local echo

SRM (`CSI 12 l` / `CSI 12 h`) no longer lets applications switch local echo
on or off. Echo belongs to the pty/application: drawing keystrokes again in
phist could misrepresent input or reveal typing even with tty echo disabled.

For a manual check, disable tty echo, request local echo and type:

```sh
stty -echo; printf '\033[12l'; cat >/dev/null
```

Typing stays invisible. Press Ctrl+C and run `stty echo` to restore the tty.
`LC_ALL=C.UTF-8 sh tests/sequences.sh` checks the ignored mode.

## Focus reporting

Focus mode 1004 is ignored. Gaining or losing focus no longer sends `ESC [
I` / `ESC [ O` to the child, even if an application enabled the mode before
exiting. This keeps output or an abandoned mode from leaving those bytes at
the shell prompt. Vim `FocusGained`/`FocusLost` autocommands and tmux
`focus-events` consequently do not fire.

To inspect, run `printf '\033[?1004h'; cat -v`, then switch windows and
back. No focus bytes arrive; press Ctrl+C to finish. Run `LC_ALL=C.UTF-8 sh
tests/mouse.sh` for mode and focus-event checks.

## Palette changes

`allowcolorchanges = 0` is the default. Applications cannot redefine palette
entries or default foreground, background and cursor colors with OSC
4/10/11/12 unless this option is enabled. Ignored requests produce an
`erresc` diagnostic. This prevents persistent color changes from altering
the terminal's appearance after an application exits; minimum contrast is
handled separately by the
[renderer](appearance.md#concealed-and-low-contrast-text).

Queries (`OSC 4;n;?` and `OSC 10/11/12;?`) and resets (`OSC
104/110/111/112`) remain allowed. Tools that set color schemes need
`allowcolorchanges = 1` in `config.h`. Add the new definition from
`config.def.h` when retaining an older configuration.

For example, `printf '\033]11;#800000\a'` leaves the background unchanged
with the default setting and reports `erresc: OSC 11 color change ignored
(allowcolorchanges = 0)` on stderr. Queries still return the configured
color; `printf '\033]111\a'` resets the background. Run `LC_ALL=C.UTF-8 sh
tests/sequences.sh`.

## Diagnostics

Input-triggered escape diagnostics go through `erresc()`: a burst of 20
messages is printed, followed by at most five per second. The next printed
message reports dropped messages as `erresc: N messages suppressed`.
Occasional diagnostics remain visible; startup and resize errors are
unaffected.

CSI and string dumps follow the diagnostic they belong to and stop after 128
bytes, appending the original length as `...(N bytes)`. An unknown OSC can
no longer add up to 1 MiB to stderr in one dump, and flooded unknown CSI
sequences cannot fill session logs at input speed.

To inspect, start phist with stderr redirected to a chosen file, emit many
unknown `CSI 999 x` sequences inside it, and inspect that file from the
launching shell. A two-second flood should produce about 30 diagnostics (the
initial 20 plus five per second), rather than hundreds of thousands. Long
unknown OSC payloads should show the truncation suffix. Exact counts depend
on timing and other diagnostics. Run `LC_ALL=C.UTF-8 sh tests/sequences.sh`
for rate, suppression and dump limits.

## Vim compatibility probe

The exact `ESC [ 0 % m` probe Vim sends at startup is a silent no-op. It
leaves attributes and cursor position unchanged. Similar unknown sequences
with another parameter, final byte or private prefix still produce normal
diagnostics.

Run `printf '\033[0%%m'` to emit it directly; `%%` produces the literal
percent sign for `printf`. Stderr stays quiet. The sequence suite checks
both the exact probe and nearby sequences that must still be diagnosed.
