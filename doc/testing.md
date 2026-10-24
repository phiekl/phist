# Testing and fuzzing

Run regression suites and stress the terminal core with reproducible input.

## Regression suites

Run scripts from the repository root with a UTF-8 locale, for example:

```sh
LC_ALL=C.UTF-8 sh tests/history.sh
LC_ALL=C.UTF-8 sh tests/rendering.sh
```

Topic guides name the relevant scripts and what they check. Scripts build in
temporary directories, clean up on exit and accept `CC` and `CFLAGS`.
Terminal-core tests use the real parser through `tests/terminal.h` without
an X window. Renderer and mouse suites link the X libraries and use mocked
drawing or events. Keep `config.h` current when new configuration
definitions are added; scripts that need it run `make config.h`, which does
not replace an existing file.

`tests/history-bench.sh` is a measurement tool with optional arguments; see
[compressed rows](scrollback.md#compressed-rows) for its workloads.

## Fuzzing

`tests/fuzz.sh` drives the real parser, resizing, history, selections and
URL queries with seeded random input. It includes malformed escapes and
UTF-8, extreme CSI parameters, OSC payloads and one-cell dimensions.
Incomplete UTF-8 is kept across writes as in `ttyread()`, and payloads stay
within their buffers. Bounds on terminal state, saved cursors and history
are checked after every operation even with `NDEBUG`. Episode boundaries
scan all live and history rows for wide/dummy pairing issues.

```sh
LC_ALL=C.UTF-8 sh tests/fuzz.sh
FUZZ_SEEDS='1 2' FUZZ_EPISODES=10 FUZZ_OPS=500 FUZZ_TIMEOUT=60 \
    LC_ALL=C.UTF-8 sh tests/fuzz.sh
```

Defaults run seeds 1 through 8, six episodes of 300 operations each, with a
120-second budget per seed. `FUZZ_TIMEOUT` must be a positive integer. Each
seed's result is reported independently. An internal SIGALRM handler prints
diagnostics before an optional external `timeout` backstop. The handler
avoids stdio locks and exits through `_exit()`; the unwinder is loaded
before it is installed, so the handler does not have to load it.

For memory and undefined-behavior checking:

```sh
CC=gcc CFLAGS='-g -O1 -fsanitize=address,undefined -fsanitize-recover=address,signed-integer-overflow' \
    ASAN_OPTIONS=halt_on_error=0 LC_ALL=C.UTF-8 sh tests/fuzz.sh
```

Sanitizer findings and abnormal exits fail the run; sanitizer summaries are
preserved and failure filtering uses portable extended regexes. Wide/dummy
mismatches are warnings, allowing known pairing problems to be investigated
separately from bounds violations.
