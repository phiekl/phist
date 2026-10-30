#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
testdir=$(mktemp -d)
trap 'rm -rf "$testdir"' 0
make config.h
${CC:-cc} -std=c99 -D_XOPEN_SOURCE=600 ${CFLAGS:-} \
	$(pkg-config --cflags fontconfig freetype2) \
	tests/events.c st.c -o "$testdir/events" \
	-lm -lrt -lX11 -lutil -lXft -lXrender $(pkg-config --libs fontconfig freetype2)
"$testdir/events"
