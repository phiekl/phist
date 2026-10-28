#include <assert.h>
#include <fcntl.h>
#include <locale.h>
#include <sys/socket.h>
#define VERSION "test"
#define main st_main
#include "../x.c"
#undef main
#include "paste-time.h"

void testmouse_init(int, int, int);
void testmouse_put(const char *);
int testmouse_alldirty(void);
void testmouse_cleardirty(void);
int testmouse_offset(void);

/* Mock the X selection protocol, retaining real parsing and tty writes. */
typedef struct {
	Atom type;
	int format;
	const void *data;
	ulong items, remaining;
	long offset;
	int error;
} Property;

static Property properties[4];
static int propertycount, propertyindex, requests, deletes, attributes;
static KeySym lookupkey;
static Atom requested, requestedproperty;
static Time requestedtime;
static Window requestedwindow;
static Rune painted[24][128];
static unsigned long ink[24][128];
/* Opaque rectangles and translucent blends, in drawing order. */
typedef struct { int x, y, w, h, order; unsigned long pixel; } Rect;
typedef struct { int x, y, w, h, order; unsigned short red, green, blue, alpha; } Blend;
static Rect rects[16];
static Blend blends[96];
static int nrects, nblends, drawn;
static int glyphs, copies;
static unsigned long framepixels[4];
static int presenting, bellfills;
static int failpixmap, faildraw, failpicture, drawchanges;
static Drawable maindrawable = 2;

static Drawable
drawtarget(Draw d)
{
	return d == (Draw)2 ? maindrawable : (uintptr_t)d;
}

void
XftDrawChange(Draw d, Drawable drawable)
{
	assert(d == (Draw)2 && (drawable == xw.win || drawable == xw.buf));
	maindrawable = drawable;
	drawchanges++;
}

Pixmap
XCreatePixmap(Display *d, Drawable parent, uint w, uint h, uint depth)
{
	assert(w == win.w && h == win.h && depth == 24);
	return failpixmap ? None : 3;
}

XftDraw *XftDrawCreate(Display *d, Drawable dr, Visual *v, Colormap c)
{
	assert(dr == 3);
	return faildraw ? NULL : (Draw)3;
}
void XftDrawDestroy(Draw d) { assert(d == (Draw)3); }
int XFreePixmap(Display *d, Pixmap p) { assert(p == 3); return 0; }

int XFree(void *p) { free(p); return 0; }
/* The server's request size in 4-byte units, with BIG-REQUESTS or without. */
static long bigrequests = 4194303, requestsize = 65535;
long XExtendedMaxRequestSize(Display *d) { return bigrequests; }
long XMaxRequestSize(Display *d) { return requestsize; }
int XFlush(Display *d) { return 0; }
int XDeleteProperty(Display *d, Window w, Atom a) { deletes++; return 0; }

Atom
XInternAtom(Display *d, const char *name, Bool only)
{
	if (!strcmp(name, "_ST_PASTE_TIME"))
		return 45;
	if (!strncmp(name, "_ST_PASTE", 9))
		return 100 + atoi(name + 9);
	return !strcmp(name, "INCR") ? 43 : !strcmp(name, "CLIPBOARD") ? 42 : 44;
}

int
XConvertSelection(Display *d, Atom selection, Atom target, Atom property,
                  Window requestor, Time time)
{
	assert(property == 100 && requestor >= 1000);
	requestedwindow = requestor;
	assert(time != CurrentTime);
	requestedtime = time;
	requested = selection;
	requestedproperty = property;
	requests++;
	return 0;
}

int
XChangeWindowAttributes(Display *d, Window w, unsigned long mask,
                        XSetWindowAttributes *a)
{
	attributes++;
	return 0;
}

int
XGetWindowProperty(Display *d, Window w, Atom property, long offset, long length,
                   Bool delete, Atom requestedtype, Atom *type, int *format,
                   ulong *items, ulong *remaining, uchar **data)
{
	Property *p;
	size_t size;

	assert(w == requestedwindow && property == requestedproperty);
	assert(propertyindex < propertycount);
	p = &properties[propertyindex++];
	assert(offset == p->offset);
	*type = p->type;
	*format = p->format;
	*items = p->items;
	*remaining = p->remaining;
	size = p->format == 32 ? p->items * sizeof(long) : p->items * p->format/8;
	*data = xmalloc(MAX(1, size));
	if (size)
		memcpy(*data, p->data, size);
	return p->error;
}

int
XLookupString(XKeyEvent *e, char *buf, int size, KeySym *key, XComposeStatus *status)
{
	*key = lookupkey;
	buf[0] = 'x';
	return 1;
}

/* A mouse binding that is not a paste, e.g. one sending Enter. */
static int mousecalls;
static void othermouse(const Arg *a) { mousecalls++; }

/* Count claims of PRIMARY, which a release ending a selection makes. */
static int owned;
int XSetSelectionOwner(Display *d, Atom selection, Window w, Time t) { owned++; return 1; }
Window XGetSelectionOwner(Display *d, Atom selection) { return xw.win; }

/* Record glyph placement without needing an X server or installed fonts. */
FT_UInt XftCharIndex(Display *d, XftFont *f, FcChar32 c) { return c; }

/* Digits 9 pixels tall leave 3 pixels above them (ascent 12) and 4 below
 * them in a 16-pixel row. */
void
XftTextExtentsUtf8(Display *d, XftFont *f, const FcChar8 *s, int len, XGlyphInfo *info)
{
	memset(info, 0, sizeof(*info));
	info->y = 9;
	info->height = 9;
}
Bool
XftColorAllocValue(Display *d, Visual *v, Colormap map, const XRenderColor *value, XftColor *color)
{
	color->color = *value;
	color->pixel = ((value->red >> 8) << 16) | ((value->green >> 8) << 8) |
	               (value->blue >> 8);
	return 1;
}
Bool XftDrawSetClip(XftDraw *d, Region r) { return 1; }
static int failclip;
Bool XftDrawSetClipRectangles(XftDraw *d, int x, int y, const XRectangle *r, int n) { return !failclip; }

void
XftDrawRect(XftDraw *d, const XftColor *color, int x, int y, uint w, uint h)
{
	int row, col;

	if (!w || !h)
		return;
	assert(!presenting || drawtarget(d) == vbell.buf || drawtarget(d) == xw.win);
	assert(x >= 0 && y >= 0 && x + w <= win.w && y + h <= win.h);
	assert(x % win.cw == 0 && y % win.ch == 0 && w % win.cw == 0 && h % win.ch == 0);
	assert(nrects < LEN(rects));
	rects[nrects++] = (Rect){ x / win.cw, y / win.ch, w / win.cw,
	                          h / win.ch, drawn++, color->pixel };
	if (!x && !y)
		framepixels[drawtarget(d)] = color->pixel;
	/* An opaque rectangle covers the glyphs drawn before it. */
	for (row = y / win.ch; row < (y + h) / win.ch && row < LEN(painted); row++) {
		for (col = x / win.cw; col < (x + w) / win.cw && col < LEN(painted[0]); col++) {
			painted[row][col] = 0;
			ink[row][col] = 0;
		}
	}
}

Picture XftDrawPicture(XftDraw *d) { return d == (Draw)3 && failpicture ? None : drawtarget(d); }

void
XRenderFillRectangle(Display *d, int op, Picture p, const XRenderColor *c,
                     int x, int y, unsigned int w, unsigned int h)
{
	assert(op == PictOpOver && w && h);
	assert(!presenting || p == vbell.buf || p == xw.win);
	assert(x >= 0 && y >= 0 && x + w <= win.w && y + h <= win.h);
	assert(c->red <= c->alpha && c->green <= c->alpha && c->blue <= c->alpha);
	assert(nblends < LEN(blends));
	blends[nblends++] = (Blend){ x, y, w, h, drawn++, c->red, c->green, c->blue, c->alpha };
	if (!x && !y) {
		const unsigned short channels[] = { c->blue, c->green, c->red };
		unsigned long pixel = 0;
		int i;

		for (i = 0; i < 3; i++) {
			unsigned int value = channels[i] * 255U +
			                     ((framepixels[p] >> (8 * i)) & 255) * (65535U - c->alpha);
			pixel |= MIN(255, (value + 32767) / 65535) << (8 * i);
		}
		framepixels[p] = pixel;
	}
}

void
XRenderFillRectangles(Display *d, int op, Picture p, const XRenderColor *c,
                      const XRectangle *r, int n)
{
	int i;

	bellfills++;
	for (i = 0; i < n; i++)
		XRenderFillRectangle(d, op, p, c, r[i].x, r[i].y, r[i].width, r[i].height);
}

void
XftDrawGlyphFontSpec(XftDraw *d, const XftColor *color, const XftGlyphFontSpec *s, int n)
{
	int i, x, y;

	assert(!presenting || drawtarget(d) == vbell.buf || drawtarget(d) == xw.win);
	for (i = 0; i < n; i++) {
		x = s[i].x / win.cw;
		y = (s[i].y - dc.font.ascent) / win.ch;
		assert(x >= 0 && x < win.tw / win.cw && y >= 0 && y < win.th / win.ch);
		painted[y][x] = s[i].glyph;
		ink[y][x] = color->pixel;
		glyphs++;
	}
}

int
XCopyArea(Display *d, Drawable src, Drawable dst, GC gc, int sx, int sy,
          uint w, uint h, int dx, int dy)
{
	assert(src == xw.buf || src == vbell.buf);
	if (dst == xw.win && pastemodal() && src == vbell.buf)
		assert(glyphs > 0); /* The confirmation must precede presentation. */
	else if (dst == vbell.buf || (dst == xw.win && pastemodal())) {
		assert(src == xw.buf && !nblends && !nrects);
		assert(framepixels[src] == 0x204060 || framepixels[src] == 0x80a0c0);
	}
	framepixels[dst] = framepixels[src];
	copies++;
	return 0;
}

int XSetForeground(Display *d, GC gc, unsigned long pixel) { return 0; }

