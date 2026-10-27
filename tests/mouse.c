#include <assert.h>
#include <locale.h>
#include <sys/socket.h>
#define VERSION "test"
#define main st_main
#include "../x.c"
#undef main

void testmouse_init(int, int, int);
void testmouse_put(const char *);
int testmouse_offset(void);
int testmouse_alldirty(void);

static int paste_requests, attribute_changes;
static const char *paste_text = "pasted text\nnext line";

/* Clipboard transport is mocked; terminal parsing and tty I/O are real. */
int XSetSelectionOwner(Display *d, Atom a, Window w, Time t) { return 0; }
Window XGetSelectionOwner(Display *d, Atom a) { return xw.win; }
int XFlush(Display *d) { return 0; }
int XFree(void *p) { free(p); return 0; }
int XDeleteProperty(Display *d, Window w, Atom a) { return 0; }
/* Count synchronous full redraws (linked with -Wl,--wrap=redraw). */
static int redraws;
void __real_redraw(void);
void __wrap_redraw(void) { redraws++; __real_redraw(); }

static int wmname_sets, wmiconname_sets;
static char wmname[64], wmiconname[64];

int
Xutf8TextListToTextProperty(Display *d, char **list, int count,
                            XICCEncodingStyle style, XTextProperty *prop)
{
	prop->value = (unsigned char *)xstrdup(list[0]);
	return Success;
}

void
XSetWMName(Display *d, Window w, XTextProperty *prop)
{
	wmname_sets++;
	snprintf(wmname, sizeof(wmname), "%s", (char *)prop->value);
}

void
XSetWMIconName(Display *d, Window w, XTextProperty *prop)
{
	wmiconname_sets++;
	snprintf(wmiconname, sizeof(wmiconname), "%s", (char *)prop->value);
}

void XSetTextProperty(Display *d, Window w, XTextProperty *prop, Atom a) {}

static int wmhints_reads;
XWMHints *XGetWMHints(Display *d, Window w) { wmhints_reads++; return NULL; }

Atom
XInternAtom(Display *d, const char *name, Bool only)
{
	return !strcmp(name, "CLIPBOARD") ? 42 : 43;
}

int
XConvertSelection(Display *d, Atom selection, Atom target, Atom property,
                  Window requestor, Time time)
{
	assert(selection == XA_PRIMARY);
	paste_requests++;
	return 0;
}

int
XChangeWindowAttributes(Display *d, Window w, unsigned long mask,
                        XSetWindowAttributes *attributes)
{
	attribute_changes++;
	return 0;
}

int
XGetWindowProperty(Display *d, Window w, Atom property, long offset, long length,
                   Bool delete, Atom requested, Atom *type, int *format,
                   unsigned long *items, unsigned long *remaining,
                   unsigned char **data)
{
	*type = XA_STRING;
	*format = 8;
	*items = strlen(paste_text);
	*remaining = 0;
	*data = (unsigned char *)xstrdup(paste_text);
	return Success;
}

static void
event(int type, int button, int col, int row, uint state)
{
	XEvent e = {0};

	e.xbutton.type = type;
	e.xbutton.button = button;
	e.xbutton.state = state;
	e.xbutton.x = col * win.cw + 1;
	e.xbutton.y = row * win.ch + 1;
	e.xbutton.time = 100;
	if (type == ButtonPress)
		bpress(&e);
	else if (type == ButtonRelease)
		brelease(&e);
	else
		bmotion(&e);
}

static void
click(int button, int col, int row)
{
	event(ButtonPress, button, col, row, 0);
	event(ButtonRelease, button, col, row, buttonmask(button));
}

static void
expect_input(int fd, const char *want)
{
	char buffer[256];
	ssize_t n = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);

	if (!want) {
		assert(n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
		return;
	}
	assert(n == (ssize_t)strlen(want) && !memcmp(buffer, want, n));
	expect_input(fd, NULL);
}

