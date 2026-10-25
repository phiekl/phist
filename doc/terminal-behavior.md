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

## Media Copy

Application-controlled Media Copy sequences work only when phist was started
with `-o FILE`. Without it, `iofd` is phist's own stdout, potentially the
launching terminal or a session log; output from any application could
otherwise turn on raw copying with `CSI 5 i` or dump the screen/selection
with `CSI 0/1/2 i`. Local Print-key shortcuts are unaffected.

To check, run these inside a phist started without `-o`:

```sh
printf '\033[5i'; echo secret-output; printf '\033[4i'
printf '\033[0i'
```

The launching terminal receives neither raw output nor a screen dump. With
`phist -o FILE`, Media Copy still writes to `FILE`. See `phist(1)` and run
`LC_ALL=C.UTF-8 sh tests/printing.sh` for printing regressions.

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
