#!/bin/sh
# Usage: sh tests/history-bench.sh [source-tree] [plain|colored|varied]
# An optional older source tree lets the same workload measure both versions.
set -eu
cd "$(dirname "$0")/.."
sourcedir=$(cd "${1:-.}" && pwd)
testdir=$(mktemp -d)
trap 'rm -rf "$testdir"' 0
cp tests/history-bench.c "$testdir/history-bench.c"
${CC:-cc} -std=c99 -D_XOPEN_SOURCE=600 ${CFLAGS:--O2} -I"$sourcedir/tests" \
	"$testdir/history-bench.c" -o "$testdir/history-bench" -lutil
"$testdir/history-bench" "${2:-plain}"