int
main(void)
{
	const int modes[] = {9, 1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1015, 1016};
	const char *url = "https://example.org/path";
	int sockets[2], i, set;
	char sequence[64];
	XEvent pasted = {0}, focusevent = {0};

	setlocale(LC_CTYPE, "");
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	borderpx = 0;
	win.cw = 8;
	win.ch = 16;
	win.tw = 80 * win.cw;
	win.th = 5 * win.ch;
	win.mode = MODE_FOCUSED; /* Drawing stays disabled without an X server. */
	xw.win = 1;
	testmouse_init(sockets[0], 80, 5);

	/* Applications cannot enable or reset any mouse reporting state. */
	for (set = 0; set <= 1; set++)
		for (i = 0; i < LEN(modes); i++) {
			snprintf(sequence, sizeof(sequence), "\033[?%d%c", modes[i], set ? 'h' : 'l');
			testmouse_put(sequence);
			assert(win.mode == MODE_FOCUSED);
		}
	testmouse_put("\033[?9;1000;1002;1003;1006;1016h");
	assert(win.mode == MODE_FOCUSED && !attribute_changes);
	/* Nor can they lock the keyboard (KAM), which would block typing reset. */
	testmouse_put("\033[2h\033[2;4h");
	assert(win.mode == MODE_FOCUSED);
	testmouse_put("\033[4l");
	/* Focus changes are not reported to the child, even when requested. */
	testmouse_put("\033[?1004h");
	focusevent.type = FocusOut;
	focus(&focusevent);
	focusevent.type = FocusIn;
	focus(&focusevent);
	assert(win.mode == MODE_FOCUSED);
	expect_input(sockets[1], NULL);

	/* Movement and all mouse buttons send no mouse input to the child. */
	testmouse_put("hello world");
	for (i = 1; i <= 11; i++) {
		event(MotionNotify, 0, i, 0, 0);
		click(i, 1, 0);
	}
	expect_input(sockets[1], NULL);

	/* Plain dragging copies to PRIMARY without needing a modifier. */
	event(ButtonPress, Button1, 0, 0, 0);
	event(MotionNotify, 0, 4, 0, Button1Mask);
	event(ButtonRelease, Button1, 4, 0, Button1Mask);
	assert(xsel.primary && !strcmp(xsel.primary, "hello"));
	expect_input(sockets[1], NULL);

	/* Multi-click word/phrase expansion still runs inside phist. */
	xsel.clicks = 0;
	click(Button1, 1, 0);
	click(Button1, 1, 0);
	assert(!strcmp(xsel.primary, "hello"));
	click(Button1, 1, 0);
	assert(!strcmp(xsel.primary, "hello world"));
	/* Past the largest match, the next click starts over without waiting. */
	click(Button1, 1, 0);
	assert(xsel.clicks == 1);
	click(Button1, 1, 0);
	assert(xsel.clicks == 2 && !strcmp(xsel.primary, "hello"));
	click(Button1, 1, 0);
	assert(!strcmp(xsel.primary, "hello world"));

	/* Wheel scrolling changes the history view, with no input sent to the child. */
	for (i = 0; i < 20; i++)
		testmouse_put("\r\nold line");
	assert(testmouse_offset() == 0);
	click(Button4, 1, 0);
	assert(testmouse_offset() == mousescrolllines);
	click(Button5, 1, 0);
	assert(testmouse_offset() == 0);
	expect_input(sockets[1], NULL);

	/* Clicking a URL still copies it to both selections and starts its flash. */
	testmouse_put("\033[H\033[2J");
	testmouse_put(url);
	click(Button1, 10, 0);
	assert(!strcmp(xsel.primary, url) && !strcmp(xsel.clipboard, url));
	assert(urlflashset && turlflashed(10, 0));
	expect_input(sockets[1], NULL);

	/* A middle click requests PRIMARY; only the actual pasted text is sent. */
	paste_requests = 0;
	click(Button2, 1, 0);
	assert(paste_requests == 1);
	expect_input(sockets[1], NULL);
	pasted.xselection.type = SelectionNotify;
	pasted.xselection.property = XA_PRIMARY;
	selnotify(&pasted);
	expect_input(sockets[1], "pasted text\rnext line");

	/* Alternate-screen apps cannot take over selection or wheel events either. */
	testmouse_put("\033[?1049h\033[?1003;1006h\033[Halt text");
	event(ButtonPress, Button1, 0, 0, 0);
	event(MotionNotify, 0, 2, 0, Button1Mask);
	event(ButtonRelease, Button1, 2, 0, Button1Mask);
	assert(!strcmp(xsel.primary, "alt"));
	/* There, the wheel scrolls phist's main history instead. */
	click(Button4, 1, 0);
	assert(testmouse_offset() == mousescrolllines);
	click(Button5, 1, 0);
	assert(testmouse_offset() == 0);
	expect_input(sockets[1], NULL);
	testmouse_put("\033[?2004h");
	click(Button2, 1, 0);
	selnotify(&pasted);
	expect_input(sockets[1], "\033[200~pasted text\rnext line\033[201~");
	/* Pasted ESC bytes cannot end the bracket early and inject input. */
	paste_text = "a\033[201~echo pwned\n\033\033";
	selnotify(&pasted);
	expect_input(sockets[1], "\033[200~a[201~echo pwned\r\033[201~");
	/* Without bracketed paste, the text is sent unchanged as before. */
	testmouse_put("\033[?2004l");
	selnotify(&pasted);
	expect_input(sockets[1], "a\033[201~echo pwned\r\033\033");

	/* A flood of BELs rings once per BELLGAP; later bells ring again. */
	win.mode = 0; /* unfocused: each ring sets the urgency hint */
	nanosleep(&(struct timespec){ .tv_nsec = (BELLGAP + 20) * 1000000L }, NULL);
	wmhints_reads = 0;
	for (i = 0; i < 1000; i++)
		testmouse_put("\a");
	assert(wmhints_reads == 1);
	nanosleep(&(struct timespec){ .tv_nsec = (BELLGAP + 20) * 1000000L }, NULL);
	testmouse_put("\a");
	assert(wmhints_reads == 2);
	/* A steady stream, like a held key, keeps ringing at the limited rate. */
	nanosleep(&(struct timespec){ .tv_nsec = (BELLGAP + 20) * 1000000L }, NULL);
	wmhints_reads = 0;
	for (i = 0; i < 16; i++) {
		testmouse_put("\a");
		nanosleep(&(struct timespec){ .tv_nsec = 30 * 1000000L }, NULL);
	}
	assert(BETWEEN(wmhints_reads, 3, 6));
	win.mode = MODE_FOCUSED;

	/* Reverse video toggles only mark the screen for the next frame. */
	redraws = 0;
	for (i = 0; i < 1000; i++)
		testmouse_put(i % 2 ? "\033[?5l" : "\033[?5h");
	testmouse_put("\033[?5h");
	assert(redraws == 0 && (win.mode & MODE_REVERSE));
	assert(testmouse_alldirty());
	testmouse_put("\033[?5l");
	assert(!(win.mode & MODE_REVERSE));

	/* A flood of title changes reaches X once, with the latest title. */
	opt_title = "phist";
	for (i = 0; i < 1000; i++) {
		snprintf(sequence, sizeof(sequence), "\033]0;title %d\a", i);
		testmouse_put(sequence);
	}
	assert(!wmname_sets && !wmiconname_sets);
	xflushtitles();
	assert(wmname_sets == 1 && !strcmp(wmname, "title 999"));
	assert(wmiconname_sets == 1 && !strcmp(wmiconname, "title 999"));
	xflushtitles();
	assert(wmname_sets == 1 && wmiconname_sets == 1);
	testmouse_put("\033]2;\a");
	xflushtitles();
	assert(wmname_sets == 2 && !strcmp(wmname, "phist") && wmiconname_sets == 1);

	close(sockets[0]);
	close(sockets[1]);
	puts("PASS: ignored mouse/focus modes and keyboard lock, no focus reports, no mouse input to child, selection, multi-clicks, wheel history, URL copy/flash, middle-click, ESC-free bracketed paste, bell rate limiting, coalesced reverse video and title coalescing");
	return 0;
}
