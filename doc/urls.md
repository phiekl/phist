# Clickable URLs

Recognize, underline and copy URLs from the screen or history.

## Copying a URL

HTTP and HTTPS URLs are underlined on the main screen, alternate screen and
in scrollback, including soft-wrapped continuations beyond the viewport. A
plain left click copies the target to both PRIMARY and CLIPBOARD. Dragging
selects text; Shift-click bypasses URL copying. Application-supplied
underline styles take precedence over the overlay.

`urlprefixes` sets recognized schemes and `urlchars` the accepted
characters. Prefixes must look like `scheme://`: an initial letter, then
letters, digits, `+`, `-` or `.`, followed by `://`. Invalid entries are
ignored with a diagnostic; overlapping valid schemes work.

## Matching and bounds

Scheme matching ignores case. Parentheses and brackets are balanced and
trailing prose punctuation is removed. Hard newlines, non-ASCII cells and
wide-character dummy cells stop a match. Each target is limited to 2,048
ASCII characters; scanning then continues for further URLs. The copied
string is exactly the bounded target that is underlined.

Matching uses visible rows plus up to 2,048 characters of wrapped context on
either side, rounded to complete rows. Text changes, scrolling, history
changes and geometry changes recompute the overlay; only rows whose URL
cells changed need redrawing. Stored glyph attributes are untouched, and no
cross-update cache can retain a stale target after an edit or history
eviction.

The cap bounds allocations, matching and row lookups by the viewport rather
than the full length of a wrapped line. One tradeoff remains: chains of
nested URLs that repeatedly hit the cap can be segmented differently when
the viewport moves.

## Checks

```sh
LC_ALL=C.UTF-8 sh tests/urls.sh
LC_ALL=C.UTF-8 sh tests/rendering.sh
```

The URL suite compares every visible cell against a fresh update and an
independent whole-line parser, except for the capped-chain ambiguity above.
It covers edits, eviction, wrapping, colors, alternate screens, resizes,
random parser input and long-line scrolling, with 32 seeds over 18 history
limits and seven sizes. A 4 MB wrapped-line case checks allocation and
row-lookup bounds through the test-only `ROWVISIT()` hook in `viewline()`
and `sb_get()`. Rendering checks cover foreground-colored URL underlines.
