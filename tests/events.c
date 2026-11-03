/* The main loop's waiting and event dispatch, without an X server. */
#include <assert.h>
#include <fcntl.h>
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
	assert(!xwait(xfds[0], fds[0], 0, -1, -1, 1, &readable, &writable));
	assert(!readable && !writable);

	/* Output for phist, and room for input only when it has some. */
	assert(write(fds[1], "x", 1) == 1);
	assert(!xwait(xfds[0], fds[0], 0, -1, -1, 1000, &readable, &writable));
	assert(readable && !writable);
	assert(!xwait(xfds[0], fds[0], 1, -1, -1, 1000, &readable, &writable));
	assert(readable && writable);
	assert(read(fds[0], &c, 1) == 1);
	assert(!xwait(xfds[0], fds[0], 1, -1, -1, 1000, &readable, &writable));
	assert(!readable && writable);

	/* The X connection ends the wait, and is not mistaken for the tty. */
	assert(write(xfds[1], "x", 1) == 1);
	assert(!xwait(xfds[0], fds[0], 0, -1, -1, -1, &readable, &writable));
	assert(!readable && !writable);
	assert(read(xfds[0], &c, 1) == 1);

	/* Waiting output ends the wait once its reader has room, without counting
	 * as the tty being ready; the X connection and tty still need no room. */
	{
		int out[2];
		char full[65536 * 2] = {0};
		ssize_t n;

		assert(pipe(out) == 0);
		assert(!xwait(xfds[0], fds[0], 0, out[1], -1, 1000, &readable, &writable));
		assert(!readable && !writable);
		assert(fcntl(out[1], F_SETFL, O_NONBLOCK) == 0);
		while ((n = write(out[1], full, sizeof(full))) > 0)
			;
		assert(fcntl(out[1], F_SETFL, 0) == 0);
		assert(!xwait(xfds[0], fds[0], 0, out[1], -1, 1, &readable, &writable));
		assert(!readable && !writable);
		close(out[0]);
		close(out[1]);
	}

	/* The shell exiting wakes the wait, without anything else being ready. */
	{
		int wake[2];

		assert(pipe(wake) == 0);
		assert(write(wake[1], "", 1) == 1);
		assert(!xwait(xfds[0], fds[0], 0, -1, wake[0], -1, &readable, &writable));
		assert(!readable && !writable);
		close(wake[0]);
		close(wake[1]);
	}

	/* A timeout under a millisecond waits rather than spins or truncates. */
	assert(!xwait(xfds[0], fds[0], 0, -1, -1, 0.25, &readable, &writable));

	/* A long timeout is not cut short, and a hang-up counts as ready. */
	close(fds[1]);
	assert(!xwait(xfds[0], fds[0], 0, -1, -1, 1e300, &readable, &writable));
	assert(readable && !writable);

	puts(high ? "PASS: waiting on descriptors past FD_SETSIZE"
	          : "PASS: waiting (descriptors past FD_SETSIZE not allowed here)");
}

static int failhints, sethints;

XSizeHints *
XAllocSizeHints(void)
{
	return failhints ? NULL : calloc(1, sizeof(XSizeHints));
}

void
XSetWMProperties(Display *d, Window w, XTextProperty *name, XTextProperty *icon,
                 char **argv, int argc, XSizeHints *size, XWMHints *wm, XClassHint *class)
{
	sethints++;
}

static void
testhints(void)
{
	xhints();
	assert(sethints == 1);
	failhints = 1;
	xhints();
	assert(sethints == 1);
	failhints = 0;
	puts("PASS: failing to allocate size hints skips them");
}

static int handled;

static void
counthandled(XEvent *e)
{
	handled++;
}

static void
testdispatch(void)
{
	XEvent e = {0};
	int types[] = { -1, LASTEvent, 64, 127, 128, 255, INT_MAX, INT_MIN };
	int i;

	handler[KeyPress] = counthandled;
	e.type = KeyPress;
	xdispatch(&e);
	assert(handled == 1);
	/* The last core type has an entry, even if empty. */
	handler[LASTEvent - 1] = counthandled;
	e.type = LASTEvent - 1;
	xdispatch(&e);
	assert(handled == 2);
	/* Types outside the table reach no handler, and no memory after it. */
	handler[LASTEvent - 1] = NULL;
	e.type = LASTEvent - 1;
	xdispatch(&e);
	for (i = 0; i < LEN(types); i++) {
		e.type = types[i];
		xdispatch(&e);
	}
	assert(handled == 2);
	/* An empty entry is skipped. */
	e.type = 0;
	xdispatch(&e);
	assert(handled == 2);
	puts("PASS: events outside the handler table are ignored");
}

static void
testorigin(void)
{
	assert(xwindoworigin(10, 0, 1920, 640) == 10);
	assert(xwindoworigin(-10, 1, 1920, 640) == 1268);
	assert(xwindoworigin(INT_MAX, 0, 1920, 640) == SHRT_MAX);
	assert(xwindoworigin(INT_MIN, 0, 1920, 640) == SHRT_MIN);
	assert(xwindoworigin(-INT_MAX, 1, 1920, 65535) == SHRT_MIN);
	assert(xwindoworigin(INT_MAX, 1, 65535, 1) == SHRT_MAX);
	puts("PASS: window positions from -g stay in range");
}

static void
testclose(void)
{
	XClientMessageEvent good = { .type = ClientMessage, .window = 5, .message_type = 6,
		.format = 32 }, e;

	xw.win = 5;
	xw.wmprotocols = 6;
	xw.wmdeletewin = 7;
	good.data.l[0] = 7;
	assert(xclosemessage(&good));
	e = good; e.window = 8;
	assert(!xclosemessage(&e));
	e = good; e.message_type = 9;
	assert(!xclosemessage(&e));
	e = good; e.format = 8;
	assert(!xclosemessage(&e));
	e = good; e.format = 16;
	assert(!xclosemessage(&e));
	e = good; e.data.l[0] = 9;
	assert(!xclosemessage(&e));
	puts("PASS: only WM_DELETE_WINDOW from WM_PROTOCOLS closes the window");

	/* XEMBED messages are also only for this window. */
	{
		XEvent x = { .xclient = { .type = ClientMessage, .window = 8,
			.message_type = 10, .format = 32 } };

		xw.xembed = 10;
		x.xclient.data.l[1] = XEMBED_FOCUS_OUT;
		win.mode |= MODE_FOCUSED;
		cmessage(&x);
		assert(win.mode & MODE_FOCUSED);
		x.xclient.window = 5;
		cmessage(&x);
		assert(!(win.mode & MODE_FOCUSED));
	}
}

int
main(void)
{
	testhints();
	testclose();
	testwait();
	testdispatch();
	testorigin();
	return 0;
}
