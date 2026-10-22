#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
testdir=$(mktemp -d)
trap 'rm -rf "$testdir"' 0
${CC:-cc} -std=c99 -D_XOPEN_SOURCE=600 ${CFLAGS:-} tests/selection.c -o "$testdir/selection" -lutil
"$testdir/selection"
${CC:-cc} -std=c99 -D_XOPEN_SOURCE=600 ${CFLAGS:-} \
	$(pkg-config --cflags fontconfig freetype2) \
	tests/selection-mouse.c st.c -o "$testdir/selection-mouse" \
	-lm -lrt -lX11 -lutil -lXft -lXrender $(pkg-config --libs fontconfig freetype2)
"$testdir/selection-mouse"
