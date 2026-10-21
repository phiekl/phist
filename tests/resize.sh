#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
testdir=$(mktemp -d)
trap 'rm -rf "$testdir"' 0
${CC:-cc} -std=c99 -D_XOPEN_SOURCE=600 ${CFLAGS:-} tests/resize.c -o "$testdir/resize" -lutil
for capacity in 0 1 4 64 65536; do
	"$testdir/resize" "$capacity"
done
