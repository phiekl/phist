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
