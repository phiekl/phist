#!/bin/sh
# Fuzz the terminal core under several deterministic seeds.
# Override with FUZZ_SEEDS, FUZZ_EPISODES, FUZZ_OPS and FUZZ_TIMEOUT.
# FUZZ_TIMEOUT is a positive integer time budget in seconds for each seed.
# Assertions stay enabled even when CFLAGS contains -DNDEBUG.
# For memory checking run with sanitizers, e.g.
#	CC=gcc CFLAGS='-g -O1 -fsanitize=address,undefined \
#	  -fsanitize-recover=address,signed-integer-overflow' \
#	ASAN_OPTIONS=halt_on_error=0 sh tests/fuzz.sh
set -eu
cd "$(dirname "$0")/.."
testdir=$(mktemp -d)
trap 'rm -rf "$testdir"' 0
seconds=${FUZZ_TIMEOUT:-120}
case "$seconds" in
	''|0*|*[!0-9]*)
		echo 'FUZZ_TIMEOUT must be a positive integer in seconds' >&2
		exit 2
		;;
esac
${CC:-cc} -std=c99 -D_XOPEN_SOURCE=600 ${CFLAGS:-} -rdynamic \
	tests/fuzz.c -o "$testdir/fuzz" -lutil

rc=0
for seed in ${FUZZ_SEEDS:-1 2 3 4 5 6 7 8}; do
	log="$testdir/seed-$seed.log"
	status=0
	if command -v timeout >/dev/null 2>&1; then
		# Let SIGALRM print diagnostics before the external backstop fires.
		timeout -k 5 "$((seconds + 5))" "$testdir/fuzz" "$seed" \
			"${FUZZ_EPISODES:-6}" "${FUZZ_OPS:-300}" "$seconds" \
			>"$log" 2>&1 || status=$?
	else
		"$testdir/fuzz" "$seed" \
			"${FUZZ_EPISODES:-6}" "${FUZZ_OPS:-300}" "$seconds" \
			>"$log" 2>&1 || status=$?
	fi
	if grep -Eq "ERROR: AddressSanitizer|runtime error:" "$log"; then
		echo "FAIL: sanitizer findings in fuzz seed $seed"
		grep -E "ERROR: AddressSanitizer|SUMMARY: AddressSanitizer|runtime error:" "$log" | sort -u
		rc=1
	elif [ "$status" -ne 0 ]; then
		echo "FAIL: fuzz seed $seed exited abnormally (status $status)"
		grep -Ev "^(erresc|esc unhandled)" "$log" | tail -20
		rc=1
	fi
done
if [ "$rc" -eq 0 ]; then
	echo "PASS: fuzz seeds ${FUZZ_SEEDS:-1 2 3 4 5 6 7 8}"
fi
exit "$rc"
