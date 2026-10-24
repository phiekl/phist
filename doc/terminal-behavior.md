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
