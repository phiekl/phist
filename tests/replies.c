#include <sys/socket.h>
/* Route st.c's writes through testwrite() to force short writes. */
#define write testwrite
#define fcntl testfcntl
#include "terminal.h"
#undef write
#undef fcntl
ssize_t write(int, const void *, size_t); /* renamed with the rest above */
int fcntl(int, int, ...);

static int shortwrites; /* how many of the next writes write one byte */

ssize_t
testwrite(int fd, const void *buf, size_t n)
{
	if (shortwrites > 0 && n > 1) {
		shortwrites--;
		n = 1;
	}
	return write(fd, buf, n);
}

/* Fail this fcntl command, and count flags set and writes attempted. */
static int failfcntl, setflags, wantedsets;

/* A failure other than the one expected must not look like die()'s status. */
static void
exitstatus(void)
{
	if (setflags != wantedsets)
		_exit(8);
}

int
testfcntl(int fd, int cmd, ...)
{
	va_list ap;
	int arg;

	if (cmd == F_SETFL)
		setflags++;
	if (cmd == failfcntl && setflags >= (cmd == F_SETFL)) {
		errno = EIO;
		return -1;
	}
	if (cmd == F_GETFL)
		return fcntl(fd, cmd);
	va_start(ap, cmd);
	arg = va_arg(ap, int);
	va_end(ap);
	return fcntl(fd, cmd, arg);
}

/* Whether writing with a failing fcntl dies with neither bad flags nor a write. */
static void
expectfcntlfailure(int cmd, int wantsets)
{
	int fds[2], status, null;
	pid_t child;
	char c;

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	assert((child = fork()) >= 0);
	if (!child) {
		alarm(10); /* alarms are not inherited */
		null = open("/dev/null", O_WRONLY);
		dup2(null, 2);
		cmdfd = fds[0];
		failfcntl = cmd;
		setflags = 0;
		atexit(exitstatus);
		wantedsets = wantsets;
		ttyreply("x", 1);
		_exit(9);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
	assert(fcntl(fds[1], F_SETFL, O_NONBLOCK) == 0);
	assert(read(fds[1], &c, 1) < 0);
	close(fds[0]);
	close(fds[1]);
}

int
main(void)
{
	int fds[2];
	Arg step = { .i = 1 };
	char reply[64];
	int i;

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	cmdfd = fds[0];
	reset(20, 4);
	put("A\r\nB\r\nC\r\nD\r\nE");
	kscrollup(&step);
	selstart(0, 0, SNAP_WORD);
	put("\033[6n");
	assert(read(fds[1], reply, sizeof(reply)) > 0);
	assert(sb.view_offset == 1 && sel.ob.x == 0);
	ttywrite("\033[I", 3, 0);
	assert(read(fds[1], reply, sizeof(reply)) == 3);
	assert(sb.view_offset == 1 && sel.ob.x == 0);
	ttyinput("", 0, 1);
	assert(sb.view_offset == 1 && sel.ob.x == 0);
	ttyinput("x", 1, 1);
	assert(read(fds[1], reply, sizeof(reply)) == 1 && reply[0] == 'x');
	assert(sb.view_offset == 0 && sel.ob.x == 0 && sel.ob.y == -1);
	puts("PASS: automatic replies preserve history/selection; user input returns live, keeping the selection");

	/* A short write of user input neither blocks reading nor loses bytes. */
	alarm(10);
	shortwrites = 1;
	ttyinput("abc", 3, 1);
	assert(read(fds[1], reply, sizeof(reply)) == 3 && !memcmp(reply, "abc", 3));
	alarm(0);

	/* A partly written reply is completed before a keypress follows it. */
	put("\033[H");
	shortwrites = 1;
	put("\033[6n");
	assert(ttyreplypending());
	assert(read(fds[1], reply, sizeof(reply)) == 1 && reply[0] == '\033');
	ttyinput("x", 1, 1);
	assert(!ttyreplypending());
	assert(read(fds[1], reply, sizeof(reply)) == 6 && !memcmp(reply, "[1;1Rx", 6));
	/*
	 * Replies to queries read while an earlier reply is being completed
	 * wait until after the keypress instead of holding it back.
	 */
	put("\033[H");
	shortwrites = 1;
	put("\033[6n");
	assert(read(fds[1], reply, sizeof(reply)) == 1 && reply[0] == '\033');
	for (i = 0; i < 10; i++)
		assert(write(fds[1], "\033[6n", 4) == 4);
	alarm(10);
	shortwrites = 1; /* the rest of the reply is written in two parts */
	ttyinput("x", 1, 1);
	alarm(0);
	{
		char all[256];
		size_t got = 0;
		ssize_t r;

		ttyflushreplies();
		assert(fcntl(fds[1], F_SETFL, O_NONBLOCK) == 0);
		while ((r = read(fds[1], all + got, sizeof(all) - got)) > 0)
			got += r;
		assert(fcntl(fds[1], F_SETFL, 0) == 0);
		assert(got == 6 + 10 * 6 && !memcmp(all, "[1;1Rx", 6));
		for (i = 0; i < 10; i++)
			assert(!memcmp(all + 6 + 6 * i, "\033[1;1R", 6));
	}

	/* Replies produced while user input is being written wait for it. */
	replies.userwrite = 1;
	put("\033[6n");
	assert(ttyreplypending());
	replies.userwrite = 0;
	ttyflushreplies();
	assert(read(fds[1], reply, sizeof(reply)) == 6 && !memcmp(reply, "\033[1;1R", 6));
	puts("PASS: replies and user input reach the child in order, also after a short write, without blocking");

	/*
	 * A child that floods queries without reading the replies: they are
	 * queued up to REPLY_MAX without blocking, the rest are dropped, and
	 * parsing goes on. Blocking here used to re-enter ttyread().
	 */
	{
		int p[2], i;
		size_t total = 0;
		ssize_t n;
		char drain[65536];

		assert(pipe(p) == 0 && fcntl(p[0], F_SETFL, O_NONBLOCK) == 0);
		cmdfd = p[1];
		alarm(10); /* fail instead of hanging if a reply blocks */
		reset(20, 4);
		put("\033[H");
		for (i = 0; i < 30000; i++)
			put("\033[6n");
		assert(ttyreplypending());
		put("ok");
		assert(term.line[0][0].u == 'o' && term.line[0][1].u == 'k');
		do {
			while ((n = read(p[0], drain, sizeof(drain))) > 0) {
				assert(!memcmp(drain, "\033[1;1R", 6));
				total += n;
			}
			ttyflushreplies();
		} while (ttyreplypending());
		alarm(0);
		/* The pipe's capacity plus at most REPLY_MAX, in whole replies. */
		assert(total % 6 == 0 && total > REPLY_MAX && total < 30000 * 6);
		close(p[0]);
		close(p[1]);
		puts("PASS: a query flood without reading neither blocks nor re-enters the parser");
	}

	expectfcntlfailure(F_GETFL, 0);
	expectfcntlfailure(F_SETFL, 1);
	puts("PASS: a tty whose flags cannot be read or set is not written to");
	return 0;
}
