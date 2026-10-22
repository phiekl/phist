#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
testdir=$(mktemp -d)
trap 'rm -rf "$testdir"' 0
make config.h

# Each curly underline, chosen in config.h, with x.c next to the copy.
src="$testdir/src"
mkdir -p "$src/tests"
cp ./*.c ./*.h "$src/"
cp tests/rendering.c "$src/tests/"
for style in UNDERCURL_CURLY UNDERCURL_SPIKY UNDERCURL_CAPPED; do
	sed "s/^#define UNDERCURL_STYLE .*/#define UNDERCURL_STYLE $style/" config.h >"$src/config.h"
	grep -q "^#define UNDERCURL_STYLE $style\$" "$src/config.h"
	(
		cd "$src"
		${CC:-cc} -std=c99 -D_XOPEN_SOURCE=600 ${CFLAGS:-} \
			$(pkg-config --cflags fontconfig freetype2) \
			tests/rendering.c st.c -o "$testdir/rendering" \
			-lm -lrt -lX11 -lutil -lXft -lXrender $(pkg-config --libs fontconfig freetype2)
	)
	"$testdir/rendering"
done
