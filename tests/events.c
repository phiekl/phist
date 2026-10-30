/* The main loop's waiting and event dispatch, without an X server. */
#include <assert.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#define VERSION "test"
#define main st_main
#include "../x.c"
#undef main

/* Numbers past any fd_set, when the limit on open files allows them. */
static int
highfds(int fds[2])
{
	struct rlimit limit;
	int pair[2], want = FD_SETSIZE + 8;

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
	assert(getrlimit(RLIMIT_NOFILE, &limit) == 0);
	if (limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur <= (rlim_t)want + 2) {
		if (limit.rlim_max != RLIM_INFINITY && limit.rlim_max <= (rlim_t)want + 2)
			goto low;
		limit.rlim_cur = (rlim_t)want + 3;
		if (setrlimit(RLIMIT_NOFILE, &limit))
			goto low;
	}
	assert(dup2(pair[0], want) == want && dup2(pair[1], want + 1) == want + 1);
	close(pair[0]);
	close(pair[1]);
	fds[0] = want;
	fds[1] = want + 1;
	return 1;
low:
	fds[0] = pair[0];
	fds[1] = pair[1];
	return 0;
}

static void
testwait(void)
{
	int fds[2], xfds[2], readable, writable, high;
	char c;

	high = highfds(fds);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, xfds) == 0);

	/* Nothing to report: only the timeout ends the wait. */
	assert(!xwait(xfds[0], fds[0], 0, 1, &readable, &writable));
	assert(!readable && !writable);

	/* Output for phist, and room for input only when it has some. */
	assert(write(fds[1], "x", 1) == 1);
	assert(!xwait(xfds[0], fds[0], 0, 1000, &readable, &writable));
	assert(readable && !writable);
	assert(!xwait(xfds[0], fds[0], 1, 1000, &readable, &writable));
	assert(readable && writable);
	assert(read(fds[0], &c, 1) == 1);
	assert(!xwait(xfds[0], fds[0], 1, 1000, &readable, &writable));
	assert(!readable && writable);

	/* The X connection ends the wait, and is not mistaken for the tty. */
	assert(write(xfds[1], "x", 1) == 1);
	assert(!xwait(xfds[0], fds[0], 0, -1, &readable, &writable));
	assert(!readable && !writable);
	assert(read(xfds[0], &c, 1) == 1);

	/* A timeout under a millisecond waits rather than spins or truncates. */
	assert(!xwait(xfds[0], fds[0], 0, 0.25, &readable, &writable));

	/* A long timeout is not cut short, and a hang-up counts as ready. */
	close(fds[1]);
	assert(!xwait(xfds[0], fds[0], 0, 1e300, &readable, &writable));
	assert(readable && !writable);

	puts(high ? "PASS: waiting on descriptors past FD_SETSIZE"
	          : "PASS: waiting (descriptors past FD_SETSIZE not allowed here)");
}

int
main(void)
{
	testwait();
	return 0;
}
