#include <assert.h>
#include <fcntl.h>
#include <sys/socket.h>
#define VERSION "test"
#define main st_main
#include "../x.c"
#undef main

void testblink_init(void);
int testblink_dirty(void);
void testblink_output(void);
void testblink_fd(int);
void testblink_write(const char *);
int testblink_offset(void);

/* Just enough of a display for xresize(). */
Pixmap XCreatePixmap(Display *d, Drawable w, unsigned int x, unsigned int y, unsigned int depth) { return 1; }
int XFreePixmap(Display *d, Pixmap p) { return 0; }
void XftDrawChange(XftDraw *d, Drawable dr) {}
void XftDrawRect(XftDraw *d, const XftColor *c, int x, int y, unsigned int w, unsigned int h) {}

static KeySym lookupkey = XK_Shift_L;
static int lookuptext;

int
XLookupString(XKeyEvent *event, char *buffer, int size, KeySym *key, XComposeStatus *status)
{
	*key = lookupkey;
	buffer[0] = 'a';
	return lookuptext;
}

int
main(void)
{
	struct timespec now = { .tv_sec = 1 };
	int i, hidden = 0, visible = 0;
	int fds[2];
	char input;
	XEvent event = { .xkey = { .type = KeyPress } };

	testblink_init();
	testblink_dirty();
	blinktimeout = 500;
	cursorblinks = 1;
	blinkresetat(now);
	assert(blinkremaining(now) == 500);
	for (i = 1; i <= 20; i++) {
		now.tv_sec = 1 + i / 10;
		now.tv_nsec = (i % 10) * 100000000L;
		testblink_output();
		blinkremaining(now);
		if (i % 5 == 0) {
			assert(testblink_dirty());
			if (IS_SET(MODE_BLINK)) hidden++;
			else visible++;
		}
	}
	assert(hidden == 2 && visible == 2);
	now.tv_nsec = 500000000;
	blinkremaining(now);
	assert(IS_SET(MODE_BLINK));
	testblink_dirty();
	blinkresetat(now);
	assert(!IS_SET(MODE_BLINK) && testblink_dirty());
	assert(blinkremaining(now) == 500);
	win.mode |= MODE_BLINK;
	for (i = 0; i < 20; i++) {
		lookupkey = i % 2 ? XK_Shift_L : XK_Control_L;
		kpress(&event);
		assert(IS_SET(MODE_BLINK));
		assert(lastblink.tv_sec == now.tv_sec && lastblink.tv_nsec == now.tv_nsec);
	}
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	testblink_fd(fds[0]);
	lookupkey = XK_a;
	lookuptext = 1;
	kpress(&event);
	assert(!IS_SET(MODE_BLINK));
	assert(read(fds[1], &input, 1) == 1 && input == 'a');
	close(fds[0]);
	close(fds[1]);
	blinktimeout = 0;
	assert(blinkremaining(now) == -1);
	/* The cursor alone only wakes phist while focused and visible. */
	blinktimeout = 500;
	testblink_write("\033[H\033[2J");
	cursorblinks = 1;
	win.mode = MODE_FOCUSED;
	blinkresetat(now);
	assert(blinkremaining(now) == 500);
	win.mode = MODE_FOCUSED | MODE_HIDE;
	assert(blinkremaining(now) == -1);
	win.mode = 0;
	assert(blinkremaining(now) == -1);
	testblink_write("\033[5mx\033[0m");
	assert(blinkremaining(now) == 500);
	blinktimeout = 0;

	/* Shift+PageUp scrolls history, even beneath alternate-screen programs. */
	{
		char buf[16];

		assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
		assert(fcntl(fds[1], F_SETFL, O_NONBLOCK) == 0);
		testblink_fd(fds[0]);
		lookupkey = XK_Prior;
		lookuptext = 0;
		event.xkey.state = ShiftMask;
		testblink_write("\033[?1049h");
		assert(testblink_offset() == 0);
		kpress(&event);
		assert(testblink_offset() > 0);
		assert(read(fds[1], buf, sizeof(buf)) < 0);
		lookupkey = XK_Next;
		kpress(&event);
		assert(testblink_offset() == 0);
		assert(read(fds[1], buf, sizeof(buf)) < 0);
		testblink_write("\033[?1049l");
		event.xkey.state = 0;
		close(fds[0]);
		close(fds[1]);
	}

	/* A pixel-only resize still redraws every row into the new buffer. */
	{
		Screen screen = { .root_depth = 24 };
		_XPrivDisplay display = calloc(1, sizeof(*display));
		static Color colors[260];
		int err = dup(2), null = open("/dev/null", O_WRONLY);

		assert(display);
		display->screens = &screen;
		xw.dpy = (Display *)display;
		dc.col = colors;
		win.cw = 10;
		win.ch = 20;
		cresize(20 * win.cw, 4 * win.ch);
		testblink_dirty();
		dup2(null, 2); /* no tty to resize */
		cresize(20 * win.cw + 7, 4 * win.ch + 3);
		dup2(err, 2);
		close(err);
		close(null);
		assert(testblink_dirty());
		xw.dpy = NULL;
		free(display);
	}

	/* DECSCUSR 0 restores the configured style. */
	cursorstyle = 2;
	assert(xsetcursor(5) == 0 && win.cursor == 5 && cursorblinks);
	assert(xsetcursor(0) == 0 && win.cursor == 2 && !cursorblinks);
	cursorstyle = 0;
	assert(xsetcursor(0) == 0 && win.cursor == 0 && cursorblinks);
	assert(xsetcursor(9) == 1 && win.cursor == 0);
	puts("PASS: output-independent blinking, visible input reset, disabled timing, unfocused cursors, alternate-screen history keys, pixel-only resizes and default style");
	return 0;
}
