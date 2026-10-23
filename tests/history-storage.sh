#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
testdir=$(mktemp -d)
trap 'rm -rf "$testdir"' 0
${CC:-cc} -std=c99 -D_XOPEN_SOURCE=600 ${CFLAGS:-} tests/history-storage.c -o "$testdir/history-storage" -lutil
"$testdir/history-storage"
