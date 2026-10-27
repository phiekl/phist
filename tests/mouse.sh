#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
testdir=$(mktemp -d)
trap 'rm -rf "$testdir"' 0
${CC:-cc} -std=c99 -D_XOPEN_SOURCE=600 ${CFLAGS:-} \
	$(pkg-config --cflags fontconfig freetype2) \
	tests/mouse.c tests/mouse-terminal.c -o "$testdir/mouse" -Wl,--wrap=redraw \
	-lm -lrt -lX11 -lutil -lXft -lXrender $(pkg-config --libs fontconfig freetype2)
"$testdir/mouse"
tic -x -o "$testdir/terminfo" st.info
for term in phist-mono phist phist-256color phist-meta phist-meta-256color phist-bs phist-bs-256color; do
	case $(infocmp -x -1 -A "$testdir/terminfo" "$term") in
		*kmous=*|*XM=*) echo "Unexpected mouse capability in $term" >&2; exit 1 ;;
	esac
done
echo "PASS: terminfo does not advertise mouse reporting"