static void
expectbytes(int fd, const void *text, size_t len)
{
	char buf[2048];
	ssize_t n;

	if (len) {
		n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
		if (n != len || memcmp(buf, text, len))
			fprintf(stderr, "Paste input mismatch: got %zd bytes, wanted %zu (%.*s)\n",
			        n, len, (int)len, (const char *)text);
		assert(n == len && !memcmp(buf, text, len));
	}
	n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
	assert(n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
}

static void
expect(int fd, const char *text)
{
	expectbytes(fd, text, text ? strlen(text) : 0);
}

static void
pressnow(KeySym key, uint state)
{
	XEvent e = { .xkey = { .type = KeyPress, .state = state } };

	lookupkey = key;
	kpress(&e);
}

/* Press a key as someone reading the dialog first would, after the delay
 * in which choices are ignored; pressnow() presses within it. */
static void
press(KeySym key, uint state)
{
	if (paste.state == PASTE_CONFIRM)
		paste.shown.tv_sec -= 1;
	if (pastefail.text[0])
		pastefail.shown.tv_sec -= 1;
	pressnow(key, state);
}

static void
supply(Property property, int incremental)
{
	XEvent e = {0};

	properties[0] = property;
	propertycount = 1;
	propertyindex = 0;
	if (incremental) {
		e.xproperty.type = PropertyNotify;
		e.xproperty.window = requestedwindow;
		e.xproperty.atom = requestedproperty;
		e.xproperty.state = PropertyNewValue;
		propnotify(&e);
	} else {
		e.xselection.type = SelectionNotify;
		e.xselection.requestor = requestedwindow;
		e.xselection.selection = requested;
		e.xselection.property = requestedproperty;
		e.xselection.time = requestedtime;
		e.xselection.target = xsel.xtarget;
		selnotify(&e);
	}
	assert(propertyindex == propertycount);
}

static Property
textproperty(const void *text, size_t len)
{
	return (Property){ .type = XA_STRING, .format = 8, .data = text, .items = len };
}

static void
transfer(const void *text, size_t len)
{
	assert(paste.state == PASTE_IDLE);
	selpaste(NULL);
	supply(textproperty(text, len), 0);
}

static void
startincr(void)
{
	static unsigned long size = 100;

	assert(paste.state == PASTE_IDLE);
	selpaste(NULL);
	supply((Property){ .type = 43, .format = 32, .data = &size, .items = 1 }, 0);
	assert(paste.state == PASTE_INCR);
}

/* Let an abandoned INCR owner finish: its chunks are deleted unread
 * until the empty last one. */
static void
drain(void)
{
	int deleted = deletes;

	assert(pastedrain.window == requestedwindow);
	supply((Property){ .type = XA_STRING, .format = 8, .remaining = 1000 }, 1);
	assert(pastedrain.window && deletes == deleted + 1);
	supply(textproperty("", 0), 1);
	assert(!pastedrain.window && deletes == deleted + 2);
}

/* Press, drag or release a button at a cell. */
static void
mouseat(int type, uint button, int col, int row)
{
	XEvent e = { .xbutton = { .type = type, .window = xw.win, .button = button,
	             .x = borderpx + col * win.cw, .y = borderpx + row * win.ch } };

	if (type == ButtonPress)
		bpress(&e);
	else if (type == ButtonRelease)
		brelease(&e);
	else
		bmotion(&e);
}

/* Drag Button1 from one cell to another. */
static void
drag(int col, int row, int tocol, int torow)
{
	mouseat(ButtonPress, Button1, col, row);
	mouseat(MotionNotify, 0, tocol, torow);
	mouseat(ButtonRelease, Button1, tocol, torow);
}

/* Click Button1 at a cell, without moving. */
static void
clickat(int col, int row)
{
	mouseat(ButtonPress, Button1, col, row);
	mouseat(ButtonRelease, Button1, col, row);
}

/* Turn the wheel a step, up for Button4 and down for Button5. */
static void
wheel(uint button)
{
	XEvent e = { .xbutton = { .type = ButtonPress, .window = xw.win,
	             .button = button } };

	bpress(&e);
	e.xbutton.type = ButtonRelease;
	brelease(&e);
}

static void
expectpaste(int fd, const char *text, size_t len, int bracketed)
{
	char wanted[512];
	size_t i, n = 6;

	assert(len + 12 <= sizeof(wanted));
	if (!bracketed) {
		expectbytes(fd, text, len);
		return;
	}
	/* ESC is dropped inside bracketed pastes. */
	memcpy(wanted, "\033[200~", 6);
	for (i = 0; i < len; i++)
		if (text[i] != '\033')
			wanted[n++] = text[i];
	memcpy(wanted + n, "\033[201~", 6);
	expectbytes(fd, wanted, n + 6);
}

/* Type an option's key: s and p without Shift, P and V with it. */
/* Answer the question that k asks before pasting the original. */
static void
answer(KeySym key)
{
	assert(paste.state == PASTE_CONFIRM && paste.asking);
	press(key, 0);
}

static void
presskey(char key)
{
	press(key, 0);
	if (key == 'k') /* asks first */
		answer(XK_y);
}

/* Select an option with the arrow keys and confirm it with Enter. */
static void
selectkey(int option)
{
	press(XK_Home, 0);
	while (option--)
		press(XK_Down, 0);
	paste.shown.tv_sec -= 1; /* past the Enter delay */
	press(XK_Return, 0);
	if (paste.state == PASTE_CONFIRM)
		answer(XK_y);
}

static void
testcontrols(int fd)
{
	int bracketed, c, before;
	char byte, preview[512];
	struct timespec later;

	for (bracketed = 0; bracketed <= 1; bracketed++) {
		testmouse_put(bracketed ? "\033[?2004h" : "\033[?2004l");
		for (c = 0; c <= 0x7f; c++) {
			if (c >= 0x20 && c != 0x7f)
				continue;
			byte = c;
			transfer(&byte, 1);
			assert(paste.state == PASTE_CONFIRM);
			assert(paste.lf == (c == '\n') && paste.tabs == (c == '\t'));
			assert(!paste.crlf && paste.danger == (c != '\n' && c != '\t'));
			assert(paste.noptions == (c == '\n' ? 2 : c == '\t' ? 4 : 3));
			expect(fd, NULL);
			if (c == '\n') {
				assert(pastebackground() == PASTE_BLUE);
				assert(paste.options[0].key == 's' && paste.options[1].key == 'p');
				assert(!strcmp(paste.options[1].label, "Keep LF"));
				presskey('s'); /* a former newline at the end leaves nothing */
				expectpaste(fd, "", 0, bracketed);
				transfer(&byte, 1);
				presskey('p');
				expectpaste(fd, "\r", 1, bracketed);
			} else if (c == '\t') {
				/* Trimming removes it, also as spaces. */
				assert(pastebackground() == PASTE_YELLOW);
				assert(!strcmp(paste.options[0].label, "TAB → SPACE, trim surrounding space"));
				assert(!strcmp(paste.options[1].label, "Trim trailing space, TAB → 4x SPACE"));
				assert(!strcmp(paste.options[2].label, "Trim trailing space, keep TAB"));
				assert(paste.options[3].key == 'k');
				presskey('s');
				expectpaste(fd, "", 0, bracketed);
				transfer(&byte, 1);
				presskey('p');
				expectpaste(fd, "", 0, bracketed);
				transfer(&byte, 1);
				presskey('t');
				expectpaste(fd, "", 0, bracketed);
				transfer(&byte, 1);
				presskey('k');
				expectpaste(fd, "\t", 1, bracketed);
			} else {
				/* A lone CR acts like Enter, so it is dangerous, too. */
				assert(pastebackground() == PASTE_RED);
				assert(paste.options[0].key == 's' && paste.options[1].key == 'p');
				assert(!strcmp(paste.options[1].label, "DANGER → SPACE"));
				assert(paste.options[2].key == 'k');
				presskey('s');
				expectpaste(fd, "", 0, bracketed);
				transfer(&byte, 1);
				presskey('p');
				expectpaste(fd, "", 0, bracketed);
				transfer(&byte, 1);
				presskey('k');
				expectpaste(fd, &byte, 1, bracketed);
			}
			assert(paste.state == PASTE_IDLE && !paste.data);
		}
	}
	testmouse_put("\033[?2004l");
	transfer("a\0b", 3);
	presskey('k');
	expectbytes(fd, "a\0b", 3);
	/* Keys work in either case, with Shift or Caps Lock. */
	transfer("a\033b", 3);
	press(XK_V, ShiftMask);
	assert(paste.viewing && paste.selected == -1);
	press(XK_V, LockMask);
	assert(!paste.viewing && paste.selected == -1);
	press(XK_t, 0); /* no tabs, so no t */
	assert(paste.state == PASTE_CONFIRM);
	expect(fd, NULL);
	press(XK_S, LockMask | ShiftMask);
	expect(fd, "a b");
	transfer("a\033b", 3);
	press(XK_P, ShiftMask);
	expect(fd, "a b");
	transfer("a\tb", 3);
	press(XK_T, LockMask);
	expect(fd, "a\tb");
	/* k, K or Enter on it select it and ask before pasting the original:
	 * No or Yes, No selected. y or Enter on Yes pastes; n, Esc or Enter on
	 * No go back; Ctrl+C cancels the paste. */
	transfer("a\033b", 3);
	assert(paste.noptions == 3 && paste.options[2].key == 'k');
	press(XK_K, ShiftMask);
	assert(paste.asking && !paste.askyes && paste.selected == 2);
	press(XK_s, 0);
	press(XK_v, 0);
	press(XK_y, ControlMask);
	assert(paste.asking && !paste.viewing && !paste.askyes);
	expect(fd, NULL);
	/* No is on the left, Yes on the right. */
	press(XK_Up, 0);
	assert(!paste.askyes);
	press(XK_KP_Down, 0);
	assert(paste.askyes);
	press(XK_KP_Left, 0);
	assert(!paste.askyes);
	press(XK_Right, 0);
	assert(paste.askyes);
	press(XK_Tab, 0);
	assert(!paste.askyes);
	press(XK_ISO_Left_Tab, ShiftMask);
	assert(paste.askyes);
	press(XK_End, 0);
	assert(paste.askyes);
	press(XK_Home, 0);
	assert(!paste.askyes);
	answer(XK_Return);
	assert(!paste.asking && paste.state == PASTE_CONFIRM && paste.selected == 2);
	press(XK_Home, 0);
	press(XK_k, LockMask);
	assert(paste.selected == 2);
	answer(XK_n);
	press(XK_k, 0);
	answer(XK_KP_Enter);
	press(XK_k, 0);
	answer(XK_Escape);
	assert(!paste.asking && paste.state == PASTE_CONFIRM);
	expect(fd, NULL);
	press(XK_k, 0);
	press(XK_Right, 0);
	answer(XK_Return);
	expect(fd, "a\033b");
	transfer("a\033b", 3);
	press(XK_End, 0);
	paste.shown.tv_sec -= 1;
	press(XK_Return, 0);
	answer(XK_Y);
	expect(fd, "a\033b");
	transfer("a\033b", 3);
	press(XK_k, 0);
	press(XK_c, ControlMask);
	assert(paste.state == PASTE_IDLE);
	expect(fd, NULL);
	transfer("echo command\n", 13);
	assert(paste.state == PASTE_CONFIRM);
	later = paste.progress;
	later.tv_sec += 60;
	assert(pasteremaining(later) == -1 && paste.state == PASTE_CONFIRM);
	before = requests;
	pressnow(XK_Return, 0); /* within the delay */
	pressnow(XK_KP_Enter, 0);
	pressnow(XK_p, 0);
	pressnow(XK_S, ShiftMask);
	press(XK_K, ShiftMask); /* The blue popup has no original override. */
	press(XK_k, 0);
	press(XK_t, 0);
	press(XK_n, 0);
	press(XK_p, ControlMask);
	press(XK_p, Mod1Mask);
	press(XK_s, ControlMask);
	press(XK_K, ControlMask | ShiftMask);
	clippaste(NULL);
	selpaste(NULL);
	assert(requests == before && paste.state == PASTE_CONFIRM);
	expect(fd, NULL);
	press(XK_Escape, 0);
	assert(paste.state == PASTE_IDLE);
	expect(fd, NULL);
	/* Ctrl+C cancels as well, also with Caps Lock, but not Ctrl+Shift+C,
	 * which copies in phist: it does nothing in the dialog. */
	transfer("cancel\n", 7);
	press(XK_c, ControlMask);
	assert(paste.state == PASTE_IDLE);
	transfer("cancel\n", 7);
	press(XK_C, ControlMask | LockMask);
	assert(paste.state == PASTE_IDLE);
	transfer("cancel\n", 7);
	owned = 0;
	press(XK_C, ControlMask | ShiftMask);
	press(XK_C, ControlMask | ShiftMask | LockMask);
	assert(paste.state == PASTE_CONFIRM && owned == 0);
	press(XK_Escape, 0);
	expect(fd, NULL);

	/* Check mode at delivery, after an arbitrary wait at the dialog. */
	transfer("a\033[201~b\n", 9);
	testmouse_put("\033[?2004h");
	presskey('k');
	expect(fd, "\033[200~a[201~b\r\033[201~");
	testmouse_put("\033[?2004l");
	transfer("a\033[201~b\n", 9);
	presskey('k');
	expectbytes(fd, "a\033[201~b\r", 9);

	/* UTF-8 continuation bytes in the C1 byte range are ordinary text. */
	transfer("\xd0\x9b", 2);
	expect(fd, "\xd0\x9b");
	/* C1 controls, encoded or as stray bytes, are dangerous controls. */
	transfer("a\xc2\x9b" "201~b", 8);
	assert(paste.state == PASTE_CONFIRM && paste.danger == 1);
	pastepreview(preview, 100);
	assert(!strcmp(preview, "a\\u009b201~b"));
	presskey('s');
	expect(fd, "a 201~b");
	transfer("a\x9b\x85\xc2\x80\tb\xe2\x82\xac", 10);
	assert(paste.danger == 3 && paste.tabs == 1);
	presskey('p'); /* the run of DANGER is next to a tab */
	expect(fd, "a    b\xe2\x82\xac");
	transfer("a\x9b" "201~", 6);
	presskey('k');
	expect(fd, "a\x9b" "201~");
	/* Sequences glibc accepts but UTF-8 forbids cannot hide a C1 byte. */
	transfer("x\xf4\xa0\xa0\x9b" "201~", 9); /* above U+10FFFF */
	assert(paste.state == PASTE_CONFIRM && paste.danger == 4);
	pastepreview(preview, 100);
	assert(!strcmp(preview, "x\\xf4\\xa0\\xa0\\x9b201~"));
	presskey('s');
	expect(fd, "x 201~");
	/* Overlong forms and surrogates are invalid bytes, each DANGER. */
	transfer("x\xc1\x9b\xe0\x82\x9b\xed\xa0\x80y", 10);
	assert(paste.danger == 8);
	presskey('s');
	expect(fd, "x y");
	transfer("x\xc1\x9b\xe0\x82\x9b\xed\xa0\x80y", 10);
	presskey('p');
	expect(fd, "x y");
	/* UTF-8 is decoded the same way whatever the process locale is. */
	assert(setlocale(LC_CTYPE, "C"));
	transfer("\xd0\x9b", 2);
	expect(fd, "\xd0\x9b");
	transfer("\xd0\x9b\x9b", 3);
	assert(paste.state == PASTE_CONFIRM && paste.danger == 1);
	presskey('s');
	expect(fd, "\xd0\x9b");
	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	transfer("\xd0\x9b\n\033\177\0\\\xff\xe2\x80\xae", 11);
	pastepreview(preview, 100);
	assert(!strcmp(preview, "\xd0\x9b\\n\\x1b\\x7f\\x00\\\\\\xff\\u202e"));
	pastepreview(preview, 5);
	assert(!strcmp(preview, "\xd0\x9b..."));
	press(XK_Escape, 0);

	/* Both selections use the same confirmation path; empty pastes send nothing. */
	clippaste(NULL);
	assert(requested == 42);
	supply(textproperty("clip\n", 5), 0);
	press(XK_S, LockMask);
	expect(fd, "clip");
	testmouse_put("\033[?2004h");
	transfer("", 0);
	assert(paste.state == PASTE_IDLE);
	expect(fd, NULL);
}

#define BYTES(s) { s, sizeof(s) - 1 }

/* The examples the dialog was designed from, plus lone CRs, squeezing,
 * trimming and the DANGER run rule. */
static const struct {
	struct { const char *s; size_t n; } text;
	int background;
	size_t lf, tabs, crlf, danger;
	struct { char key; const char *label; struct { const char *s; size_t n; } out; } options[4];
} examples[] = {
	{ BYTES("test\nexample\n"), PASTE_BLUE, 2, 0, 0, 0, {
		{ 's', "LF → SPACE, trim surrounding space", BYTES("test example") },
		{ 'p', "Keep LF", BYTES("test\rexample\r") } } },
	{ BYTES("test\texample"), PASTE_YELLOW, 0, 1, 0, 0, {
		{ 's', "TAB → SPACE", BYTES("test example") },
		{ 'p', "TAB → 4x SPACE", BYTES("test    example") },
		{ 't', "Keep TAB", BYTES("test\texample") } } },
	{ BYTES("test\texample\n"), PASTE_YELLOW, 1, 1, 0, 0, {
		{ 's', "LF|TAB → SPACE, trim surrounding space", BYTES("test example") },
		{ 'p', "Keep LF, TAB → 4x SPACE", BYTES("test    example\r") },
		{ 't', "Keep LF, keep TAB", BYTES("test\texample\r") } } },
	{ BYTES("te\tst\r\nexample\r\n"), PASTE_YELLOW, 0, 1, 2, 0, {
		{ 's', "TAB|CRLF → SPACE, trim surrounding space", BYTES("te st example") },
		{ 'p', "CRLF → LF, TAB → 4x SPACE", BYTES("te    st\rexample\r") },
		{ 't', "CRLF → LF, keep TAB", BYTES("te\tst\rexample\r") },
		{ 'k', "Keep CRLF, keep TAB", BYTES("te\tst\r\rexample\r\r") } } },
	{ BYTES("te\tst\r\nexample\r\n\0"), PASTE_RED, 0, 1, 2, 1, {
		{ 's', "TAB|CRLF|DANGER → SPACE, trim surrounding space",
		  BYTES("te st example") },
		{ 'p', "DANGER → SPACE, CRLF → LF, TAB → 4x SPACE",
		  BYTES("te    st\rexample\r") },
		{ 't', "DANGER → SPACE, CRLF → LF, keep TAB", BYTES("te\tst\rexample\r") },
		{ 'k', "Keep DANGER, keep CRLF, keep TAB", BYTES("te\tst\r\rexample\r\r\0") } } },
	{ BYTES("test\texample\0"), PASTE_RED, 0, 1, 0, 1, {
		{ 's', "TAB|DANGER → SPACE, trim surrounding space", BYTES("test example") },
		{ 'p', "DANGER → SPACE, TAB → 4x SPACE", BYTES("test    example") },
		{ 't', "DANGER → SPACE, keep TAB", BYTES("test\texample") },
		{ 'k', "Keep DANGER, keep TAB", BYTES("test\texample\0") } } },
	{ BYTES("test\nexample\0"), PASTE_RED, 1, 0, 0, 1, {
		{ 's', "LF|DANGER → SPACE, trim surrounding space", BYTES("test example") },
		{ 'p', "DANGER → SPACE, keep LF", BYTES("test\rexample") },
		{ 'k', "Keep DANGER, keep LF", BYTES("test\rexample\0") } } },
	{ BYTES("a\rb\r\r\n"), PASTE_RED, 0, 0, 1, 2, {
		{ 's', "CRLF|DANGER → SPACE, trim surrounding space", BYTES("a b") },
		{ 'p', "DANGER → SPACE, CRLF → LF", BYTES("a b\r") },
		{ 'k', "Keep DANGER, keep CRLF", BYTES("a\rb\r\r\r") } } },
	/* s squeezes original spaces, too. */
	{ BYTES("a \tb  c\n\n d \n"), PASTE_YELLOW, 3, 1, 0, 0, {
		{ 's', "LF|TAB → SPACE, squeeze all space, trim surrounding space",
		  BYTES("a b c d") },
		{ 'p', "Keep LF, trim trailing space, TAB → 4x SPACE",
		  BYTES("a     b  c\r\r d\r") },
		{ 't', "Keep LF, trim trailing space, keep TAB", BYTES("a \tb  c\r\r d\r") },
		{ 'k', "Keep LF, keep trailing space, keep TAB", BYTES("a \tb  c\r\r d \r") } } },
	/* Trailing blanks before LF and at the very end, after the last LF. */
	{ BYTES("line1    \nline2\n   "), PASTE_BLUE, 2, 0, 0, 0, {
		{ 's', "LF → SPACE, squeeze all space, trim surrounding space",
		  BYTES("line1 line2") },
		{ 'p', "Keep LF, trim trailing space", BYTES("line1\rline2\r") },
		{ 'k', "Keep LF, keep trailing space", BYTES("line1    \rline2\r   ") } } },
	{ BYTES("a\tb\t\nc"), PASTE_YELLOW, 1, 2, 0, 0, {
		{ 's', "LF|TAB → SPACE, squeeze all space", BYTES("a b c") },
		{ 'p', "Keep LF, trim trailing space, TAB → 4x SPACE", BYTES("a    b\rc") },
		{ 't', "Keep LF, trim trailing space, keep TAB", BYTES("a\tb\rc") },
		{ 'k', "Keep LF, keep trailing space, keep TAB", BYTES("a\tb\t\rc") } } },
	/* s trims indentation; the others trim the end without a final LF. */
	{ BYTES("  a\nb  "), PASTE_BLUE, 1, 0, 0, 0, {
		{ 's', "LF → SPACE, trim surrounding space", BYTES("a b") },
		{ 'p', "Keep LF, trim trailing space", BYTES("  a\rb") },
		{ 'k', "Keep LF, keep trailing space", BYTES("  a\rb  ") } } },
	/* p and t leave at most one newline at the end, also after blank lines
	 * with spaces and CR+LF; s leaves none. */
	{ BYTES("line\n\n \n\r\n"), PASTE_YELLOW, 3, 0, 1, 0, {
		{ 's', "LF|CRLF → SPACE, trim surrounding space", BYTES("line") },
		{ 'p', "Keep LF, CRLF → LF, trim trailing space", BYTES("line\r") },
		{ 'k', "Keep LF, keep CRLF, keep trailing space", BYTES("line\r\r \r\r\r") } } },
	{ BYTES("a\tb\n\n\n"), PASTE_YELLOW, 3, 1, 0, 0, {
		{ 's', "LF|TAB → SPACE, trim surrounding space", BYTES("a b") },
		{ 'p', "Keep LF, trim trailing space, TAB → 4x SPACE", BYTES("a    b\r") },
		{ 't', "Keep LF, trim trailing space, keep TAB", BYTES("a\tb\r") },
		{ 'k', "Keep LF, keep trailing space, keep TAB", BYTES("a\tb\r\r\r") } } },
	/* A run of DANGER becomes one space, none next to a blank or at either
	 * end of the paste. */
	{ BYTES("\033[31mred\033[0m\n"), PASTE_RED, 1, 0, 0, 2, {
		{ 's', "LF|DANGER → SPACE, trim surrounding space", BYTES("[31mred [0m") },
		{ 'p', "DANGER → SPACE, keep LF", BYTES("[31mred [0m\r") },
		{ 'k', "Keep DANGER, keep LF", BYTES("\033[31mred\033[0m\r") } } },
	{ BYTES("a \0b\1\2c\3 d\n\4e\5"), PASTE_RED, 1, 0, 0, 6, {
		{ 's', "LF|DANGER → SPACE, squeeze all space, trim surrounding space",
		  BYTES("a b c d e") },
		{ 'p', "DANGER → SPACE, keep LF", BYTES("a b c d\re") },
		{ 'k', "Keep DANGER, keep LF", BYTES("a \0b\1\2c\3 d\r\4e\5") } } },
	/* In t, a tab is a blank as well. */
	{ BYTES("\tx\033\ty"), PASTE_RED, 0, 2, 0, 1, {
		{ 's', "TAB|DANGER → SPACE, squeeze all space, trim surrounding space",
		  BYTES("x y") },
		{ 'p', "DANGER → SPACE, TAB → 4x SPACE", BYTES("    x    y") },
		{ 't', "DANGER → SPACE, keep TAB", BYTES("\tx\ty") },
		{ 'k', "Keep DANGER, keep TAB", BYTES("\tx\033\ty") } } },
	/* Bytes outside valid UTF-8, here Latin-1, are DANGER. */
	{ BYTES("caf\xe9 au lait\n"), PASTE_RED, 1, 0, 0, 1, {
		{ 's', "LF|DANGER → SPACE, squeeze all space, trim surrounding space",
		  BYTES("caf au lait") },
		{ 'p', "DANGER → SPACE, keep LF", BYTES("caf au lait\r") },
		{ 'k', "Keep DANGER, keep LF", BYTES("caf\xe9 au lait\r") } } },
};

static void
testactions(int fd)
{
	int bracketed, i, keyed;
	size_t e;

	for (bracketed = 0; bracketed <= 1; bracketed++) {
		testmouse_put(bracketed ? "\033[?2004h" : "\033[?2004l");
		for (e = 0; e < LEN(examples); e++) {
			for (i = 0; i < 4 && examples[e].options[i].key; i++) {
				for (keyed = 0; keyed <= 1; keyed++) {
					transfer(examples[e].text.s, examples[e].text.n);
					assert(paste.state == PASTE_CONFIRM);
					assert(pastebackground() == examples[e].background);
					assert(paste.lf == examples[e].lf && paste.tabs == examples[e].tabs);
					assert(paste.crlf == examples[e].crlf);
					assert(paste.danger == examples[e].danger);
					assert(paste.options[i].key == examples[e].options[i].key);
					assert(!strcmp(paste.options[i].label, examples[e].options[i].label));
					assert(paste.noptions == i + 1 || examples[e].options[i + 1].key);
					assert(paste.selected == 0);
					if (keyed)
						presskey(examples[e].options[i].key);
					else
						selectkey(i);
					assert(paste.state == PASTE_IDLE);
					expectpaste(fd, examples[e].options[i].out.s,
					            examples[e].options[i].out.n, bracketed);
				}
			}
			assert(i == 4 || !examples[e].options[i].key);
		}
	}
}

static void
testtransfers(int fd)
{
	XEvent e = {0};
	char large[513], wanted[540];
	int before;
	Property p;
	size_t limit = pastemaxbytes;
	struct timespec now;

	testmouse_put("\033[?2004h");
	startincr();
	before = requests;
	clippaste(NULL);
	assert(requests == before);
	e.xproperty.type = PropertyNotify;
	e.xproperty.window = requestedwindow;
	e.xproperty.atom = 999;
	e.xproperty.state = PropertyNewValue;
	propnotify(&e);
	assert(paste.state == PASTE_INCR && !paste.len);
	supply(textproperty("one ", 4), 1);
	expect(fd, NULL);
	supply(textproperty("two!", 4), 1);
	expect(fd, NULL);
	supply(textproperty("", 0), 1);
	expect(fd, "\033[200~one two!\033[201~");
	assert(paste.state == PASTE_IDLE);

	startincr();
	supply(textproperty("first\n", 6), 1);
	supply(textproperty("\tnext\n", 6), 1);
	expect(fd, NULL);
	supply(textproperty("", 0), 1);
	assert(paste.lf == 2 && paste.tabs == 1 && !paste.danger);
	presskey('p');
	expect(fd, "\033[200~first\r    next\r\033[201~");

	/* A C1 character split across chunks is still one control. */
	startincr();
	supply(textproperty("x\xc2", 2), 1);
	supply(textproperty("\x9by", 2), 1);
	supply(textproperty("", 0), 1);
	assert(paste.state == PASTE_CONFIRM && paste.danger == 1);
	presskey('s');
	expect(fd, "\033[200~x y\033[201~");

	/* INCR announcements without a size, as xclip sends for selections
	 * of a megabyte or more, or with another format, start a transfer. */
	selpaste(NULL);
	supply((Property){ .type = 43, .format = 32 }, 0);
	assert(paste.state == PASTE_INCR);
	supply(textproperty("no size", 7), 1);
	supply(textproperty("", 0), 1);
	expect(fd, "\033[200~no size\033[201~");
	selpaste(NULL);
	supply((Property){ .type = 43, .format = 8, .data = "x", .items = 1 }, 0);
	assert(paste.state == PASTE_INCR);
	supply(textproperty("odd hint", 8), 1);
	supply(textproperty("", 0), 1);
	expect(fd, "\033[200~odd hint\033[201~");

	/* CRLF normalization and tab detection span INCR chunk boundaries. */
	startincr();
	supply(textproperty("one\r", 4), 1);
	supply(textproperty("\ntwo\t", 5), 1);
	expect(fd, NULL);
	supply(textproperty("", 0), 1);
	assert(paste.tabs == 1 && paste.crlf == 1 && !paste.lf && !paste.danger);
	presskey('k');
	expect(fd, "\033[200~one\r\rtwo\t\033[201~");

	startincr();
	supply(textproperty("echo", 4), 1);
	expect(fd, NULL);
	testmouse_put("\033[?2004l");
	supply(textproperty("\r", 1), 1);
	supply(textproperty("\nnext\n", 6), 1);
	expect(fd, NULL);
	supply(textproperty("", 0), 1);
	assert(paste.state == PASTE_CONFIRM && paste.crlf == 1 && paste.lf == 1);
	assert(!paste.danger && paste.options[0].key == 's');
	assert(paste.options[1].key == 'p' && paste.options[2].key == 'k');
	presskey('s');
	expect(fd, "echo next");

	/* An owner echoing another target than the requested one still answers
	 * the request; phist never requests any other. */
	testmouse_put("\033[?2004l");
	selpaste(NULL);
	properties[0] = textproperty("other target", 12);
	propertycount = 1;
	propertyindex = 0;
	e.xselection.type = SelectionNotify;
	e.xselection.requestor = requestedwindow;
	e.xselection.selection = requested;
	e.xselection.property = requestedproperty;
	e.xselection.time = requestedtime;
	e.xselection.target = XA_STRING;
	selnotify(&e);
	assert(propertyindex == 1 && paste.state == PASTE_IDLE);
	expect(fd, "other target");

	/* Multiple property reads are one paste, too (offsets are 32-bit units). */
	testmouse_put("\033[?2004h");
	selpaste(NULL);
	properties[0] = textproperty("one\n", 4);
	properties[0].remaining = 4;
	properties[1] = textproperty("two\n", 4);
	properties[1].offset = 1;
	propertycount = 2;
	propertyindex = 0;
	e.xselection.type = SelectionNotify;
	e.xselection.requestor = requestedwindow;
	e.xselection.selection = requested;
	e.xselection.property = requestedproperty;
	e.xselection.time = requestedtime;
	e.xselection.target = xsel.xtarget;
	selnotify(&e);
	assert(propertyindex == 2 && paste.state == PASTE_CONFIRM);
	expect(fd, NULL);
	press(XK_p, 0);
	expect(fd, "\033[200~one\rtwo\r\033[201~");

	/* A paste is queued whole with the mode it was sent in; output read
	 * while it is written, here a mode switch, applies to the next one. */
	memset(large, 'a', 512);
	large[512] = 0;
	assert(write(fd, "\033[?2004l", 8) == 8);
	transfer(large, 512);
	ttyread();
	assert(!(win.mode & MODE_BRCKTPASTE));
	snprintf(wanted, sizeof(wanted), "\033[200~%s\033[201~", large);
	expect(fd, wanted);
	memcpy(large, "\033[201~", 6);
	transfer(large, 512);
	expect(fd, NULL);
	assert(write(fd, "\033[?2004h", 8) == 8);
	presskey('k');
	ttyread();
	assert(win.mode & MODE_BRCKTPASTE);
	expect(fd, large);

	/* A paste larger than the child takes at once does not block phist: the
	 * rest is written as it reads, and keys typed meanwhile follow it. */
	{
		static char big[1 << 20], got[(1 << 20) + 16];
		size_t total = 0;
		ssize_t r;

		testmouse_put("\033[?2004l");
		memset(big, 'b', sizeof(big));
		big[sizeof(big) - 1] = '\n';
		alarm(10);
		transfer(big, sizeof(big));
		presskey('p');
		assert(paste.state == PASTE_IDLE && ttywritepending());
		pressnow(XK_x, 0); /* typed while the paste is written */
		assert(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
		while (ttywritepending() || total < sizeof(big) + 1) {
			while ((r = read(fd, got + total, sizeof(got) - total)) > 0)
				total += r;
			ttyflushwrites();
		}
		assert(fcntl(fd, F_SETFL, 0) == 0);
		alarm(0);
		assert(total == sizeof(big) + 1);
		assert(got[sizeof(big) - 2] == 'b' && got[sizeof(big) - 1] == '\r');
		assert(got[sizeof(big)] == 'x');

		/* A paste that does not fit the queue sends none of it. */
		total = 0;
		alarm(30);
		do {
			ttypaste(big, sizeof(big), 0);
			total += sizeof(big);
		} while (ttyinputfits(sizeof(big)));
		transfer(big, sizeof(big));
		presskey('p');
		assert(paste.state == PASTE_IDLE);
		assert(!strcmp(pastefail.text, "The program is not reading its input"));
		pastefailclear();
		assert(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
		while (ttywritepending()) {
			while ((r = read(fd, got, sizeof(got))) > 0)
				total -= r;
			ttyflushwrites();
		}
		while ((r = read(fd, got, sizeof(got))) > 0)
			total -= r;
		assert(fcntl(fd, F_SETFL, 0) == 0);
		alarm(0);
		assert(!total);
		testmouse_put("\033[?2004h");
	}

	/* Any 8-bit reply type is text, but it must not change within a paste. */
	selpaste(NULL);
	p = textproperty("text", 4);
	p.type = 77; /* e.g. TEXT or COMPOUND_TEXT */
	supply(p, 0);
	expect(fd, "\033[200~text\033[201~");

	/* A failure or limit violation must discard all buffered data. */
	startincr();
	supply(textproperty("safe prefix", 11), 1);
	p = textproperty("bad", 3);
	p.format = 16;
	p.items = 1;
	supply(p, 1);
	assert(paste.state == PASTE_IDLE && !paste.data);
	drain();
	expect(fd, NULL);
	startincr();
	supply(textproperty("prefix", 6), 1);
	p = textproperty("bad", 3);
	p.type = xsel.xtarget;
	supply(p, 1);
	assert(paste.state == PASTE_IDLE);
	drain();
	expect(fd, NULL);
	selpaste(NULL);
	p = textproperty("", 0);
	p.error = BadAlloc;
	supply(p, 0);
	assert(paste.state == PASTE_IDLE);
	startincr();
	p = (Property){ .type = 43, .format = 32, .data = &limit, .items = 1 };
	supply(p, 1);
	assert(paste.state == PASTE_IDLE);
	drain();
	pastemaxbytes = 7;
	startincr();
	supply(textproperty("1234", 4), 1);
	supply(textproperty("5678", 4), 1);
	assert(paste.state == PASTE_IDLE && !paste.data);
	drain();
	transfer("12345678", 8);
	assert(paste.state == PASTE_IDLE);
	transfer("1234567", 7);
	expect(fd, "\033[200~1234567\033[201~");
	pastemaxbytes = limit;
	selpaste(NULL);
	p = textproperty("a", 1);
	p.remaining = 1; /* Cannot advance a 32-bit property offset by one byte. */
	supply(p, 0);
	assert(paste.state == PASTE_IDLE);
	expect(fd, NULL);

	/* Ignore unrelated/unsolicited events, handle a refused conversion. */
	selpaste(NULL);
	e.xselection.requestor = requestedwindow;
	e.xselection.property = None;
	e.xselection.time = requestedtime;
	e.xselection.selection = 999;
	selnotify(&e);
	assert(paste.state == PASTE_WAIT);
	e.xselection.selection = requested;
	selnotify(&e);
	assert(paste.state == PASTE_IDLE);
	e.xselection.property = requestedproperty;
	selnotify(&e);
	assert(paste.state == PASTE_IDLE);

	selpaste(NULL);
	now = paste.progress;
	assert(pasteremaining(now) == pastetimeout);
	now.tv_sec += pastetimeout / 1000 + 1;
	pasteremaining(now);
	assert(paste.state == PASTE_IDLE);
	startincr();
	supply(textproperty("prefix", 6), 1);
	now = paste.progress;
	now.tv_sec += pastetimeout / 1000 + 1;
	pasteremaining(now);
	assert(paste.state == PASTE_IDLE && !paste.data);
	expect(fd, NULL);
}

static void
refuse(void)
{
	XEvent e = { .xselection = { .type = SelectionNotify,
	             .requestor = requestedwindow, .selection = requested,
	             .target = xsel.xtarget, .property = None,
	             .time = requestedtime } };

	selnotify(&e);
}

static void
abandon(void)
{
	struct timespec now = paste.progress;

	now.tv_sec += pastetimeout / 1000 + 1;
	pasteremaining(now);
	assert(paste.state == PASTE_IDLE);
}

/* Deliver a late reply and a late INCR chunk for an earlier request to
 * its window. */
static void
latereply(int fd, Window window)
{
	XEvent e = { .xselection = { .type = SelectionNotify, .requestor = window,
	             .selection = requested, .target = xsel.xtarget,
	             .property = requestedproperty, .time = requestedtime } };

	assert(paste.state == PASTE_WAIT && requestedwindow != window);
	selnotify(&e);
	e = (XEvent){ .xproperty = { .type = PropertyNotify, .window = window,
	              .atom = requestedproperty, .state = PropertyNewValue } };
	propnotify(&e);
	assert(paste.state == PASTE_WAIT && !paste.len);
	supply(textproperty("fresh", 5), 0);
	expect(fd, "\033[200~fresh\033[201~");
}

static void
testendlessdrain(void)
{
	struct timespec now;
	Window stale;
	int asked;
	size_t limit = pastemaxbytes;

	pasteclear();
	pastefailclear();
	pastemaxbytes = 7;
	startincr();
	supply(textproperty("12345678", 8), 1);
	pastemaxbytes = limit;
	assert(paste.state == PASTE_IDLE && pastedrain.window);
	pastefailclear();
	stale = requestedwindow;
	asked = requests;
	selpaste(NULL);
	assert(paste.state == PASTE_WAIT && requests == asked);
	/* Chunks in time keep it going, however many there are, but the time they
	 * leave only shrinks. */
	supply((Property){ .type = XA_STRING, .format = 8, .remaining = 1000 }, 1);
	now = pastedrain.progress;
	assert(pasteremaining(now) <= pastetimeout);
	assert(pastedrain.window && requests == asked);
	pastedrain.started.tv_sec -= (time_t)(PASTE_DRAINMS / 1000) - 1;
	supply((Property){ .type = XA_STRING, .format = 8, .remaining = 1000 }, 1);
	now = pastedrain.progress;
	assert(pastedrain.window && requests == asked);
	assert(pasteremaining(now) <= 1000);
	/* At the limit the waiting paste asks anyway, on a new window, so that a
	 * new owner can answer. */
	now.tv_sec += 1;
	pasteremaining(now);
	assert(!pastedrain.window && paste.state == PASTE_WAIT && requests == asked + 1);
	assert(requestedwindow != stale);
	pasteclear();
}

static void
testrequests(int fd)
{
	Window reused, stale, windows[40];
	Property p;
	Time drainedtime;
	struct timespec now;
	size_t limit;
	int i, asked, ticks;

	testmouse_put("\033[?2004h");
	/* Finished conversions, including refusals, reuse their property. */
	transfer("one", 3);
	expect(fd, "\033[200~one\033[201~");
	reused = requestedwindow;
	selpaste(NULL);
	refuse();
	transfer("two\n", 4);
	press(XK_Escape, 0);
	selpaste(NULL);
	assert(requestedwindow == reused);
	pasteclear();

	/* An abandoned property is not used again while it rests, however many
	 * pastes follow. */
	selpaste(NULL);
	stale = requestedwindow;
	abandon();
	for (i = 0; i < 40; i++) {
		transfer("x", 1);
		expect(fd, "\033[200~x\033[201~");
		assert(requestedwindow != stale);
	}
	reused = requestedwindow;
	selpaste(NULL);
	latereply(fd, stale);

	/* Abandoned INCR owners may still send chunks to the old property. */
	startincr();
	supply(textproperty("old", 3), 1);
	stale = requestedwindow;
	abandon();
	selpaste(NULL);
	latereply(fd, stale);

	/* Stale INCR chunks are ignored during a new INCR transfer too. */
	startincr();
	{
		XEvent chunk = { .xproperty = { .type = PropertyNotify,
		                .window = stale, .atom = requestedproperty,
		                .state = PropertyNewValue } };

		propnotify(&chunk);
		assert(paste.state == PASTE_INCR && !paste.len);
		supply(textproperty("fresh", 5), 1);
		supply(textproperty("", 0), 1);
		expect(fd, "\033[200~fresh\033[201~");
	}

	/* A reply failing validation was written whole, so its owner is done
	 * with the property, which stays in use. */
	selpaste(NULL);
	stale = requestedwindow;
	supply((Property){ .type = XA_STRING, .format = 16, .data = "ab", .items = 1 }, 0);
	assert(paste.state == PASTE_IDLE);
	selpaste(NULL);
	assert(requestedwindow == stale);
	abandon();

	/* A late refusal cannot cancel the current paste, even for the same
	 * selection and target. Refusals after abandonment keep reusing the
	 * new property, rather than interning an atom for each empty paste. */
	selpaste(NULL);
	assert(requestedwindow != stale);
	{
		XEvent late = { .xselection = { .type = SelectionNotify,
		               .selection = requested, .target = xsel.xtarget,
		               .property = None, .time = requestedtime } };

		abandon();
		selpaste(NULL);
		reused = requestedwindow;
		late.xselection.requestor = reused; /* only the time is old */
		selnotify(&late);
		assert(paste.state == PASTE_WAIT);
		supply(textproperty("fresh", 5), 0);
		expect(fd, "\033[200~fresh\033[201~");
		for (i = 0; i < 100; i++) {
			selpaste(NULL);
			assert(requestedwindow == reused);
			selnotify(&late);
			assert(paste.state == PASTE_WAIT);
			refuse();
			assert(paste.state == PASTE_IDLE);
		}
	}

	/* Wrong and zero timestamps are ignored for replies and refusals.
	 * Accepting a zero-time refusal would recreate the stale-reply bug. */
	selpaste(NULL);
	{
		XEvent wrong = { .xselection = { .type = SelectionNotify,
		                .requestor = requestedwindow, .selection = requested,
		                .target = xsel.xtarget, .property = requestedproperty,
		                .time = requestedtime - 1 } };

		selnotify(&wrong);
		assert(paste.state == PASTE_WAIT);
		wrong.xselection.time = CurrentTime;
		selnotify(&wrong);
		wrong.xselection.property = None;
		selnotify(&wrong);
		assert(paste.state == PASTE_WAIT);
		refuse();
	}

	/* Same-tick requests wait for a distinct server timestamp, and the
	 * 32-bit server clock can wrap without sending CurrentTime (zero). */
	for (i = 0; i < 3; i++) {
		int before = testpastetimequeries;

		testpastetime = i == 0 ? requestedtime : i == 1 ? UINT32_MAX : 0;
		selpaste(NULL);
		assert(testpastetimequeries == before + (i == 1 ? 1 : 2));
		assert(requestedtime != CurrentTime && requestedwindow == reused);
		refuse();
	}
	expect(fd, NULL);

	/* An owner whose INCR transfer was abandoned for exceeding the limit
	 * still sends chunks, and xclip ignores requests until the last one.
	 * The chunks are deleted until then, a paste meanwhile waits for that,
	 * and the property stays in use. */
	limit = pastemaxbytes;
	pastemaxbytes = 7;
	startincr();
	supply(textproperty("12345678", 8), 1);
	pastemaxbytes = limit;
	assert(paste.state == PASTE_IDLE && pastefail.text[0]);
	reused = requestedwindow;
	drainedtime = requestedtime;
	ticks = testpastetimequeries;
	asked = requests;
	selpaste(NULL);
	assert(!pastefail.text[0] && paste.state == PASTE_WAIT && requests == asked);
	assert(!paste.time && testpastetimequeries == ticks);
	/* Neither a refusal nor the request's own timeout ends the wait,
	 * while the drain makes progress, within its total limit. */
	refuse();
	assert(paste.state == PASTE_WAIT);
	paste.progress.tv_sec -= pastetimeout / 1000 + 1;
	clock_gettime(CLOCK_MONOTONIC, &pastedrain.progress);
	assert(pasteremaining(pastedrain.progress) == pastetimeout);
	assert(paste.state == PASTE_WAIT && requests == asked);
	drain();
	assert(requests == asked + 1 && requestedwindow == reused);
	assert(requestedtime != drainedtime && testpastetimequeries == ticks + 1);
	{
		/* Draining allows property reuse, but an old refusal still must
		 * not cancel the conversion sent after the drain finished. */
		XEvent late = { .xselection = { .type = SelectionNotify,
		               .requestor = requestedwindow, .selection = requested,
		               .target = xsel.xtarget, .property = None,
		               .time = drainedtime } };

		selnotify(&late);
		assert(paste.state == PASTE_WAIT);
	}
	supply(textproperty("fresh", 5), 0);
	expect(fd, "\033[200~fresh\033[201~");
	/* An owner that stops sending chunks is given up on after the
	 * timeout, and its property is retired. */
	pastemaxbytes = 7;
	startincr();
	supply(textproperty("12345678", 8), 1);
	pastemaxbytes = limit;
	stale = requestedwindow;
	selpaste(NULL);
	assert(requests == asked + 2);
	now = pastedrain.progress;
	now.tv_sec += pastetimeout / 1000 + 1;
	pasteremaining(now);
	assert(!pastedrain.window);
	assert(paste.state == PASTE_WAIT && requests == asked + 3);
	assert(requestedwindow != stale);
	latereply(fd, stale);

	/* A reply that could not be read may have announced INCR, which
	 * deleting it acknowledged, so its window is not used again. */
	selpaste(NULL);
	stale = requestedwindow;
	p = textproperty("", 0);
	p.error = BadAlloc;
	supply(p, 0);
	assert(paste.state == PASTE_IDLE);
	selpaste(NULL);
	latereply(fd, stale);

	/* Every abandoned window is kept, never destroyed, and no longer
	 * watched; each abandonment takes a new window. */
	for (i = 0; i < 40; i++) {
		int unwatched = testpasteunwatched;

		selpaste(NULL);
		windows[i] = requestedwindow;
		assert(!i || windows[i] > windows[i - 1]);
		abandon();
		assert(testpasteunwatched == unwatched + 1);
	}
	selpaste(NULL);
	supply(textproperty("fresh", 5), 0);
	expect(fd, "\033[200~fresh\033[201~");
}

static void
resizewindow(int cols, int rows)
{
	win.w = win.tw = cols * win.cw;
	win.h = win.th = rows * win.ch;
}

static void
drawdialog(void)
{
	memset(painted, 0, sizeof(painted));
	memset(ink, 0, sizeof(ink));
	nrects = nblends = 0;
	pastedraw();
}

/* Return the first window row at or after from whose text contains s, or -1. */
static int
dialogrowfrom(int from, const char *s)
{
	char text[128 * 4 + 1];
	int x, y;
	size_t n;

	for (y = MAX(0, from); y < win.th / win.ch; y++) {
		for (x = n = 0; x < win.tw / win.cw; x++)
			n += painted[y][x] ? utf8encode(painted[y][x], text + n) : (text[n] = ' ', 1);
		text[n] = '\0';
		if (strstr(text, s))
			return y;
	}
	return -1;
}

static int
dialogrow(const char *s)
{
	return dialogrowfrom(0, s);
}

static unsigned long
pixel(int index)
{
	const XRenderColor *c = &pastevalues[index];

	return ((c->red >> 8) << 16) | ((c->green >> 8) << 8) | (c->blue >> 8);
}

/* The window is tinted, then the box is raised with a shadow and a bevel. */
static void
expectframe(int background)
{
	int i, shadows = 0, light = 0, dark = 0;
	unsigned short alpha = 0x3000;

	assert(nrects >= 1 && rects[0].pixel == pixel(background));
	assert(nblends > 3 && blends[0].x == 0 && blends[0].y == 0);
	assert(blends[0].w == win.w && blends[0].h == win.h && blends[0].alpha == alpha);
	assert(blends[0].red == pastevalues[background].red * alpha / 0xffff);
	assert(blends[0].blue == pastevalues[background].blue * alpha / 0xffff);
	for (i = 1; i < nblends; i++) {
		if (blends[i].order < rects[0].order)
			shadows++;
		else if (blends[i].red == blends[i].alpha)
			light++;
		else if (!blends[i].red)
			dark++;
	}
	assert(shadows >= 1 && light >= 2 && dark >= 2);
}

/* Check the dialog for an example at its current selection. */
static void
expectdialog(size_t e)
{
	char line[128], text[512], chips[6][32];
	int i, y, last, n, x, k, sunk, left, inner;
	int cols = win.tw / win.cw, rows = win.th / win.ch;
	int bg = examples[e].background, t = win.ch / 4;
	int tc = bg == PASTE_RED ? PASTE_DARKRED : bg == PASTE_YELLOW ? PASTE_DARKYELLOW :
	         PASTE_DARKBLUE;
	int fgcolor = bg == PASTE_RED ? PASTE_GOLD : bg == PASTE_YELLOW ? PASTE_BLACK :
	              PASTE_WHITE;
	unsigned long fg = pixel(fgcolor);
	const char *title = examples[e].danger ? "Paste contains dangerous characters" :
	                    examples[e].tabs && examples[e].crlf ?
	                    "Paste contains tabs and CRLF line endings" :
	                    examples[e].tabs ? "Paste contains tabs" :
	                    examples[e].crlf ? "Paste contains CRLF line endings" :
	                    "Paste contains multiple lines";
	Rect *box = &rects[0], *bar = &rects[2];

	expectframe(bg);
	assert(nrects == 3 && bar->pixel == fg && bar->h == 1);
	assert(abs(2 * box->x + box->w - cols) <= 1);
	assert(abs(2 * box->y + box->h - rows) <= 1);
	/* The title bar is the top row in a darker shade, a little taller, and
	 * says why the dialog appeared. */
	assert(rects[1].pixel == pixel(tc) && rects[1].x == box->x && rects[1].w == box->w);
	assert(rects[1].y == box->y && rects[1].h == 1 && rects[1].order < bar->order);
	/* It reaches up a quarter row and 1 pixel more, like the fields, and a
	 * quarter row down. */
	for (i = n = 0; i < nblends; i++)
		n += blends[i].alpha == 0xffff &&
		     blends[i].red == pastevalues[tc].red && blends[i].blue == pastevalues[tc].blue &&
		     blends[i].x == box->x * win.cw && blends[i].w == box->w * win.cw &&
		     ((blends[i].y == box->y * win.ch - t - 1 && blends[i].h == t + 1) ||
		      (blends[i].y == (box->y + 1) * win.ch && blends[i].h == t));
	assert(n == 2);
	/* The title and chips start where the key caps do, at column 2. */
	y = dialogrow(title);
	for (left = box->x; !painted[y][left]; left++)
		;
	left -= 2;
	inner = box->w - 2 * (left - box->x);
	assert(y == box->y && painted[y][left + 2] == 'P' && ink[y][left + 2] == fg);
	assert(dialogrow("Confirm") == -1 && dialogrow("DANGER!") != y);
	/* The size, characters and counts are two-tone chips a row high and a
	 * cell apart: the number on a block in the title bar's shade, the unit
	 * on a darker fill. */
	n = 0;
	snprintf(chips[n++], 32, "%zu bytes", examples[e].text.n);
	for (i = x = 0; i < examples[e].text.n; x++) {
		Rune u;
		size_t k = pasteutf8(examples[e].text.s + i, examples[e].text.n - i, &u);
		i += k ? k : 1;
	}
	snprintf(chips[n++], 32, "%d characters", x);
	assert(paste.chars == x);
	if (examples[e].lf)
		snprintf(chips[n++], 32, "%zu LF", examples[e].lf);
	if (examples[e].tabs)
		snprintf(chips[n++], 32, "%zu TAB", examples[e].tabs);
	if (examples[e].crlf)
		snprintf(chips[n++], 32, "%zu CRLF", examples[e].crlf);
	if (examples[e].danger)
		snprintf(chips[n++], 32, "%zu DANGER!", examples[e].danger);
	assert((last = dialogrow(" bytes ")) > y);
	for (i = 0, x = left + 2; i < n; x += strlen(chips[i++]) + 4) {
		const char *unit = strchr(chips[i], ' ') + 1;
		int digits = unit - chips[i] - 1, block = 0;

		snprintf(line, sizeof(line), " %.*s  %s ", digits, chips[i], unit);
		assert(dialogrow(line) == last && !painted[last][x] && !painted[last][x - 1]);
		assert(painted[last][x + 1] == chips[i][0] && ink[last][x + 1] == fg);
		assert(painted[last][x + digits + 3] == unit[0]);
		assert(ink[last][x + digits + 3] == fg);
		for (k = sunk = 0; k < nblends; k++) {
			/* Reaching up 1 pixel leaves 4 above the digits, as below. */
			if (blends[k].y != last * win.ch - 1 || blends[k].h != win.ch + 1)
				continue;
			block += blends[k].alpha == 0xffff && blends[k].x == x * win.cw &&
			         blends[k].w == (digits + 2) * win.cw &&
			         pixel(tc) == (unsigned long)(((blends[k].red >> 8) << 16) |
			         ((blends[k].green >> 8) << 8) | (blends[k].blue >> 8));
			sunk += blends[k].alpha == 0x4000 && !blends[k].red &&
			        blends[k].x == (x + digits + 2) * win.cw &&
			        blends[k].w == (strlen(unit) + 2) * win.cw;
		}
		assert(block == 1 && sunk == 1);
		/* Each part is one fill, so both are the same size. */
		for (k = block = 0; k < nblends; k++)
			block += blends[k].y < (last + 1) * win.ch &&
			         blends[k].y + blends[k].h > last * win.ch &&
			         (blends[k].x == x * win.cw ||
			          blends[k].x == (x + digits + 2) * win.cw);
		assert(block == 2);
	}
	/* The preview is "V View" and the paste in a sunken field, which is
	 * inverted when selected. */
	snprintf(line, sizeof(line), "%s  V   View", paste.selected < 0 ? "▸" : " ");
	assert((y = dialogrow(line)) > last);
	pastepreview(text, inner - 14);
	assert(dialogrow(text) == y && painted[y][left + 13] == (uchar)text[0]);
	assert(ink[y][left] == fg || paste.selected >= 0);
	if (paste.selected < 0) {
		assert(bar->y == y && bar->x == left + 12 && bar->w == inner - 12);
		assert(ink[y][left + 13] == pixel(bg) && ink[y][left + 7] == fg);
	}
	/* The field reaches up 1 pixel like the chips: darker, or the
	 * inverted bar's color when selected. */
	for (i = 0, last = n = 0; i < nblends; i++) {
		if (blends[i].x != borderpx + (left + 12) * win.cw ||
		    blends[i].w != (inner - 12) * win.cw)
			continue;
		last += blends[i].y == y * win.ch - 1 && blends[i].h == win.ch + 1 &&
		        blends[i].alpha == 0x4000 && !blends[i].red;
		n += blends[i].y == y * win.ch - 1 && blends[i].h == 1 &&
		     blends[i].alpha == 0xffff && blends[i].red == pastevalues[fgcolor].red &&
		     blends[i].blue == pastevalues[fgcolor].blue;
	}
	assert(last == (paste.selected >= 0) && n == (paste.selected < 0));
	/* Choices are a blank row apart. Only the label of the selected one is
	 * inverted, reaching up 1 pixel like the preview field. */
	for (i = 0; i < paste.noptions; i++) {
		snprintf(line, sizeof(line), "%s  %c   %s", i == paste.selected ? "▸" : " ",
		         toupper(examples[e].options[i].key), examples[e].options[i].label);
		last = y;
		assert((y = dialogrowfrom(y + 1, line)) > 0);
		assert(i == 0 || y == last + 2);
		assert(ink[y][left + 3] == fg);
		if (i == paste.selected) {
			for (n = 0, x = 0; examples[e].options[i].label[x]; x++)
				n += (examples[e].options[i].label[x] & 0xc0) != 0x80;
			assert(bar->y == y && bar->x == left + 6 && bar->w == n + 2);
			assert(ink[y][left] == fg && ink[y][left + 7] == pixel(bg));
			for (k = n = 0; k < nblends; k++)
				n += blends[k].alpha == 0xffff && blends[k].h == 1 &&
				     blends[k].y == y * win.ch - 1 &&
				     blends[k].x == bar->x * win.cw && blends[k].w == bar->w * win.cw &&
				     blends[k].red == pastevalues[fgcolor].red &&
				     blends[k].blue == pastevalues[fgcolor].blue;
			assert(n == 1);
		} else {
			assert(ink[y][left + 7] == fg);
		}
	}
	/* Every key, v included, is on a raised key cap reaching up 1 pixel
	 * like the preview field: 17 pixels with its 1-pixel bottom edge. */
	for (i = n = 0; i < nblends; i++)
		n += blends[i].alpha == 0x4000 && blends[i].red == 0x4000 &&
		     blends[i].x == (left + 2) * win.cw && blends[i].w == 3 * win.cw &&
		     blends[i].y % win.ch == win.ch - 1 && blends[i].h == win.ch;
	assert(n == paste.noptions + 1);
	/* No help text below the choices. */
	assert(dialogrowfrom(y + 1, "Enter") == -1 && dialogrowfrom(y + 1, "Esc") == -1);
	assert(y + 2 == box->y + box->h);
}

static void
testdialog(int fd)
{
	static Color colors[260];
	static XftFont face;
	int cols, rows, y;
	size_t e;
	char lines[512 + 30 * 8 + 1];
	struct timespec now, later;
	Rect *bar;
	XEvent click = {0};
	size_t limit;
	int x, asked, i, n;
	char *primary;

	mincontrast = 0;
	emojifont = NULL;
	dc.col = colors;
	dc.font.match = &face;
	dc.font.ascent = 12;
	resizewindow(80, 20);
	testmouse_put("\033[?2004l");

	/* Every example: layout, colors and moving the selection. */
	for (e = 0; e < LEN(examples); e++) {
		transfer(examples[e].text.s, examples[e].text.n);
		drawdialog();
		expectdialog(e);
		press(XK_Down, 0);
		drawdialog();
		expectdialog(e);
		assert(paste.selected == 1);
		press(XK_Up, 0);
		press(XK_Up, 0);
		drawdialog();
		expectdialog(e);
		assert(paste.selected == -1);
		press(XK_Up, 0);
		press(XK_KP_Up, 0);
		assert(paste.selected == -1);
		press(XK_End, 0);
		drawdialog();
		expectdialog(e);
		press(XK_Down, 0);
		press(XK_KP_Down, 0);
		assert(paste.selected == paste.noptions - 1);
		press(XK_Escape, 0);
		expect(fd, NULL);
	}

	/* k asks in a box like the dialog's over it, which is darkened: a
	 * title bar, then No and Yes side by side and centered without the
	 * marker, No selected.
	 * The dialog's selection moves to the original. */
	transfer(examples[4].text.s, examples[4].text.n);
	press(XK_k, 0);
	drawdialog();
	assert(nrects == 6 && rects[3].y == 8 && rects[3].h == 4);
	assert(rects[3].x == 20 && rects[3].w == 39 && rects[3].pixel == pixel(PASTE_RED));
	assert(rects[4].y == 8 && rects[4].h == 1 && rects[4].pixel == pixel(PASTE_DARKRED));
	/* The title is centered: columns 24 to 54 of the inner 22 to 56. */
	assert(dialogrow("Confirm pasting data unmodified") == 8 && painted[8][24] == 'C');
	assert(painted[8][54] == 'd' && !painted[8][23] && !painted[8][55]);
	assert(24 - (rects[3].x + 2) == (rects[3].x + rects[3].w - 3) - 54);
	assert(painted[10][30] == 'N' && painted[10][34] == 'N' && painted[10][35] == 'o');
	assert(painted[10][41] == 'Y' && painted[10][45] == 'Y' && painted[10][47] == 's');
	for (cols = 0; cols < 80; cols++)
		assert(painted[10][cols] != 0x25b8); /* no marker */
	assert(rects[5].y == 10 && rects[5].x == 33 && rects[5].w == 4);
	assert(ink[10][34] == pixel(PASTE_RED) && ink[10][45] == pixel(PASTE_GOLD));
	for (y = cols = 0; cols < nblends; cols++)
		y += blends[cols].alpha == 0x4000 && blends[cols].red == 0x4000 &&
		     (blends[cols].x == 29 * win.cw || blends[cols].x == 40 * win.cw) &&
		     blends[cols].y == 10 * win.ch - 1 && blends[cols].h == win.ch;
	assert(y == 2);
	assert(dialogrow("Paste contains dangerous characters") >= 0);
	assert(dialogrow("Keep DANGER, keep CRLF, keep TAB") == rects[2].y);
	/* Behind the question, everything is darkened rather than tinted. */
	for (rows = cols = 0; cols < nblends; cols++)
		rows += blends[cols].w == win.w && blends[cols].h == win.h;
	assert(rows == 2);
	for (rows = cols = 0; cols < nblends; cols++) {
		if (blends[cols].w != win.w || blends[cols].h != win.h || !rows++)
			continue;
		assert(blends[cols].alpha == 0xa000 && !blends[cols].red &&
		       !blends[cols].green && !blends[cols].blue);
		assert(blends[cols].order < rects[3].order);
	}
	press(XK_Right, 0);
	drawdialog();
	assert(rects[5].y == 10 && rects[5].x == 44 && rects[5].w == 5);
	assert(ink[10][45] == pixel(PASTE_RED) && ink[10][34] == pixel(PASTE_GOLD));
	press(XK_KP_Left, 0);
	assert(!paste.askyes);
	for (rows = 1; rows <= 20; rows++) {
		for (cols = 1; cols <= 80; cols++) {
			resizewindow(cols, rows);
			drawdialog();
		}
	}
	resizewindow(80, 20);
	answer(XK_Escape);
	press(XK_Escape, 0);
	expect(fd, NULL);

	/* In the C locale, where wcwidth() knows no non-ASCII character,
	 * labels and the marker are still drawn in full. */
	assert(setlocale(LC_CTYPE, "C"));
	transfer("a\tb\n", 4);
	drawdialog();
	assert(dialogrow("▸  S   LF|TAB → SPACE, trim surrounding space") >= 0);
	assert(dialogrow("   P   Keep LF, TAB → 4x SPACE") >= 0);
	assert(dialogrow("   T   Keep LF, keep TAB") >= 0);
	press(XK_Escape, 0);
	assert(setlocale(LC_CTYPE, "C.UTF-8"));

	/* Scrolling up finds the rows wrapping each line from its start would,
	 * for every row and several widths. */
	{
		static char text[4096], *t;
		int widths[] = { 1, 2, 3, 7, 40, 74 }, w, k;
		size_t top, row, next, naive;

		t = text;
		t += sprintf(t, "short\n");
		memset(t, 'x', 2000);
		t += 2000;
		for (k = 0; k < 40; k++)
			t += sprintf(t, "a\tb\xe2\x82\xac\x01");
		t += sprintf(t, "\n\n\xe2\x80\xae\ntail");
		transfer(text, t - text);
		for (w = 0; w < (int)LEN(widths); w++) {
			for (top = 0; top < paste.len; top = pasterow(top, widths[w], NULL)) {
				/* The row before, wrapping its line from the start. */
				for (row = top ? top - 1 : 0; row > 0 && text[row - 1] != '\n'; row--)
					;
				for (naive = row; top && (next = pasterow(naive, widths[w], NULL)) < top;)
					naive = next;
				assert(pasterowbefore(top, widths[w]) == naive);
			}
			assert(pasterowbefore(paste.len, widths[w]) <= paste.len);
		}
		press(XK_Escape, 0);
	}

	/* A long line is wrapped once, not again at every step up it. */
	{
		static char line[1 << 20];
		struct timespec start, end;

		memset(line, 'y', sizeof(line) - 1);
		line[sizeof(line) - 1] = '\n';
		transfer(line, sizeof(line));
		press(XK_Up, 0);
		press(XK_Return, 0);
		assert(paste.viewing);
		drawdialog();
		press(XK_End, 0);
		clock_gettime(CLOCK_MONOTONIC, &start);
		for (y = 0; y < 2000; y++)
			press(XK_Up, 0);
		clock_gettime(CLOCK_MONOTONIC, &end);
		assert(TIMEDIFF(end, start) < 2000);
		/* 74 columns: the last row starts at 14169 * 74; End shows the
		 * page of 13 rows ending with it, then 2000 rows up. */
		assert(paste.viewcols == 74 && paste.viewrows == 14);
		assert(paste.viewtop == (size_t)(14169 - 13 - 2000) * 74);
		press(XK_Escape, 0);
		press(XK_Escape, 0);
		expect(fd, NULL);
	}

	/* The dialog is modal for the mouse, too: clicks do not reach the
	 * terminal while it is open, not even on release, which would claim
	 * PRIMARY again for a selection still shown. A drag begun before the
	 * dialog still ends. */
	testmouse_put("\033[H\033[2Jselected text");
	click.xbutton.button = Button1;
	click.xbutton.type = ButtonPress;
	bpress(&click);
	click.xbutton.x = 7 * win.cw;
	click.xbutton.type = MotionNotify;
	bmotion(&click);
	click.xbutton.type = ButtonRelease;
	owned = 0;
	brelease(&click);
	assert(owned == 1 && xsel.primary && !strcmp(xsel.primary, "selected"));
	transfer(examples[4].text.s, examples[4].text.n);
	xsel.clicks = 0;
	owned = 0;
	click.xbutton.x = 0;
	click.xbutton.type = ButtonPress;
	bpress(&click);
	assert(xsel.clicks == 0);
	click.xbutton.x = 3 * win.cw;
	click.xbutton.type = MotionNotify;
	bmotion(&click);
	click.xbutton.type = ButtonRelease;
	brelease(&click);
	assert(owned == 0 && !strcmp(xsel.primary, "selected"));
	/* Also when the dialog closed before the button was released. */
	click.xbutton.type = ButtonPress;
	bpress(&click);
	press(XK_Escape, 0);
	click.xbutton.type = ButtonRelease;
	brelease(&click);
	assert(owned == 0);
	/* A drag begun before the dialog ends normally. */
	click.xbutton.type = ButtonPress;
	bpress(&click);
	click.xbutton.x = 5 * win.cw;
	click.xbutton.type = MotionNotify;
	bmotion(&click);
	transfer(examples[4].text.s, examples[4].text.n);
	click.xbutton.x = 7 * win.cw;
	bmotion(&click);
	click.xbutton.type = ButtonRelease;
	brelease(&click);
	assert(owned == 1 && !strcmp(xsel.primary, "ected")); /* columns 3 to 7 */
	press(XK_Escape, 0);
	click.xbutton.type = ButtonPress;
	bpress(&click);
	assert(xsel.clicks >= 1);
	click.xbutton.type = ButtonRelease;
	brelease(&click);
	expect(fd, NULL);

	/* The wheel still scrolls history beneath the dialog and the failure
	 * box, but neither its press nor its release runs a binding there. */
	{
		uint button = mshortcuts[0].button;
		int i, calls = mousecalls;

		for (i = 0; i < 40; i++)
			testmouse_put("\r\nold line");
		assert(mshortcuts[0].func == selpaste && mshortcuts[0].release);
		mshortcuts[0].func = othermouse;
		mshortcuts[0].button = Button4;
		transfer(examples[4].text.s, examples[4].text.n);
		assert(testmouse_offset() == 0);
		wheel(Button4);
		assert(testmouse_offset() == mousescrolllines && mousecalls == calls);
		wheel(Button5);
		assert(testmouse_offset() == 0 && paste.state == PASTE_CONFIRM);
		press(XK_Escape, 0);
		pastefailed("The selection owner sent invalid data");
		wheel(Button4);
		assert(testmouse_offset() == mousescrolllines && mousecalls == calls);
		assert(pastefail.text[0]);
		pastefailclear();
		wheel(Button5);
		wheel(Button4); /* with neither box, its release runs the binding */
		wheel(Button5);
		assert(testmouse_offset() == 0 && mousecalls == calls + 1);
		mshortcuts[0].func = selpaste;
		mshortcuts[0].button = button;
		expect(fd, NULL);
	}

	/* A failed paste is explained in a red box: a title bar and the
	 * reason, centered, until Esc, Ctrl+C or Enter after the delay, or for
	 * five seconds. It takes all other keys and clicks meanwhile, except
	 * that pasting retries, replacing it. */
	limit = pastemaxbytes;
	pastemaxbytes = 32 * 1024 * 1024;
	selpaste(NULL);
	supply((Property){ .type = 43, .format = 32 }, 0);
	{
		Property p = textproperty("x", 1);

		p.remaining = 33 * 1024 * 1024; /* an INCR chunk past the limit */
		supply(p, 1);
	}
	assert(paste.state == PASTE_IDLE);
	assert(!strcmp(pastefail.text, "The paste is larger than 33554432 bytes"));
	drain();
	drawdialog();
	expectframe(PASTE_RED);
	assert(rects[0].y == 8 && rects[0].h == 4 && rects[0].pixel == pixel(PASTE_RED));
	assert(rects[1].y == 8 && rects[1].h == 1 && rects[1].pixel == pixel(PASTE_DARKRED));
	assert(dialogrow("Paste failed") == 8);
	assert(dialogrow("The paste is larger than 33554432 bytes") == 10);
	/* Both lines are centered. */
	for (x = 0; !painted[10][x]; x++)
		;
	assert(x - (rects[0].x + 2) == (rects[0].x + rects[0].w - 3) - (x + 38)); /* its last of 39 columns */
	assert(ink[10][x] == pixel(PASTE_GOLD));
	/* Keys within the delay, other keys and clicks do nothing. */
	asked = requests;
	pressnow(XK_Escape, 0);
	pressnow(XK_Return, 0);
	pressnow(XK_c, ControlMask);
	press(XK_a, 0);
	press(XK_v, ControlMask);
	click.xbutton.type = ButtonPress;
	click.xbutton.button = Button1;
	bpress(&click);
	click.xbutton.type = ButtonRelease;
	brelease(&click);
	assert(pastefail.text[0] && requests == asked);
	expect(fd, NULL);
	testmouse_cleardirty();
	press(XK_Escape, 0);
	assert(!pastefail.text[0] && !testmouse_alldirty());
	/* Opening either box does not dirty the terminal either. */
	testmouse_cleardirty();
	pastefailed("The selection owner sent invalid data");
	assert(pastefail.text[0] && !testmouse_alldirty());
	pastefailclear();
	assert(paste.state == PASTE_IDLE);
	transfer("a\nb", 3);
	assert(paste.state == PASTE_CONFIRM && !testmouse_alldirty());
	pasteclear();
	asked = requests;
	/* Pasting by Ctrl+Shift+V, Shift+Insert or a middle-click retries,
	 * at once, replacing the box. */
	pastefailed("The selection owner sent invalid data");
	pressnow(XK_V, ControlMask | ShiftMask);
	assert(!pastefail.text[0] && paste.state == PASTE_WAIT && requests == asked + 1);
	pasteclear();
	pastefailed("The selection owner sent invalid data");
	pressnow(XK_Insert, ShiftMask);
	assert(!pastefail.text[0] && paste.state == PASTE_WAIT && requests == asked + 2);
	pasteclear();
	pastefailed("The selection owner sent invalid data");
	click.xbutton.type = ButtonPress;
	click.xbutton.button = Button2;
	bpress(&click);
	assert(pastefail.text[0]);
	click.xbutton.type = ButtonRelease;
	brelease(&click);
	assert(!pastefail.text[0] && paste.state == PASTE_WAIT && requests == asked + 3);
	pasteclear();
	expect(fd, NULL);
	/* A paste binding needs its modifiers to retry: with Ctrl+Button1
	 * bound, plain left clicks and drags still do nothing, and do not claim
	 * PRIMARY for a selection shown. A retrying click does nothing else,
	 * not even when Ctrl is let go before the release. */
	{
		uint savedmod = mshortcuts[0].mod, savedbutton = mshortcuts[0].button;

		assert(mshortcuts[0].func == selpaste && mshortcuts[0].release);

		testmouse_put("\033[H\033[2Jselected text");
		click.xbutton.button = Button1;
		click.xbutton.state = 0;
		click.xbutton.x = 0;
		click.xbutton.type = ButtonPress;
		bpress(&click);
		click.xbutton.x = 7 * win.cw;
		click.xbutton.type = MotionNotify;
		bmotion(&click);
		click.xbutton.type = ButtonRelease;
		brelease(&click);
		assert(!strcmp(xsel.primary, "selected"));
		mshortcuts[0].mod = ControlMask;
		mshortcuts[0].button = Button1;
		pastefailed("The selection owner sent invalid data");
		owned = 0;
		asked = requests;
		click.xbutton.x = 2 * win.cw;
		click.xbutton.type = ButtonPress;
		bpress(&click);
		click.xbutton.x = 5 * win.cw;
		click.xbutton.type = MotionNotify;
		bmotion(&click);
		click.xbutton.type = ButtonRelease;
		brelease(&click);
		assert(pastefail.text[0] && requests == asked && owned == 0);
		assert(!strcmp(xsel.primary, "selected"));
		click.xbutton.state = ControlMask;
		click.xbutton.type = ButtonPress;
		bpress(&click);
		click.xbutton.state = 0; /* Ctrl let go before the release */
		click.xbutton.type = ButtonRelease;
		brelease(&click);
		assert(pastefail.text[0] && requests == asked && owned == 0);
		click.xbutton.state = ControlMask;
		click.xbutton.type = ButtonPress;
		bpress(&click);
		click.xbutton.x = 9 * win.cw;
		click.xbutton.type = MotionNotify;
		bmotion(&click);
		click.xbutton.type = ButtonRelease;
		brelease(&click);
		assert(!pastefail.text[0] && paste.state == PASTE_WAIT);
		assert(requests == asked + 1 && owned == 0);
		assert(!strcmp(xsel.primary, "selected"));
		pasteclear();
		/* Only paste bindings run for a retrying click: with plain
		 * Button1 bound to something else, a retry released without
		 * Ctrl runs nothing. */
		pastefailed("The selection owner sent invalid data");
		mshortcuts[0].mod = 0;
		mshortcuts[0].func = othermouse;
		mousecalls = 0;
		mousepasting |= Button1Mask; /* as after a Ctrl+click under the box */
		click.xbutton.state = 0;
		click.xbutton.type = ButtonRelease;
		brelease(&click);
		assert(mousecalls == 0 && !mousepasting && pastefail.text[0]);
		mshortcuts[0].func = selpaste;
		press(XK_Escape, 0);
		mshortcuts[0].mod = savedmod;
		mshortcuts[0].button = savedbutton;
		expect(fd, NULL);
	}
	pastemaxbytes = 1000;
	pastefailedsize();
	assert(!strcmp(pastefail.text, "The paste is larger than 1000 bytes"));
	press(XK_Return, 0);
	assert(!pastefail.text[0]);
	pastefailedsize();
	press(XK_c, ControlMask);
	assert(!pastefail.text[0]);
	pastemaxbytes = limit;
	/* Invalid data and a silent owner have their own reasons. */
	selpaste(NULL);
	{
		Property p = textproperty("bad", 3);

		p.format = 16;
		supply(p, 0);
	}
	assert(!strcmp(pastefail.text, "The selection owner sent invalid data"));
	selpaste(NULL); /* a new paste replaces the box */
	assert(!pastefail.text[0] && paste.state == PASTE_WAIT);
	later = paste.progress;
	later.tv_sec += 60;
	assert(pasteremaining(later) == -1 && paste.state == PASTE_IDLE);
	assert(!strcmp(pastefail.text, "The selection owner stopped answering"));
	/* It closes by itself after five seconds. */
	later = pastefail.shown;
	later.tv_sec += 4;
	assert(pasteremaining(later) == 1000 && pastefail.text[0]);
	later.tv_sec += 2;
	testmouse_cleardirty();
	assert(pasteremaining(later) == -1 && !pastefail.text[0]);
	assert(!testmouse_alldirty());
	expect(fd, NULL);

	/* Nothing in the dialog blinks. */
	transfer(examples[4].text.s, examples[4].text.n);
	clock_gettime(CLOCK_MONOTONIC, &now);
	assert(blinkremaining(now) == -1);
	press(XK_Escape, 0);

	/* Terminal output cannot erase the dialog; drawing sends nothing. */
	transfer(examples[4].text.s, examples[4].text.n);
	testmouse_put("\033[Hbackground output\033[?5h");
	memset(painted, 0, sizeof(painted));
	memset(ink, 0, sizeof(ink));
	nrects = nblends = 0;
	presenting = 1;
	framepixels[xw.buf] = 0x204060;
	xfinishdraw();
	assert(xw.draw == (Draw)xw.buf);
	assert(copies == 2 && (win.mode & MODE_REVERSE));
	expectdialog(4);
	memset(painted, 0, sizeof(painted));
	glyphs = 0;
	nrects = nblends = 0;
	xfinishdraw();
	assert(copies == 4 && xw.draw == (Draw)xw.buf);
	expectdialog(4);
	presenting = 0;
	expect(fd, NULL);
	press(XK_Tab, 0);
	assert(paste.selected == 1);
	press(XK_ISO_Left_Tab, ShiftMask);
	press(XK_Tab, ShiftMask);
	assert(paste.selected == -1);
	press(XK_Home, 0);
	assert(paste.selected == 0);
	press(XK_Down, ControlMask);
	press(XK_Down, Mod1Mask);
	assert(paste.selected == 0 && paste.state == PASTE_CONFIRM);

	/* Presentation starts from the untinted terminal buffer each frame,
	 * without clearing it or forcing dirty lines, even after closing. */
	win.mode |= MODE_VISIBLE;
	presenting = 1;
	for (y = 0; y < 3; y++) {
		unsigned long first;

		testmouse_cleardirty();
		nrects = nblends = 0;
		assert(xstartdraw() && !testmouse_alldirty() && !nrects);
		if (y == 2)
			framepixels[xw.buf] = 0x80a0c0; /* background output */
		first = framepixels[xw.win];
		xfinishdraw();
		assert(xw.draw == (Draw)xw.buf);
		assert(framepixels[xw.buf] == (y == 2 ? 0x80a0c0 : 0x204060));
		if (y == 1)
			assert(framepixels[xw.win] == first); /* tint never accumulates */
		if (y == 2)
			assert(framepixels[xw.win] != first); /* output remains live */
	}
	testmouse_cleardirty();
	press(XK_Escape, 0);
	nrects = nblends = 0;
	assert(xstartdraw() && !testmouse_alldirty() && !nrects);
	xfinishdraw();
	assert(framepixels[xw.win] == framepixels[xw.buf]);

	/* Failure boxes use the same clean presentation path, including when
	 * the visual bell is active or disabled by its width/opacity. */
	pastefailed("test failure");
	for (y = 0; y < 3; y++) {
		vbellset = y == 0;
		vbellwidth = y == 1 ? 0 : 32;
		vbellopacity = y == 2 ? 0 : 0.5;
		clock_gettime(CLOCK_MONOTONIC, &lastvbell);
		nrects = nblends = 0;
		testmouse_cleardirty();
		assert(xstartdraw() && !testmouse_alldirty());
		xfinishdraw();
		assert(nblends > 0 && framepixels[xw.buf] == 0x80a0c0);
	}
	assert(bellfills == 1);
	vbellset = 0;
	testmouse_cleardirty();
	pastefailclear();
	assert(!testmouse_alldirty());
	nrects = nblends = 0;
	xfinishdraw();
	assert(framepixels[xw.win] == framepixels[xw.buf]);
	presenting = 0;
	win.mode &= ~MODE_VISIBLE;

	/* Enter or a choice key right after the dialog appears may have been
	 * meant for the shell, and so may y or Enter on Yes right after the
	 * question appears. */
	transfer(examples[4].text.s, examples[4].text.n);
	pressnow(XK_Return, 0);
	pressnow(XK_KP_Enter, 0);
	pressnow(XK_s, 0);
	pressnow(XK_p, 0);
	pressnow(XK_t, 0);
	pressnow(XK_k, 0);
	assert(paste.state == PASTE_CONFIRM && !paste.viewing && !paste.asking);
	expect(fd, NULL);
	press(XK_k, 0);
	assert(paste.asking);
	pressnow(XK_y, 0);
	pressnow(XK_Right, 0);
	pressnow(XK_Return, 0);
	assert(paste.asking && paste.askyes && paste.state == PASTE_CONFIRM);
	expect(fd, NULL);
	pressnow(XK_n, 0); /* going back is never too early */
	assert(!paste.asking && paste.state == PASTE_CONFIRM);
	press(XK_Home, 0);
	paste.shown.tv_sec -= 1;
	press(XK_Return, ControlMask);
	assert(paste.state == PASTE_CONFIRM);
	testmouse_cleardirty();
	press(XK_Return, 0);
	assert(!testmouse_alldirty() && paste.state == PASTE_IDLE);
	expect(fd, "te st example");

	/* Enter on the preview shows the whole paste, wrapped and split at
	 * newlines; Enter, Escape or q return to the choices. */
	memset(lines, 'x', 200);
	lines[200] = '\n';
	for (y = 0; y < 30; y++)
		sprintf(lines + 201 + 8 * y, "line %02d\n", y);
	transfer(lines, strlen(lines));
	press(XK_Up, 0);
	paste.shown.tv_sec -= 1;
	press(XK_Return, 0);
	assert(paste.viewing && paste.state == PASTE_CONFIRM);
	drawdialog();
	expectframe(PASTE_BLUE);
	assert(paste.viewcols == 74 && paste.viewrows == 14);
	assert(dialogrow("Paste contains multiple lines") == 1 && dialogrow("bytes") == -1);
	assert(painted[3][3] == 'x' && painted[3][76] == 'x' && !painted[3][77]);
	assert(painted[5][54] == 'x' && painted[5][55] == '\\' && painted[5][56] == 'n');
	assert(dialogrow("line 00\\n") == 6 && dialogrow("line 10\\n") == 16);
	assert(dialogrow("line 11") < 0);
	/* Below the paste, the position is flush right, with no help text. */
	assert(painted[17][76] == '%' && !painted[17][3] && dialogrow("scroll") == -1);
	press(XK_Up, 0);
	assert(paste.viewtop == 0);
	press(XK_Down, 0);
	press(XK_KP_Down, 0);
	press(XK_Down, 0);
	assert(paste.viewtop == 201);
	press(XK_Up, 0);
	assert(paste.viewtop == 148);
	press(XK_Up, 0);
	assert(paste.viewtop == 74);
	press(XK_Next, 0); /* one row less than a page */
	drawdialog();
	assert(dialogrow("line 11\\n") == 3);
	press(XK_End, 0);
	drawdialog();
	assert(dialogrow("line 16\\n") == 3 && dialogrow("line 29\\n") == 16);
	assert(dialogrow("100%") == 17);
	press(XK_Down, 0);
	press(XK_space, 0);
	drawdialog();
	assert(dialogrow("line 29\\n") == 3);
	press(XK_Prior, 0);
	drawdialog();
	assert(dialogrow("line 16\\n") == 3);
	press(XK_Home, 0);
	assert(paste.viewtop == 0);
	/* The wheel scrolls the full view, not history, by mousescrolllines. */
	{
		size_t top;
		int i, offset = testmouse_offset();

		for (i = 0; i < mousescrolllines; i++)
			press(XK_Down, 0);
		top = paste.viewtop;
		press(XK_Home, 0);
		wheel(Button5);
		assert(paste.viewtop == top && top > 0);
		wheel(Button4);
		assert(paste.viewtop == 0 && testmouse_offset() == offset);
	}
	/* Button1 selects the text as shown, whole escaped characters, from
	 * either end; releasing it makes the selection PRIMARY, escaped but
	 * for its newlines. */
	assert(paste.viewx == 3 && paste.viewy == 3);
	owned = 0;
	drag(3, 6, 6, 7);
	assert(owned == 1 && !strcmp(xsel.primary, "line 00\nline"));
	drawdialog();
	assert(painted[6][3] == 'l' && painted[7][6] == 'e' && painted[7][7] == ' ');
	assert(ink[6][3] == ink[7][6] && ink[7][6] != ink[7][7]);
	assert(ink[7][7] == ink[8][3]);
	drag(6, 7, 3, 6);
	assert(owned == 2 && !strcmp(xsel.primary, "line 00\nline"));
	drag(55, 5, 4, 6);
	assert(!strcmp(xsel.primary, "\nli"));
	/* Beyond a row's text, up to its end; left of it, from its start. */
	drag(5, 6, 40, 6);
	assert(!strcmp(xsel.primary, "ne 00\n"));
	drag(0, 7, 4, 7);
	assert(!strcmp(xsel.primary, "li"));
	/* A drag within the character pressed on selects it, as in the
	 * terminal, escaped ones too. */
	drag(3, 6, 3, 6);
	assert(owned == 6 && !strcmp(xsel.primary, "l"));
	drag(56, 5, 55, 5);
	assert(owned == 7 && !strcmp(xsel.primary, "\n"));
	/* A click without a drag selects nothing and leaves PRIMARY. */
	clickat(3, 6);
	assert(owned == 7 && !paste.marked && !strcmp(xsel.primary, "\n"));
	drawdialog();
	assert(ink[6][3] == ink[8][3]);
	/* Dragging below or above the text scrolls by a row each motion, the
	 * selection following. */
	mouseat(ButtonPress, Button1, 3, 16);
	mouseat(MotionNotify, 0, 3, 18);
	assert(paste.viewtop == 74);
	mouseat(MotionNotify, 0, 3, 18);
	assert(paste.viewtop == 148);
	mouseat(ButtonRelease, Button1, 3, 18);
	assert(!strcmp(xsel.primary, "line 10\nline 11\nl"));
	mouseat(ButtonPress, Button1, 3, 3);
	mouseat(MotionNotify, 0, 3, 0);
	assert(paste.viewtop == 74);
	mouseat(ButtonRelease, Button1, 3, 0);
	assert(strlen(xsel.primary) == 75 && strspn(xsel.primary, "x") == 75);
	press(XK_Home, 0);
	/* Other buttons do nothing in the view, and closing it ends a drag
	 * without claiming PRIMARY. */
	asked = requests;
	owned = 0;
	mouseat(ButtonPress, Button2, 3, 6);
	mouseat(ButtonRelease, Button2, 3, 6);
	mouseat(ButtonPress, Button3, 3, 6);
	mouseat(ButtonRelease, Button3, 3, 6);
	assert(requests == asked && owned == 0);
	mouseat(ButtonPress, Button1, 3, 6);
	mouseat(MotionNotify, 0, 8, 6);
	press(XK_q, 0);
	mouseat(ButtonRelease, Button1, 8, 6);
	assert(owned == 0 && !paste.marked);
	press(XK_Return, 0);
	assert(paste.viewing && !paste.marked);
	drawdialog();
	assert(ink[6][3] == ink[8][3]);
	/* c copies the selection, or else all of the text, to CLIPBOARD, and
	 * says so until the next key. Below the text, it is on a raised key
	 * cap like the dialog's, with what it copies. */
	assert(dialogrow("Copy all") == 17 && painted[17][4] == 'C' && painted[17][8] == 'C');
	for (i = n = 0; i < nblends; i++)
		n += blends[i].alpha == 0x4000 && blends[i].red == 0x4000 &&
		     blends[i].x == 3 * win.cw && blends[i].w == 3 * win.cw &&
		     blends[i].y == 17 * win.ch + 1;
	assert(n == 1);
	press(XK_c, 0);
	assert(owned == 1 && paste.copied && xsel.clipboard);
	assert(!strncmp(xsel.clipboard, "xxx", 3) && strlen(xsel.clipboard) == 200 + 1 + 30 * 8);
	assert(!strncmp(xsel.clipboard + 200, "\nline 00\nline 01\n", 17));
	drawdialog();
	assert(dialogrow("Copied to clipboard") == 17 && painted[17][8] == 'C');
	assert(dialogrow("Copy all") == -1);
	drag(3, 6, 6, 6);
	drawdialog();
	assert(dialogrow("Copy selection") == 17);
	press(XK_C, ShiftMask);
	assert(owned == 3 && !strcmp(xsel.clipboard, "line"));
	press(XK_Down, 0);
	press(XK_Up, 0);
	drawdialog();
	assert(dialogrow("Copied to clipboard") == -1 && paste.marked);
	press(XK_c, Mod1Mask); /* not with Alt; Ctrl+C cancels */
	assert(owned == 3 && paste.viewing);
	/* Ctrl+Shift+C, phist's copy binding, copies as c does. */
	drag(3, 7, 9, 7);
	assert(owned == 4 && !strcmp(xsel.primary, "line 01"));
	press(XK_C, ControlMask | ShiftMask);
	assert(owned == 5 && paste.viewing && paste.copied > 0);
	assert(!strcmp(xsel.clipboard, "line 01"));
	/* The release position counts, though no motion reported it. */
	mouseat(ButtonPress, Button1, 3, 6);
	mouseat(MotionNotify, 0, 4, 6);
	mouseat(ButtonRelease, Button1, 9, 6);
	assert(owned == 6 && !strcmp(xsel.primary, "line 00"));
	mouseat(ButtonPress, Button1, 3, 7);
	mouseat(ButtonRelease, Button1, 6, 7);
	assert(owned == 7 && !strcmp(xsel.primary, "line"));
	/* What one request cannot carry, as phist serves a selection in one, is
	 * not copied, with BIG-REQUESTS or without: 28 bytes of a request go
	 * to its header and a big request's length, here 36 are left. */
	for (i = 0; i < 2; i++) {
		if (i)
			bigrequests = 16;
		else
			bigrequests = 0, requestsize = 16;
		clickat(3, 6); /* nothing selected: c copies all */
		press(XK_c, 0);
		assert(owned == 7 && paste.copied < 0);
		assert(!strcmp(xsel.clipboard, "line 01"));
		drawdialog();
		assert(dialogrow("Too large to copy") == 17);
		primary = xsel.primary;
		drag(3, 3, 3, 4); /* 75 bytes */
		assert(owned == 7 && paste.copied < 0 && xsel.primary == primary);
		drag(3, 6, 9, 7);
		assert(owned == 8 && !strcmp(xsel.primary, "line 00\nline 01"));
		/* Exactly at the limit: 10 units take 12 bytes, not 13. */
		*(i ? &bigrequests : &requestsize) = 10;
		drag(3, 6, 6, 7);
		assert(owned == 9 && !strcmp(xsel.primary, "line 00\nline"));
		drag(3, 6, 7, 7);
		assert(owned == 9 && paste.copied < 0);
		assert(!strcmp(xsel.primary, "line 00\nline"));
		owned = 7;
	}
	bigrequests = 4194303, requestsize = 65535;
	/* A label that does not fit is left out rather than cut short. */
	resizewindow(18, 20);
	drawdialog();
	assert(painted[17][4] == 'C' && !painted[17][8] && dialogrow("Copy") == -1);
	/* Dragging past the window's edges scrolls, even when the text fills
	 * it and there is no row above or below it inside the window. */
	resizewindow(80, 2);
	press(XK_Home, 0);
	drawdialog();
	assert(paste.viewy == 0 && paste.viewrows == 2);
	mouseat(ButtonPress, Button1, 3, 1);
	mouseat(MotionNotify, 0, 3, 2);
	assert(paste.viewtop == 74);
	mouseat(MotionNotify, 0, 3, -1);
	assert(paste.viewtop == 0);
	mouseat(ButtonRelease, Button1, 3, -1);
	resizewindow(80, 20);
	drawdialog();
	press(XK_Escape, 0);
	press(XK_Return, 0);
	/* Ctrl or Alt combinations do nothing in the full view either. */
	press(XK_Down, ControlMask);
	press(XK_Next, Mod1Mask);
	press(XK_q, ControlMask);
	press(XK_v, Mod1Mask);
	press(XK_Return, ControlMask);
	assert(paste.viewing && paste.viewtop == 0);
	press(XK_s, 0); /* choices do not apply in the full view */
	press(XK_K, ShiftMask);
	assert(paste.viewing && paste.state == PASTE_CONFIRM && !paste.asking);
	expect(fd, NULL);
	press(XK_Escape, 0);
	assert(!paste.viewing && paste.state == PASTE_CONFIRM && paste.selected == -1);
	press(XK_Return, 0);
	assert(paste.viewing);
	press(XK_q, 0);
	assert(!paste.viewing);
	/* v opens it from any choice without the Enter delay and closes it. */
	press(XK_Home, 0);
	clock_gettime(CLOCK_MONOTONIC, &paste.shown);
	pressnow(XK_v, 0);
	assert(paste.viewing && paste.viewtop == 0 && paste.selected == -1);
	press(XK_v, 0);
	assert(!paste.viewing && paste.state == PASTE_CONFIRM);
	press(XK_KP_Enter, 0);
	press(XK_Return, 0);
	assert(!paste.viewing);
	press(XK_Return, 0);
	/* Ctrl+C cancels the paste from the full view, too. */
	press(XK_c, ControlMask);
	assert(paste.state == PASTE_IDLE);
	expect(fd, NULL);
	/* Copies keep newlines as they are, and every other escape, a
	 * backslash's included, so they cannot be read differently. */
	transfer("a\tb\\n\r\n\033", 8);
	press(XK_v, 0);
	press(XK_c, 0);
	assert(!strcmp(xsel.clipboard, "a\\tb\\\\n\\r\n\\x1b"));
	press(XK_Escape, 0);
	press(XK_Escape, 0);
	assert(paste.state == PASTE_IDLE);

	/* Every size draws inside the window and keeps the selection visible. */
	transfer(examples[4].text.s, examples[4].text.n);
	for (rows = 1; rows <= 20; rows++) {
		for (cols = 1; cols <= 80; cols++) {
			resizewindow(cols, rows);
			/* The title bar comes and goes. The selected row has the
			 * marker, and its label is inverted if there is room for it. */
			press(XK_Home, 0);
			drawdialog();
			assert(nrects >= 1 && nrects <= 3);
			assert(cols < 3 || dialogrow("▸") >= 0);
			assert(nrects < 3 || (rects[1].y == rects[0].y && rects[1].h == 1));
			press(XK_End, 0);
			drawdialog();
			bar = &rects[nrects - 1];
			y = dialogrow("▸");
			assert(cols < 3 || y >= 0);
			if (nrects > 1 && bar->x != rects[0].x)
				assert(bar->y == y && bar->h == 1 && bar->order > rects[0].order);
			if (cols >= 40)
				assert(dialogrow("▸  K   Keep DANGER") == y && bar->y == y);
			press(XK_Up, 0);
			press(XK_Up, 0);
			press(XK_Up, 0);
			press(XK_Up, 0);
			drawdialog();
			paste.viewing = 1;
			drawdialog();
			assert(paste.viewcols >= 1 && paste.viewrows >= 1);
			paste.viewing = 0;
		}
	}
	/* Options scrolled out of view are marked with arrows. */
	resizewindow(80, 2);
	press(XK_Home, 0);
	drawdialog();
	y = rects[0].x + rects[0].w - 3; /* the right edge of the text */
	assert(dialogrow("▸  S   ") == 0 && dialogrow("   P   ") == 1);
	assert(painted[1][y] == 0x25bc && painted[0][y] != 0x25b2);
	press(XK_End, 0);
	drawdialog();
	y = rects[0].x + rects[0].w - 3;
	assert(dialogrow("   T   ") == 0 && dialogrow("▸  K   ") == 1);
	assert(painted[0][y] == 0x25b2 && painted[1][y] != 0x25bc);
	resizewindow(80, 20);
	press(XK_Escape, 0);
	expect(fd, NULL);
}

/* Present a frame and record only the dialog drawn by that frame. */
static void
presentframe(void)
{
	memset(painted, 0, sizeof(painted));
	memset(ink, 0, sizeof(ink));
	glyphs = nrects = nblends = 0;
	xfinishdraw();
}

static void
testpresentationfallback(int fd)
{
	int fault, repeat, before, changes;
	unsigned long first;
	struct timespec now;

	framepixels[xw.buf] = 0x204060;
	for (fault = 0; fault < 3; fault++) {
		vbellfree();
		failpixmap = fault == 0;
		faildraw = fault == 1;
		failpicture = fault == 2;
		transfer(examples[4].text.s, examples[4].text.n);
		presenting = 1;
		for (repeat = 0; repeat < 2; repeat++) {
			before = copies;
			changes = drawchanges;
			first = framepixels[xw.win];
			presentframe();
			expectdialog(4);
			assert(paste.state == PASTE_CONFIRM && !vbell.buf && !vbell.draw);
			assert(copies == before + 1 && drawchanges == changes + 2);
			assert(maindrawable == xw.buf && framepixels[xw.buf] == 0x204060);
			if (repeat)
				assert(framepixels[xw.win] == first);
		}

		/* Once setup succeeds, go back to compositing without changing
		 * the terminal buffer or its drawing context. */
		failpixmap = faildraw = failpicture = 0;
		before = copies;
		changes = drawchanges;
		presentframe();
		expectdialog(4);
		assert(vbell.buf && vbell.draw && copies == before + 2);
		assert(drawchanges == changes && maindrawable == xw.buf);
		assert(framepixels[xw.buf] == 0x204060);
		testmouse_cleardirty();
		press(XK_Escape, 0);
		assert(!testmouse_alldirty());
		presentframe();
		assert(framepixels[xw.win] == framepixels[xw.buf]);

		/* The failure box also stays visible until it expires. */
		vbellfree();
		failpixmap = fault == 0;
		faildraw = fault == 1;
		failpicture = fault == 2;
		pastefailed("test failure");
		for (repeat = 0; repeat < 2; repeat++) {
			first = framepixels[xw.win];
			presentframe();
			assert(dialogrow("Paste failed") >= 0 && pastemodal());
			assert(!vbell.buf && !vbell.draw && maindrawable == xw.buf);
			assert(framepixels[xw.buf] == 0x204060);
			if (repeat)
				assert(framepixels[xw.win] == first);
		}
		now = pastefail.shown;
		now.tv_sec += 6;
		testmouse_cleardirty();
		assert(pasteremaining(now) < 0 && !pastemodal());
		assert(!testmouse_alldirty());
		presentframe();
		assert(framepixels[xw.win] == framepixels[xw.buf]);
		presenting = 0;
		failpixmap = faildraw = failpicture = 0;
	}
	expect(fd, NULL);
}

/* A preview line that can't be clipped isn't drawn. */
static void
testclipfailure(void)
{
	Color color = {0};
	int before = glyphs;

	failclip = 1;
	pastedrawline("x", 0, 0, 5, &color);
	failclip = 0;
	assert(glyphs == before);
	pastedrawline("x", 0, 0, 5, &color);
	assert(glyphs == before + 1);
	puts("PASS: a preview line that can't be clipped isn't drawn");
}

int
main(void)
{
	int sockets[2];
	Screen screen = { .root_depth = 24 };
	_XPrivDisplay display = calloc(1, sizeof(*display));

	assert(display);
	display->screens = &screen;
	xw.dpy = (Display *)display;
	xw.buf = 2;
	xw.draw = (Draw)2;
	framepixels[2] = 0x204060;
	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	borderpx = 0;
	win.cw = 8;
	win.ch = 16;
	win.tw = 80 * win.cw;
	win.th = 5 * win.ch;
	win.mode = MODE_FOCUSED; /* Keep terminal draws disabled without X. */
	xw.win = 1;
	xsel.xtarget = 44;
	testmouse_init(sockets[0], 80, 5);
	testcontrols(sockets[1]);
	testactions(sockets[1]);
	testtransfers(sockets[1]);
	testrequests(sockets[1]);
	testendlessdrain();
	testclipfailure();
	testdialog(sockets[1]);
	testpresentationfallback(sockets[1]);
	vbellfree();
	free(display);
	close(sockets[0]);
	close(sockets[1]);
	puts("PASS: control confirmation, sanitizing, UTF-8 preview, whole INCR transfers, ignored late replies, fixed paste mode, transfer limits/timeouts, dialog layout, navigation, asking before the original, full view and redraw");
	return 0;
}
