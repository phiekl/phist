/* See LICENSE for license details. */
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <X11/XKBlib.h>

char *argv0;
#include "arg.h"
#include "st.h"
#include "win.h"

/* types used in config.h */
typedef struct {
	uint mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Shortcut;

typedef struct {
	uint mod;
	uint button;
	void (*func)(const Arg *);
	const Arg arg;
	uint  release;
} MouseShortcut;

typedef struct {
	KeySym k;
	uint mask;
	char *s;
	/* three-valued logic variables: 0 indifferent, 1 on, -1 off */
	signed char appkey;    /* application keypad */
	signed char appcursor; /* application cursor */
} Key;

/* Undercurl slope types */
enum undercurl_slope_type {
	UNDERCURL_SLOPE_ASCENDING = 0,
	UNDERCURL_SLOPE_TOP_CAP = 1,
	UNDERCURL_SLOPE_DESCENDING = 2,
	UNDERCURL_SLOPE_BOTTOM_CAP = 3
};

/* X modifiers */
#define XK_ANY_MOD    UINT_MAX
#define XK_NO_MOD     0
#define XK_SWITCH_MOD (1<<13|1<<14)

/* function definitions used in config.h */
static void clipcopy(const Arg *);
static void clippaste(const Arg *);
static void numlock(const Arg *);
static void selpaste(const Arg *);
static void zoom(const Arg *);
static void zoomabs(const Arg *);
static void zoomreset(const Arg *);
static void ttysend(const Arg *);

/* config.h for applying patches and the configuration. */
#include "config.h"

/* XEMBED messages */
#define XEMBED_FOCUS_IN  4
#define XEMBED_FOCUS_OUT 5

/* macros */
#define IS_SET(flag)		((win.mode & (flag)) != 0)
#define TRUERED(x)		(((x) & 0xff0000) >> 8)
#define TRUEGREEN(x)		(((x) & 0xff00))
#define TRUEBLUE(x)		(((x) & 0xff) << 8)

typedef XftDraw *Draw;
typedef XftColor Color;
typedef XftGlyphFontSpec GlyphFontSpec;

/* Purely graphic info */
typedef struct {
	int tw, th; /* tty width and height */
	int w, h; /* window width and height */
	int ch; /* char height */
	int cw; /* char width  */
	int mode; /* window state/mode flags */
	int cursor; /* cursor style */
} TermWindow;

typedef struct {
	Display *dpy;
	Colormap cmap;
	Window win;
	Drawable buf;
	GlyphFontSpec *specbuf; /* font spec buffer used for rendering */
	Atom xembed, wmdeletewin, netwmname, netwmiconname, netwmpid;
	struct {
		XIM xim;
		XIC xic;
		XPoint spot;
		XVaNestedList spotlist;
	} ime;
	Draw draw;
	Visual *vis;
	XSetWindowAttributes attrs;
	int scr;
	int isfixed; /* is fixed geometry? */
	int l, t; /* left and top offset */
	int gm; /* geometry mask */
} XWindow;

typedef struct {
	Atom xtarget;
	char *primary, *clipboard;
	struct timespec tclick1;
	int clicks, clickcol, clickrow;
	uint clickstate;
} XSelection;

/* Font structure */
#define Font Font_
typedef struct {
	int height;
	int width;
	int ascent;
	int descent;
	int badslant;
	int badweight;
	short lbearing;
	short rbearing;
	XftFont *match;
	FcFontSet *set;
	FcFontSet *textset; /* untrimmed, so later non-color fonts remain */
	FcPattern *pattern;
} Font;

/* Drawing Context */
typedef struct {
	Color *col;
	size_t collen;
	Font font, bfont, ifont, ibfont;
	GC gc;
	GC ugc; /* underline strokes, created on first use */
} DC;

static inline ushort sixd_to_16bit(int);
static int xmakeglyphfontspecs(XftGlyphFontSpec *, const Glyph *, int, int, int);
static void xdrawglyphfontspecs(const XftGlyphFontSpec *, Glyph, int, int, int);
static void xdrawglyph(Glyph, int, int);
static void xclear(int, int, int, int);
static int xgeommasktogravity(int);
static int ximopen(Display *);
static void ximinstantiate(Display *, XPointer, XPointer);
static void ximdestroy(XIM, XPointer, XPointer);
static int xicdestroy(XIC, XPointer, XPointer);
static void xinit(int, int);
static void cresize(int, int);
static void xresize(int, int);
static void xhints(void);
static int xloadcolor(int, const char *, Color *);
static int xloadfont(Font *, FcPattern *);
static void xloadfonts(const char *, double);
static XftFont *xloademojifont(int);
static FcPattern *xtextfallback(FcFontSet *, FcPattern *, Rune);
static FcPattern *xfallbackmatch(Font *, FcFontSet **, FcPattern *, Rune, int);
static void xunloadfont(Font *);
static void xunloadfonts(void);
static void xsetenv(void);
static void xseturgency(int);
static int evcol(XEvent *);
static int evrow(XEvent *);

static void expose(XEvent *);
static void visibility(XEvent *);
static void unmap(XEvent *);
static void kpress(XEvent *);
static void cmessage(XEvent *);
static void resize(XEvent *);
static void focus(XEvent *);
static uint buttonmask(uint);
static int mouseaction(XEvent *, uint);
static void brelease(XEvent *);
static void bpress(XEvent *);
static void bmotion(XEvent *);
static void propnotify(XEvent *);
static void selnotify(XEvent *);
static void selclear_(XEvent *);
static void selrequest(XEvent *);
static void setsel(char *, Time);
static void mousesel(XEvent *, int);
static char *kmap(KeySym, uint);
static int match(uint, uint);

static void run(void);
static void usage(void);

static void (*handler[LASTEvent])(XEvent *) = {
	[KeyPress] = kpress,
	[ClientMessage] = cmessage,
	[ConfigureNotify] = resize,
	[VisibilityNotify] = visibility,
	[UnmapNotify] = unmap,
	[Expose] = expose,
	[FocusIn] = focus,
	[FocusOut] = focus,
	[MotionNotify] = bmotion,
	[ButtonPress] = bpress,
	[ButtonRelease] = brelease,
/*
 * Uncomment if you want the selection to disappear when you select something
 * different in another window.
 */
/*	[SelectionClear] = selclear_, */
	[SelectionNotify] = selnotify,
/*
 * PropertyNotify is only turned on when there is some INCR transfer happening
 * for the selection retrieval.
 */
	[PropertyNotify] = propnotify,
	[SelectionRequest] = selrequest,
};

/* Globals */
static DC dc;
static XWindow xw;
static struct {
	struct { uint32_t rgb; Color color; int valid; } entries[16];
	unsigned int next;
} underlinecolors;

static void
xclearunderlinecolors(void)
{
	unsigned int i;

	for (i = 0; i < LEN(underlinecolors.entries); i++) {
		if (underlinecolors.entries[i].valid)
			XftColorFree(xw.dpy, xw.vis, xw.cmap, &underlinecolors.entries[i].color);
		underlinecolors.entries[i].valid = 0;
	}
	underlinecolors.next = 0;
}

static unsigned long
xunderlinepixel(uint32_t rgb, unsigned long fallback)
{
	unsigned int i;
	Color color;
	XRenderColor value = { .red = ((rgb >> 16) & 255) * 257,
		.green = ((rgb >> 8) & 255) * 257,
		.blue = (rgb & 255) * 257, .alpha = 0xffff };

	if (rgb == UCOLOR_DEFAULT)
		return fallback;
	if (!IS_TRUECOL(rgb))
		return dc.col[rgb].pixel;
	for (i = 0; i < LEN(underlinecolors.entries); i++)
		if (underlinecolors.entries[i].valid && underlinecolors.entries[i].rgb == rgb)
			return underlinecolors.entries[i].color.pixel;
	if (!XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &value, &color))
		return fallback;
	i = underlinecolors.next++ % LEN(underlinecolors.entries);
	if (underlinecolors.entries[i].valid)
		XftColorFree(xw.dpy, xw.vis, xw.cmap, &underlinecolors.entries[i].color);
	underlinecolors.entries[i].rgb = rgb;
	underlinecolors.entries[i].color = color;
	underlinecolors.entries[i].valid = 1;
	return color.pixel;
}
static XSelection xsel;
static TermWindow win;

/* Font Ring Cache */
enum {
	FRC_NORMAL,
	FRC_ITALIC,
	FRC_BOLD,
	FRC_ITALICBOLD,
	FRC_TEXT = 4 /* or'ed in: text presentation avoids color fonts */
};

typedef struct {
	XftFont *font;
	int flags;
	Rune unicodep;
	int color; /* a color font, cached for text only when nothing else had unicodep */
} Fontcache;

/* Fontcache is an array now. A new font will be appended to the array. */
enum { CELL_MAX = 4096 }; /* largest font ascent or descent, in pixels */
static Fontcache *frc = NULL;
static int frclen = 0;
static int frccap = 0;
static char *usedfont = NULL;
static double usedfontsize = 0;
static double defaultfontsize = 0;
/* Separate sizes keep both narrow symbols and wide emoji within their cells. */
static struct {
	XftFont *font;
	int loaded;
} emoji[2];

static char *opt_class = NULL;
static char **opt_cmd  = NULL;
static char *opt_dir   = NULL;
static char *opt_embed = NULL;
static char *opt_font  = NULL;
static char *opt_io    = NULL;
static char *opt_line  = NULL;
static char *opt_name  = NULL;
static char *opt_title = NULL;

static int pendingw, pendingh; /* latest unapplied ConfigureNotify size */
static int cursorblinks = 0;
static struct timespec lastblink;

static void
blinkresetat(struct timespec now)
{
	lastblink = now;
	if (IS_SET(MODE_BLINK)) {
		win.mode &= ~MODE_BLINK;
		tsetdirtattr(ATTR_BLINK);
	}
}

static void
blinkreset(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	blinkresetat(now);
}

static double
blinkremaining(struct timespec now)
{
	double remain;

	/* Only a focused, visible cursor needs blink wakeups. */
	if (!blinktimeout || !((cursorblinks && IS_SET(MODE_FOCUSED) &&
	    !IS_SET(MODE_HIDE)) || tattrset(ATTR_BLINK)))
		return -1;
	remain = blinktimeout - TIMEDIFF(now, lastblink);
	if (remain <= 0) {
		if (-remain > blinktimeout)
			win.mode |= MODE_BLINK;
		win.mode ^= MODE_BLINK;
		tsetdirtattr(ATTR_BLINK);
		lastblink = now;
		return blinktimeout;
	}
	return remain;
}
static void
sendinput(const char *text, size_t len, int may_echo)
{
	if (len)
		blinkreset();
	ttyinput(text, len, may_echo);
}

static struct {
	int active, col, row;
	char *text;
} urlclick;
static int urlflashset;
static struct timespec lasturlflash;

static void
urlflashbegin(int col, int row)
{
	if (!urlflashtimeout)
		return;
	tflashurl(col, row, xsel.primary);
	clock_gettime(CLOCK_MONOTONIC, &lasturlflash);
	urlflashset = 1;
	redraw();
	XFlush(xw.dpy);
}

static double
urlflashremaining(struct timespec now)
{
	double remain;

	if (!urlflashset)
		return -1;
	remain = urlflashtimeout - TIMEDIFF(now, lasturlflash);
	if (remain <= 0) {
		urlflashset = 0;
		tclearurlflash();
		return -1;
	}
	return remain;
}

void
clipcopy(const Arg *dummy)
{
	Atom clipboard;

	free(xsel.clipboard);
	xsel.clipboard = NULL;

	if (xsel.primary != NULL) {
		xsel.clipboard = xstrdup(xsel.primary);
		clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);
		XSetSelectionOwner(xw.dpy, clipboard, xw.win, CurrentTime);
	}
}

void
clippaste(const Arg *dummy)
{
	Atom clipboard;

	clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);
	XConvertSelection(xw.dpy, clipboard, xsel.xtarget, clipboard,
			xw.win, CurrentTime);
}

void
selpaste(const Arg *dummy)
{
	XConvertSelection(xw.dpy, XA_PRIMARY, xsel.xtarget, XA_PRIMARY,
			xw.win, CurrentTime);
}

void
numlock(const Arg *dummy)
{
	win.mode ^= MODE_NUMLOCK;
}

void
zoom(const Arg *arg)
{
	Arg larg;

	larg.f = usedfontsize + arg->f;
	zoomabs(&larg);
}

void
zoomabs(const Arg *arg)
{
	xunloadfonts();
	xloadfonts(usedfont, arg->f);
	cresize(0, 0);
	redraw();
	xhints();
}

void
zoomreset(const Arg *arg)
{
	Arg larg;

	if (defaultfontsize > 0) {
		larg.f = defaultfontsize;
		zoomabs(&larg);
	}
}

void
ttysend(const Arg *arg)
{
	sendinput(arg->s, strlen(arg->s), 1);
}

int
evcol(XEvent *e)
{
	int x = e->xbutton.x - borderpx;
	LIMIT(x, 0, win.tw - 1);
	return x / win.cw;
}

int
evrow(XEvent *e)
{
	int y = e->xbutton.y - borderpx;
	LIMIT(y, 0, win.th - 1);
	return y / win.ch;
}

void
mousesel(XEvent *e, int done)
{
	int type, seltype = SEL_REGULAR;
	uint state = e->xbutton.state & ~(Button1Mask | forcemousemod);

	if (evcol(e) != xsel.clickcol || evrow(e) != xsel.clickrow)
		xsel.clicks = 0;

	for (type = 1; type < LEN(selmasks); ++type) {
		if (match(selmasks[type], state)) {
			seltype = type;
			break;
		}
	}
	selextend(evcol(e), evrow(e), seltype, done);
	if (done)
		setsel(getsel(), e->xbutton.time);
}

uint
buttonmask(uint button)
{
	return button == Button1 ? Button1Mask
	     : button == Button2 ? Button2Mask
	     : button == Button3 ? Button3Mask
	     : button == Button4 ? Button4Mask
	     : button == Button5 ? Button5Mask
	     : 0;
}

int
mouseaction(XEvent *e, uint release)
{
	MouseShortcut *ms;

	/* ignore Button<N>mask for Button<N> - it's set on release */
	uint state = e->xbutton.state & ~buttonmask(e->xbutton.button);

	for (ms = mshortcuts; ms < mshortcuts + LEN(mshortcuts); ms++) {
		if (ms->release == release &&
		    ms->button == e->xbutton.button &&
		    (match(ms->mod, state) ||  /* exact or forced */
		     match(ms->mod, state & ~forcemousemod))) {
			ms->func(&(ms->arg));
			return 1;
		}
	}

	return 0;
}

/* Successive clicks at the same cell expand through unique regex matches. */
static int
clicksnap(int col, int row, uint state, struct timespec now)
{
	unsigned int timeout = xsel.clicks == 1 ? doubleclicktimeout : tripleclicktimeout;

	state &= ~(Button1Mask | LockMask);
	if (xsel.clicks && col == xsel.clickcol && row == xsel.clickrow &&
	    state == xsel.clickstate && TIMEDIFF(now, xsel.tclick1) <= timeout)
		xsel.clicks = MIN(xsel.clicks + 1, (int)LEN(selectionpatterns) + 2);
	else
		xsel.clicks = 1;
	xsel.clickcol = col;
	xsel.clickrow = row;
	xsel.clickstate = state;
	xsel.tclick1 = now;
	return xsel.clicks == 1 ? 0 : SNAP_REGEX + xsel.clicks - 2;
}

void
bpress(XEvent *e)
{
	int btn = e->xbutton.button;
	struct timespec now;
	int snap;

	if (btn != Button1)
		xsel.clicks = 0;

	/* The wheel scrolls history, also beneath alternate-screen programs. */
	if (btn == Button4 || btn == Button5) {
		Arg a;
		a.i = MAX(1, mousescrolllines);
		if (btn == Button4) {
			kscrollup(&a);
		} else {
			kscrolldown(&a);
		}
		return;
	}

	if (btn == Button1 && match(XK_NO_MOD, e->xbutton.state & ~LockMask) &&
	    (urlclick.text = tgeturl(evcol(e), evrow(e)))) {
		xsel.clicks = 0;
		urlclick.active = 1;
		urlclick.col = evcol(e);
		urlclick.row = evrow(e);
		selstart(urlclick.col, urlclick.row, 0);
		return;
	}

	if (mouseaction(e, 0))
		return;

	if (btn == Button1) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		snap = clicksnap(evcol(e), evrow(e), e->xbutton.state, now);
		/* Once the largest match is selected, the next click starts over. */
		if (!selstart(evcol(e), evrow(e), snap) && xsel.clicks > 2) {
			xsel.clicks = 1;
			selstart(evcol(e), evrow(e), 0);
		}
	}
}

void
propnotify(XEvent *e)
{
	XPropertyEvent *xpev;
	Atom clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);

	xpev = &e->xproperty;
	if (xpev->state == PropertyNewValue &&
			(xpev->atom == XA_PRIMARY ||
			 xpev->atom == clipboard)) {
		selnotify(e);
	}
}

void
selnotify(XEvent *e)
{
	ulong nitems, ofs, rem;
	int format;
	uchar *data, *last, *repl;
	Atom type, incratom, property = None;

	incratom = XInternAtom(xw.dpy, "INCR", 0);

	ofs = 0;
	if (e->type == SelectionNotify)
		property = e->xselection.property;
	else if (e->type == PropertyNotify)
		property = e->xproperty.atom;

	if (property == None)
		return;

	do {
		if (XGetWindowProperty(xw.dpy, xw.win, property, ofs,
					BUFSIZ/4, False, AnyPropertyType,
					&type, &format, &nitems, &rem,
					&data)) {
			fprintf(stderr, "Clipboard allocation failed\n");
			return;
		}

		if (e->type == PropertyNotify && nitems == 0 && rem == 0) {
			/*
			 * If there is some PropertyNotify with no data, then
			 * this is the signal of the selection owner that all
			 * data has been transferred. We won't need to receive
			 * PropertyNotify events anymore.
			 */
			MODBIT(xw.attrs.event_mask, 0, PropertyChangeMask);
			XChangeWindowAttributes(xw.dpy, xw.win, CWEventMask,
					&xw.attrs);
		}

		if (type == incratom) {
			/*
			 * Activate the PropertyNotify events so we receive
			 * when the selection owner does send us the next
			 * chunk of data.
			 */
			MODBIT(xw.attrs.event_mask, 1, PropertyChangeMask);
			XChangeWindowAttributes(xw.dpy, xw.win, CWEventMask,
					&xw.attrs);

			/*
			 * Deleting the property is the transfer start signal.
			 */
			XDeleteProperty(xw.dpy, xw.win, (int)property);
			continue;
		}

		/*
		 * As seen in getsel:
		 * Line endings are inconsistent in the terminal and GUI world
		 * copy and pasting. When receiving some selection data,
		 * replace all '\n' with '\r'.
		 * FIXME: Fix the computer world.
		 */
		repl = data;
		last = data + nitems * format / 8;
		while ((repl = memchr(repl, '\n', last - repl))) {
			*repl++ = '\r';
		}

		if (IS_SET(MODE_BRCKTPASTE) && ofs == 0)
			sendinput("\033[200~", 6, 0);
		sendinput((char *)data, nitems * format / 8, 1);
		if (IS_SET(MODE_BRCKTPASTE) && rem == 0)
			sendinput("\033[201~", 6, 0);
		XFree(data);
		/* number of 32-bit chunks returned */
		ofs += nitems * format / 32;
	} while (rem > 0);

	/*
	 * Deleting the property again tells the selection owner to send the
	 * next data chunk in the property.
	 */
	XDeleteProperty(xw.dpy, xw.win, (int)property);
}

void
xclipcopy(void)
{
	clipcopy(NULL);
}

void
selclear_(XEvent *e)
{
	selclear();
}

void
selrequest(XEvent *e)
{
	XSelectionRequestEvent *xsre;
	XSelectionEvent xev;
	Atom xa_targets, string, clipboard;
	char *seltext;

	xsre = (XSelectionRequestEvent *) e;
	xev.type = SelectionNotify;
	xev.requestor = xsre->requestor;
	xev.selection = xsre->selection;
	xev.target = xsre->target;
	xev.time = xsre->time;
	if (xsre->property == None)
		xsre->property = xsre->target;

	/* reject */
	xev.property = None;

	xa_targets = XInternAtom(xw.dpy, "TARGETS", 0);
	if (xsre->target == xa_targets) {
		/* respond with the supported type */
		string = xsel.xtarget;
		XChangeProperty(xsre->display, xsre->requestor, xsre->property,
				XA_ATOM, 32, PropModeReplace,
				(uchar *) &string, 1);
		xev.property = xsre->property;
	} else if (xsre->target == xsel.xtarget || xsre->target == XA_STRING) {
		/*
		 * xith XA_STRING non ascii characters may be incorrect in the
		 * requestor. It is not our problem, use utf8.
		 */
		clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);
		if (xsre->selection == XA_PRIMARY) {
			seltext = xsel.primary;
		} else if (xsre->selection == clipboard) {
			seltext = xsel.clipboard;
		} else {
			fprintf(stderr,
				"Unhandled clipboard selection 0x%lx\n",
				xsre->selection);
			return;
		}
		if (seltext != NULL) {
			XChangeProperty(xsre->display, xsre->requestor,
					xsre->property, xsre->target,
					8, PropModeReplace,
					(uchar *)seltext, strlen(seltext));
			xev.property = xsre->property;
		}
	}

	/* all done, send a notification to the listener */
	if (!XSendEvent(xsre->display, xsre->requestor, 1, 0, (XEvent *) &xev))
		fprintf(stderr, "Error sending SelectionNotify event\n");
}

void
setsel(char *str, Time t)
{
	if (!str)
		return;

	free(xsel.primary);
	xsel.primary = str;

	XSetSelectionOwner(xw.dpy, XA_PRIMARY, xw.win, t);
	if (XGetSelectionOwner(xw.dpy, XA_PRIMARY) != xw.win)
		selclear();
}

void
xsetsel(char *str)
{
	setsel(str, CurrentTime);
}

void
brelease(XEvent *e)
{
	int btn = e->xbutton.button;

	if (btn == Button1 && urlclick.active) {
		if (urlclick.text && evcol(e) == urlclick.col && evrow(e) == urlclick.row) {
			selclear();
			setsel(urlclick.text, e->xbutton.time);
			urlclick.text = NULL;
			clipcopy(NULL);
			urlflashbegin(urlclick.col, urlclick.row);
		} else {
			if (urlclick.text)
				mousesel(e, 0);
			mousesel(e, 1);
		}
		free(urlclick.text);
		urlclick.text = NULL;
		urlclick.active = 0;
		return;
	}

	if (mouseaction(e, 1))
		return;
	if (btn == Button1)
		mousesel(e, 1);
}

void
bmotion(XEvent *e)
{
	if (urlclick.active) {
		if (!urlclick.text || evcol(e) != urlclick.col || evrow(e) != urlclick.row) {
			free(urlclick.text);
			urlclick.text = NULL;
			mousesel(e, 0);
		}
		return;
	}

	mousesel(e, 0);
}

void
cresize(int width, int height)
{
	int col, row;

	if (width != 0)
		win.w = width;
	if (height != 0)
		win.h = height;

	col = (win.w - 2 * borderpx) / win.cw;
	row = (win.h - 2 * borderpx) / win.ch;
	col = MAX(1, col);
	row = MAX(1, row);

	tresize(col, row);
	xresize(col, row);
	ttyresize(win.tw, win.th);
}

void
xresize(int col, int row)
{
	win.tw = col * win.cw;
	win.th = row * win.ch;

	XFreePixmap(xw.dpy, xw.buf);
	xw.buf = XCreatePixmap(xw.dpy, xw.win, win.w, win.h,
			DefaultDepth(xw.dpy, xw.scr));
	XftDrawChange(xw.draw, xw.buf);
	xclear(0, 0, win.w, win.h);
	/* The new buffer is blank, even when the rows and columns are unchanged. */
	tfulldirt();

	/* resize to new width */
	xw.specbuf = xrealloc(xw.specbuf, col * sizeof(GlyphFontSpec));
}

ushort
sixd_to_16bit(int x)
{
	return x == 0 ? 0 : 0x3737 + 0x2828 * x;
}

int
xloadcolor(int i, const char *name, Color *ncolor)
{
	XRenderColor color = { .alpha = 0xffff };

	if (!name) {
		if (BETWEEN(i, 16, 255)) { /* 256 color */
			if (i < 6*6*6+16) { /* same colors as xterm */
				color.red   = sixd_to_16bit( ((i-16)/36)%6 );
				color.green = sixd_to_16bit( ((i-16)/6) %6 );
				color.blue  = sixd_to_16bit( ((i-16)/1) %6 );
			} else { /* greyscale */
				color.red = 0x0808 + 0x0a0a * (i - (6*6*6+16));
				color.green = color.blue = color.red;
			}
			return XftColorAllocValue(xw.dpy, xw.vis,
			                          xw.cmap, &color, ncolor);
		} else
			name = colorname[i];
	}

	return XftColorAllocName(xw.dpy, xw.vis, xw.cmap, name, ncolor);
}

void
xloadcols(void)
{
	int i;
	static int loaded;
	Color *cp;

	xclearunderlinecolors();

	if (loaded) {
		for (cp = dc.col; cp < &dc.col[dc.collen]; ++cp)
			XftColorFree(xw.dpy, xw.vis, xw.cmap, cp);
	} else {
		dc.collen = MAX(LEN(colorname), 256);
		dc.col = xmalloc(dc.collen * sizeof(Color));
	}

	for (i = 0; i < dc.collen; i++)
		if (!xloadcolor(i, NULL, &dc.col[i])) {
			if (colorname[i])
				die("could not allocate color '%s'\n", colorname[i]);
			else
				die("could not allocate color %d\n", i);
		}
	loaded = 1;
}

int
xgetcolor(int x, unsigned char *r, unsigned char *g, unsigned char *b)
{
	if (!BETWEEN(x, 0, dc.collen - 1))
		return 1;

	*r = dc.col[x].color.red >> 8;
	*g = dc.col[x].color.green >> 8;
	*b = dc.col[x].color.blue >> 8;

	return 0;
}

int
xsetcolorname(int x, const char *name)
{
	Color ncolor;

	if (!BETWEEN(x, 0, dc.collen - 1))
		return 1;

	if (!xloadcolor(x, name, &ncolor))
		return 1;

	XftColorFree(xw.dpy, xw.vis, xw.cmap, &dc.col[x]);
	dc.col[x] = ncolor;

	return 0;
}

/*
 * Absolute coordinates.
 */
void
xclear(int x1, int y1, int x2, int y2)
{
	XftDrawRect(xw.draw,
			&dc.col[IS_SET(MODE_REVERSE)? defaultfg : defaultbg],
			x1, y1, x2-x1, y2-y1);
}

void
xhints(void)
{
	XClassHint class = {opt_name ? opt_name : "phist",
	                    opt_class ? opt_class : "phist"};
	XWMHints wm = {.flags = InputHint, .input = 1};
	XSizeHints *sizeh;

	sizeh = XAllocSizeHints();

	sizeh->flags = PSize | PResizeInc | PBaseSize | PMinSize;
	sizeh->height = win.h;
	sizeh->width = win.w;
	sizeh->height_inc = win.ch;
	sizeh->width_inc = win.cw;
	sizeh->base_height = 2 * borderpx;
	sizeh->base_width = 2 * borderpx;
	sizeh->min_height = win.ch + 2 * borderpx;
	sizeh->min_width = win.cw + 2 * borderpx;
	if (xw.isfixed) {
		sizeh->flags |= PMaxSize;
		sizeh->min_width = sizeh->max_width = win.w;
		sizeh->min_height = sizeh->max_height = win.h;
	}
	if (xw.gm & (XValue|YValue)) {
		sizeh->flags |= USPosition | PWinGravity;
		sizeh->x = xw.l;
		sizeh->y = xw.t;
		sizeh->win_gravity = xgeommasktogravity(xw.gm);
	}

	XSetWMProperties(xw.dpy, xw.win, NULL, NULL, NULL, 0, sizeh, &wm,
			&class);
	XFree(sizeh);
}

int
xgeommasktogravity(int mask)
{
	switch (mask & (XNegative|YNegative)) {
	case 0:
		return NorthWestGravity;
	case XNegative:
		return NorthEastGravity;
	case YNegative:
		return SouthWestGravity;
	}

	return SouthEastGravity;
}

/* Whether a font's ascent or descent is sane, and safe to do arithmetic on. */
static int
xmetricok(int value)
{
	return value >= -CELL_MAX && value <= CELL_MAX;
}

int
xloadfont(Font *f, FcPattern *pattern)
{
	FcPattern *configured;
	FcPattern *match;
	FcResult result;
	XGlyphInfo extents;
	int wantattr, haveattr;

	/*
	 * Manually configure instead of calling XftMatchFont
	 * so that we can use the configured pattern for
	 * "missing glyph" lookups.
	 */
	configured = FcPatternDuplicate(pattern);
	if (!configured)
		return 1;

	FcConfigSubstitute(NULL, configured, FcMatchPattern);
	XftDefaultSubstitute(xw.dpy, xw.scr, configured);

	match = FcFontMatch(NULL, configured, &result);
	if (!match) {
		FcPatternDestroy(configured);
		return 1;
	}

	if (!(f->match = XftFontOpenPattern(xw.dpy, match))) {
		FcPatternDestroy(configured);
		FcPatternDestroy(match);
		return 1;
	}

	if ((XftPatternGetInteger(pattern, "slant", 0, &wantattr) ==
	    XftResultMatch)) {
		/*
		 * Check if xft was unable to find a font with the appropriate
		 * slant but gave us one anyway. Try to mitigate.
		 */
		if ((XftPatternGetInteger(f->match->pattern, "slant", 0,
		    &haveattr) != XftResultMatch) || haveattr < wantattr) {
			f->badslant = 1;
			fputs("font slant does not match\n", stderr);
		}
	}

	if ((XftPatternGetInteger(pattern, "weight", 0, &wantattr) ==
	    XftResultMatch)) {
		if ((XftPatternGetInteger(f->match->pattern, "weight", 0,
		    &haveattr) != XftResultMatch) || haveattr != wantattr) {
			f->badweight = 1;
			fputs("font weight does not match\n", stderr);
		}
	}

	XftTextExtentsUtf8(xw.dpy, f->match,
		(const FcChar8 *) ascii_printable,
		strlen(ascii_printable), &extents);

	f->set = f->textset = NULL;
	f->pattern = configured;

	f->ascent = f->match->ascent;
	f->descent = f->match->descent;
	f->lbearing = 0;
	f->rbearing = f->match->max_advance_width;

	f->height = f->ascent + f->descent;
	f->width = DIVCEIL(extents.xOff, strlen(ascii_printable));

	return 0;
}

void
xloadfonts(const char *fontstr, double fontsize)
{
	FcPattern *pattern;
	double fontval;

	if (fontstr[0] == '-')
		pattern = XftXlfdParse(fontstr, False, False);
	else
		pattern = FcNameParse((const FcChar8 *)fontstr);

	if (!pattern)
		die("can't open font %s\n", fontstr);

	if (fontsize > 1) {
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternDel(pattern, FC_SIZE);
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, (double)fontsize);
		usedfontsize = fontsize;
	} else {
		if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval) ==
				FcResultMatch) {
			usedfontsize = fontval;
		} else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &fontval) ==
				FcResultMatch) {
			usedfontsize = -1;
		} else {
			/*
			 * Default font size is 12, if none given. This is to
			 * have a known usedfontsize value.
			 */
			FcPatternAddDouble(pattern, FC_PIXEL_SIZE, 12);
			usedfontsize = 12;
		}
		defaultfontsize = usedfontsize;
	}

	if (xloadfont(&dc.font, pattern))
		die("can't open font %s\n", fontstr);

	if (usedfontsize < 0) {
		FcPatternGetDouble(dc.font.match->pattern,
		                   FC_PIXEL_SIZE, 0, &fontval);
		usedfontsize = fontval;
		if (fontsize == 0)
			defaultfontsize = fontval;
	}

	/* Setting character width and height. */
	win.cw = ceilf(dc.font.width * cwscale);
	win.ch = ceilf(dc.font.height * chscale);

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
	if (xloadfont(&dc.ifont, pattern))
		die("can't open font %s\n", fontstr);

	FcPatternDel(pattern, FC_WEIGHT);
	FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
	if (xloadfont(&dc.ibfont, pattern))
		die("can't open font %s\n", fontstr);

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
	if (xloadfont(&dc.bfont, pattern))
		die("can't open font %s\n", fontstr);

	FcPatternDestroy(pattern);
}

/*
 * First font in the sorted fallback set covering rune without being a color
 * font, so text presentation stays text when some other font can draw it.
 */
static FcPattern *
xtextfallback(FcFontSet *set, FcPattern *pattern, Rune rune)
{
	FcCharSet *charset;
	FcBool color;
	int i;

	for (i = 0; set && i < set->nfont; i++) {
		if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &charset) !=
		    FcResultMatch || !FcCharSetHasChar(charset, rune))
			continue;
		if (FcPatternGetBool(set->fonts[i], FC_COLOR, 0, &color) ==
		    FcResultMatch && color)
			continue;
		return FcFontRenderPrepare(NULL, pattern, set->fonts[i]);
	}
	return NULL;
}

/*
 * Fontconfig's best fallback for rune; for text presentation, a color match
 * gives way to the first non-color font covering rune, if any.
 */
static FcPattern *
xfallbackmatch(Font *font, FcFontSet **sets, FcPattern *pattern, Rune rune,
               int text)
{
	FcPattern *match, *mono;
	FcResult result;
	FcBool color;

	match = FcFontSetMatch(0, sets, 1, pattern, &result);
	if (!text || !match || FcPatternGetBool(match, FC_COLOR, 0, &color) !=
	    FcResultMatch || !color)
		return match;
	if (!font->textset)
		font->textset = FcFontSort(0, font->pattern, 0, 0, &result);
	if ((mono = xtextfallback(font->textset, pattern, rune))) {
		FcPatternDestroy(match);
		match = mono;
	}
	return match;
}

/* Optional color font, sized to the available cells rather than changing widths. */
XftFont *
xloademojifont(int columns)
{
	FcPattern *pattern, *match;
	FcResult result;
	FcBool color;
	XftFont *face;
	double size = usedfontsize, scale;
	int i, slot = columns - 1;

	if (!emojifont || !*emojifont)
		return NULL;
	if (emoji[slot].loaded)
		return emoji[slot].font;
	emoji[slot].loaded = 1;

	for (i = 0; i < 3; i++) {
		if (!(pattern = FcNameParse((const FcChar8 *)emojifont)))
			return NULL;
		FcPatternDel(pattern, FC_SIZE);
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, MAX(1, size));
		FcPatternAddBool(pattern, FC_COLOR, FcTrue);
		FcConfigSubstitute(NULL, pattern, FcMatchPattern);
		XftDefaultSubstitute(xw.dpy, xw.scr, pattern);
		match = FcFontMatch(NULL, pattern, &result);
		FcPatternDestroy(pattern);
		if (!match)
			return NULL;
		/* Fontconfig may substitute a text font if no color font is installed. */
		if (FcPatternGetBool(match, FC_COLOR, 0, &color) != FcResultMatch || !color) {
			FcPatternDestroy(match);
			return NULL;
		}
		if (!(face = XftFontOpenPattern(xw.dpy, match))) {
			FcPatternDestroy(match);
			return NULL;
		}
		/* Its metrics position glyphs within the cell. */
		if (!xmetricok(face->ascent) || !xmetricok(face->height)) {
			XftFontClose(xw.dpy, face);
			return NULL;
		}
		scale = MIN((double)(columns * win.cw) / MAX(1, face->max_advance_width),
		            (double)win.ch / MAX(1, face->height));
		if (scale >= 1)
			return emoji[slot].font = face;
		XftFontClose(xw.dpy, face);
		size = MAX(1, floor(size * scale));
	}
	return NULL;
}

void
xunloadfont(Font *f)
{
	XftFontClose(xw.dpy, f->match);
	FcPatternDestroy(f->pattern);
	if (f->set)
		FcFontSetDestroy(f->set);
	if (f->textset)
		FcFontSetDestroy(f->textset);
	f->set = f->textset = NULL;
}

void
xunloadfonts(void)
{
	int i;

	for (i = 0; i < LEN(emoji); i++) {
		if (emoji[i].font)
			XftFontClose(xw.dpy, emoji[i].font);
		emoji[i].font = NULL;
		emoji[i].loaded = 0;
	}

	/* Free the loaded fonts in the font cache.  */
	while (frclen > 0)
		XftFontClose(xw.dpy, frc[--frclen].font);

	xunloadfont(&dc.font);
	xunloadfont(&dc.bfont);
	xunloadfont(&dc.ifont);
	xunloadfont(&dc.ibfont);
}

int
ximopen(Display *dpy)
{
	XIMCallback imdestroy = { .client_data = NULL, .callback = ximdestroy };
	XICCallback icdestroy = { .client_data = NULL, .callback = xicdestroy };

	xw.ime.xim = XOpenIM(xw.dpy, NULL, NULL, NULL);
	if (xw.ime.xim == NULL)
		return 0;

	if (XSetIMValues(xw.ime.xim, XNDestroyCallback, &imdestroy, NULL))
		fprintf(stderr, "XSetIMValues: "
		                "Could not set XNDestroyCallback.\n");

	xw.ime.spotlist = XVaCreateNestedList(0, XNSpotLocation, &xw.ime.spot,
	                                      NULL);

	if (xw.ime.xic == NULL) {
		xw.ime.xic = XCreateIC(xw.ime.xim, XNInputStyle,
		                       XIMPreeditNothing | XIMStatusNothing,
		                       XNClientWindow, xw.win,
		                       XNDestroyCallback, &icdestroy,
		                       NULL);
	}
	if (xw.ime.xic == NULL)
		fprintf(stderr, "XCreateIC: Could not create input context.\n");

	return 1;
}

void
ximinstantiate(Display *dpy, XPointer client, XPointer call)
{
	if (ximopen(dpy))
		XUnregisterIMInstantiateCallback(xw.dpy, NULL, NULL, NULL,
		                                 ximinstantiate, NULL);
}

void
ximdestroy(XIM xim, XPointer client, XPointer call)
{
	xw.ime.xim = NULL;
	XRegisterIMInstantiateCallback(xw.dpy, NULL, NULL, NULL,
	                               ximinstantiate, NULL);
	XFree(xw.ime.spotlist);
}

int
xicdestroy(XIC xim, XPointer client, XPointer call)
{
	xw.ime.xic = NULL;
	return 1;
}

void
xinit(int cols, int rows)
{
	XGCValues gcvalues;
	Cursor cursor;
	Window parent, root;
	pid_t thispid = getpid();
	XColor xmousefg, xmousebg;

	if (!(xw.dpy = XOpenDisplay(NULL)))
		die("can't open display\n");
	xw.scr = XDefaultScreen(xw.dpy);
	xw.vis = XDefaultVisual(xw.dpy, xw.scr);

	/* font */
	if (!FcInit())
		die("could not init fontconfig.\n");

	usedfont = (opt_font == NULL)? font : opt_font;
	xloadfonts(usedfont, 0);

	/* colors */
	xw.cmap = XDefaultColormap(xw.dpy, xw.scr);
	xloadcols();

	/* adjust fixed window geometry */
	win.w = 2 * borderpx + cols * win.cw;
	win.h = 2 * borderpx + rows * win.ch;
	if (xw.gm & XNegative)
		xw.l += DisplayWidth(xw.dpy, xw.scr) - win.w - 2;
	if (xw.gm & YNegative)
		xw.t += DisplayHeight(xw.dpy, xw.scr) - win.h - 2;

	/* Events */
	xw.attrs.background_pixel = dc.col[defaultbg].pixel;
	xw.attrs.border_pixel = dc.col[defaultbg].pixel;
	xw.attrs.bit_gravity = NorthWestGravity;
	xw.attrs.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask
		| ExposureMask | VisibilityChangeMask | StructureNotifyMask
		| ButtonMotionMask | ButtonPressMask | ButtonReleaseMask;
	xw.attrs.colormap = xw.cmap;

	root = XRootWindow(xw.dpy, xw.scr);
	if (!(opt_embed && (parent = strtol(opt_embed, NULL, 0))))
		parent = root;
	xw.win = XCreateWindow(xw.dpy, root, xw.l, xw.t,
			win.w, win.h, 0, XDefaultDepth(xw.dpy, xw.scr), InputOutput,
			xw.vis, CWBackPixel | CWBorderPixel | CWBitGravity
			| CWEventMask | CWColormap, &xw.attrs);
	if (parent != root)
		XReparentWindow(xw.dpy, xw.win, parent, xw.l, xw.t);

	memset(&gcvalues, 0, sizeof(gcvalues));
	gcvalues.graphics_exposures = False;
	dc.gc = XCreateGC(xw.dpy, xw.win, GCGraphicsExposures,
			&gcvalues);
	xw.buf = XCreatePixmap(xw.dpy, xw.win, win.w, win.h,
			DefaultDepth(xw.dpy, xw.scr));
	XSetForeground(xw.dpy, dc.gc, dc.col[defaultbg].pixel);
	XFillRectangle(xw.dpy, xw.buf, dc.gc, 0, 0, win.w, win.h);

	/* font spec buffer */
	xw.specbuf = xmalloc(cols * sizeof(GlyphFontSpec));

	/* Xft rendering context */
	xw.draw = XftDrawCreate(xw.dpy, xw.buf, xw.vis, xw.cmap);

	/* input methods */
	if (!ximopen(xw.dpy)) {
		XRegisterIMInstantiateCallback(xw.dpy, NULL, NULL, NULL,
	                                       ximinstantiate, NULL);
	}

	/* white cursor, black outline */
	cursor = XCreateFontCursor(xw.dpy, mouseshape);
	XDefineCursor(xw.dpy, xw.win, cursor);

	if (XParseColor(xw.dpy, xw.cmap, colorname[mousefg], &xmousefg) == 0) {
		xmousefg.red   = 0xffff;
		xmousefg.green = 0xffff;
		xmousefg.blue  = 0xffff;
	}

	if (XParseColor(xw.dpy, xw.cmap, colorname[mousebg], &xmousebg) == 0) {
		xmousebg.red   = 0x0000;
		xmousebg.green = 0x0000;
		xmousebg.blue  = 0x0000;
	}

	XRecolorCursor(xw.dpy, cursor, &xmousefg, &xmousebg);

	xw.xembed = XInternAtom(xw.dpy, "_XEMBED", False);
	xw.wmdeletewin = XInternAtom(xw.dpy, "WM_DELETE_WINDOW", False);
	xw.netwmname = XInternAtom(xw.dpy, "_NET_WM_NAME", False);
	xw.netwmiconname = XInternAtom(xw.dpy, "_NET_WM_ICON_NAME", False);
	XSetWMProtocols(xw.dpy, xw.win, &xw.wmdeletewin, 1);

	xw.netwmpid = XInternAtom(xw.dpy, "_NET_WM_PID", False);
	XChangeProperty(xw.dpy, xw.win, xw.netwmpid, XA_CARDINAL, 32,
			PropModeReplace, (uchar *)&thispid, 1);

	win.mode = MODE_NUMLOCK;
	resettitle();
	xhints();
	XMapWindow(xw.dpy, xw.win);
	XSync(xw.dpy, False);

	xsel.clicks = 0;
	xsel.primary = NULL;
	xsel.clipboard = NULL;
	xsel.xtarget = XInternAtom(xw.dpy, "UTF8_STRING", 0);
	if (xsel.xtarget == None)
		xsel.xtarget = XA_STRING;
}

/* Convert a coordinate for a glyph spec; casting one out of range is undefined. */
static short
xshort(float v)
{
	return v <= SHRT_MIN ? SHRT_MIN : v >= SHRT_MAX ? SHRT_MAX : (short)v;
}

int
xmakeglyphfontspecs(XftGlyphFontSpec *specs, const Glyph *glyphs, int len, int x, int y)
{
	float winx = borderpx + x * win.cw, winy = borderpx + y * win.ch, xp, yp;
	uint32_t mode, prevmode = UINT32_MAX;
	Font *font = &dc.font;
	XftFont *emoji_face;
	int frcflags = FRC_NORMAL;
	float runewidth = win.cw;
	Rune rune;
	FT_UInt glyphidx;
	FcResult fcres;
	FcPattern *fcpattern, *fontpattern;
	FcFontSet *fcsets[] = { NULL };
	FcCharSet *fccharset;
	FcBool color;
	int i, f, numspecs = 0;

	for (i = 0, xp = winx, yp = winy + font->ascent; i < len; ++i) {
		/* Fetch rune and mode for current glyph. */
		rune = glyphs[i].u;
		mode = glyphs[i].mode;

		/* Skip dummy wide-character spacing. */
		if (mode & ATTR_WDUMMY)
			continue;

		/* Determine font for glyph if different from previous glyph. */
		if (prevmode != mode) {
			prevmode = mode;
			font = &dc.font;
			frcflags = FRC_NORMAL;
			runewidth = win.cw *
				((mode & ATTR_WIDE) && win.tw > win.cw ? 2.0f : 1.0f);
			if ((mode & ATTR_ITALIC) && (mode & ATTR_BOLD)) {
				font = &dc.ibfont;
				frcflags = FRC_ITALICBOLD;
			} else if (mode & ATTR_ITALIC) {
				font = &dc.ifont;
				frcflags = FRC_ITALIC;
			} else if (mode & ATTR_BOLD) {
				font = &dc.bfont;
				frcflags = FRC_BOLD;
			}
			if (!EMOJIPRES(mode))
				frcflags |= FRC_TEXT;
			yp = winy + font->ascent;
		}

		/*
		 * Color emoji override existing text glyphs for emoji presentation:
		 * wide characters unless followed by U+FE0E, and narrow ones
		 * followed by U+FE0F.  ASCII text is never overridden.
		 */
		if (rune >= 0xA9 && rune != 0x200D && EMOJIPRES(mode) &&
		    !BETWEEN(rune, 0xFE00, 0xFE0F) && !BETWEEN(rune, 0xE0020, 0xE007F) &&
		    (emoji_face = xloademojifont((mode & ATTR_WIDE) ? 2 : 1)) &&
		    (glyphidx = XftCharIndex(xw.dpy, emoji_face, rune))) {
			specs[numspecs].font = emoji_face;
			specs[numspecs].glyph = glyphidx;
			specs[numspecs].x = xshort(xp);
			specs[numspecs].y = xshort(winy + (win.ch - emoji_face->height) / 2 +
			                               emoji_face->ascent);
			xp += runewidth;
			numspecs++;
			continue;
		}

		/* Lookup character index with default font. */
		glyphidx = XftCharIndex(xw.dpy, font->match, rune);
		if (glyphidx) {
			specs[numspecs].font = font->match;
			specs[numspecs].glyph = glyphidx;
			specs[numspecs].x = (short)xp;
			specs[numspecs].y = (short)yp;
			xp += runewidth;
			numspecs++;
			continue;
		}

		/* Fallback on font cache, search the font cache for match. */
		for (f = 0; f < frclen; f++) {
			/* Other characters may have a non-color fallback to find. */
			if ((frcflags & FRC_TEXT) && frc[f].color && frc[f].unicodep != rune)
				continue;
			glyphidx = XftCharIndex(xw.dpy, frc[f].font, rune);
			/* Everything correct. */
			if (glyphidx && frc[f].flags == frcflags)
				break;
			/* We got a default font for a not found glyph. */
			if (!glyphidx && frc[f].flags == frcflags
					&& frc[f].unicodep == rune) {
				break;
			}
		}

		/* Nothing was found. Use fontconfig to find matching font. */
		if (f >= frclen) {
			if (!font->set)
				font->set = FcFontSort(0, font->pattern,
				                       1, 0, &fcres);
			fcsets[0] = font->set;

			/*
			 * Nothing was found in the cache. Now use
			 * some dozen of Fontconfig calls to get the
			 * font for one single character.
			 *
			 * Xft and fontconfig are design failures.
			 */
			fcpattern = FcPatternDuplicate(font->pattern);
			fccharset = FcCharSetCreate();

			FcCharSetAddChar(fccharset, rune);
			FcPatternAddCharSet(fcpattern, FC_CHARSET,
					fccharset);
			FcPatternAddBool(fcpattern, FC_SCALABLE, 1);

			FcConfigSubstitute(0, fcpattern,
					FcMatchPattern);
			FcDefaultSubstitute(fcpattern);

			fontpattern = xfallbackmatch(font, fcsets, fcpattern, rune,
			                             frcflags & FRC_TEXT);

			/* Allocate memory for the new cache entry. */
			if (frclen >= frccap) {
				frccap += 16;
				frc = xrealloc(frc, frccap * sizeof(Fontcache));
			}

			frc[frclen].color = fontpattern &&
				FcPatternGetBool(fontpattern, FC_COLOR, 0, &color) ==
				FcResultMatch && color;
			frc[frclen].font = XftFontOpenPattern(xw.dpy,
					fontpattern);
			if (!frc[frclen].font)
				die("XftFontOpenPattern failed seeking fallback font: %s\n",
					strerror(errno));
			frc[frclen].flags = frcflags;
			frc[frclen].unicodep = rune;

			glyphidx = XftCharIndex(xw.dpy, frc[frclen].font, rune);

			f = frclen;
			frclen++;

			FcPatternDestroy(fcpattern);
			FcCharSetDestroy(fccharset);
		}

		specs[numspecs].font = frc[f].font;
		specs[numspecs].glyph = glyphidx;
		specs[numspecs].x = (short)xp;
		specs[numspecs].y = (short)yp;
		xp += runewidth;
		numspecs++;
	}

	return numspecs;
}

/* Items that fit in a drawing request, in 4-byte units, after its 3-unit header.
 * Respect the server's limit, which may be lower than Xlib's 65,535 units. */
static int
xrequestitems(int units)
{
	return MAX(2, (XMaxRequestSize(xw.dpy) - 3) / units);
}

/* XDrawLines() in as many requests as it takes, each starting where the last ended. */
static void
xdrawpolyline(GC gc, XPoint *points, int n, int mode)
{
	int batch = xrequestitems(1), i, j, count;
	XPoint start = points[0], saved;

	for (i = 0; i < n - 1; i += count - 1) {
		count = MIN(batch, n - i);
		/* With CoordModePrevious, the first point of each must be absolute. */
		saved = points[i];
		points[i] = start;
		XDrawLines(xw.dpy, XftDrawDrawable(xw.draw), gc, points + i, count, mode);
		points[i] = saved;
		if (mode == CoordModeOrigin) {
			start = points[i + count - 1];
		} else {
			for (j = i + 1; j < i + count; j++) {
				start.x += points[j].x;
				start.y += points[j].y;
			}
		}
	}
}

static int isSlopeRising (int x, int iPoint, int waveWidth)
{
	//    .     .     .     .
	//   / \   / \   / \   / \
	//  /   \ /   \ /   \ /   \
	// .     .     .     .     .

	x += iPoint * (waveWidth/2);

	int absSlope = x / ((float)waveWidth/2);

	return (absSlope % 2);
}

static int getSlope (int x, int iPoint, int waveWidth)
{
	// Sizes: Caps are half width of slopes
	//    1_2       1_2       1_2      1_2
	//   /   \     /   \     /   \    /   \
	//  /     \   /     \   /     \  /     \
	// 0       3_0       3_0      3_0       3_
	// <2->    <1>         <---6---->

	int firstType;
	x -= (x / waveWidth) * waveWidth;
	if (x < (waveWidth * (2.f/6.f)))
		firstType = UNDERCURL_SLOPE_ASCENDING;
	else if (x < (waveWidth * (3.f/6.f)))
		firstType = UNDERCURL_SLOPE_TOP_CAP;
	else if (x < (waveWidth * (5.f/6.f)))
		firstType = UNDERCURL_SLOPE_DESCENDING;
	else
		firstType = UNDERCURL_SLOPE_BOTTOM_CAP;

	int pointType = (iPoint % 4);
	pointType += firstType;
	pointType %= 4;

	return pointType;
}

void
xdrawglyphfontspecs(const XftGlyphFontSpec *specs, Glyph base, int len, int x, int y)
{
	int charlen = len * ((base.mode & ATTR_WIDE) && win.tw > win.cw ? 2 : 1);
	int winx = borderpx + x * win.cw, winy = borderpx + y * win.ch,
	    width = charlen * win.cw;
	Color *fg, *bg, *temp, revfg, revbg, truefg, truebg;
	XRenderColor colfg, colbg;
	XRectangle r;
	int ustyle = USTYLE(base);

	if ((base.mode & ATTR_URL) && turlflashed(x, y))
		base.mode ^= ATTR_REVERSE;

	/* Fallback on color display for attributes not supported by the font */
	if (base.mode & ATTR_ITALIC && base.mode & ATTR_BOLD) {
		if (dc.ibfont.badslant || dc.ibfont.badweight)
			base.fg = defaultattr;
	} else if ((base.mode & ATTR_ITALIC && dc.ifont.badslant) ||
	    (base.mode & ATTR_BOLD && dc.bfont.badweight)) {
		base.fg = defaultattr;
	}

	if (IS_TRUECOL(base.fg)) {
		colfg.alpha = 0xffff;
		colfg.red = TRUERED(base.fg);
		colfg.green = TRUEGREEN(base.fg);
		colfg.blue = TRUEBLUE(base.fg);
		XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &colfg, &truefg);
		fg = &truefg;
	} else {
		fg = &dc.col[base.fg];
	}

	if (IS_TRUECOL(base.bg)) {
		colbg.alpha = 0xffff;
		colbg.green = TRUEGREEN(base.bg);
		colbg.red = TRUERED(base.bg);
		colbg.blue = TRUEBLUE(base.bg);
		XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &colbg, &truebg);
		bg = &truebg;
	} else {
		bg = &dc.col[base.bg];
	}

	/* Change basic system colors [0-7] to bright system colors [8-15] */
	if ((base.mode & ATTR_BOLD_FAINT) == ATTR_BOLD && BETWEEN(base.fg, 0, 7))
		fg = &dc.col[base.fg + 8];

	if (IS_SET(MODE_REVERSE)) {
		if (fg == &dc.col[defaultfg]) {
			fg = &dc.col[defaultbg];
		} else {
			colfg.red = ~fg->color.red;
			colfg.green = ~fg->color.green;
			colfg.blue = ~fg->color.blue;
			colfg.alpha = fg->color.alpha;
			XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &colfg,
					&revfg);
			fg = &revfg;
		}

		if (bg == &dc.col[defaultbg]) {
			bg = &dc.col[defaultfg];
		} else {
			colbg.red = ~bg->color.red;
			colbg.green = ~bg->color.green;
			colbg.blue = ~bg->color.blue;
			colbg.alpha = bg->color.alpha;
			XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &colbg,
					&revbg);
			bg = &revbg;
		}
	}

	if ((base.mode & ATTR_BOLD_FAINT) == ATTR_FAINT) {
		colfg.red = fg->color.red / 2;
		colfg.green = fg->color.green / 2;
		colfg.blue = fg->color.blue / 2;
		colfg.alpha = fg->color.alpha;
		XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &colfg, &revfg);
		fg = &revfg;
	}

	if (base.mode & ATTR_REVERSE) {
		temp = fg;
		fg = bg;
		bg = temp;
	}

	if (base.mode & ATTR_BLINK && win.mode & MODE_BLINK)
		fg = bg;

	if (base.mode & ATTR_INVISIBLE)
		fg = bg;

	/* Intelligent cleaning up of the borders. */
	if (x == 0) {
		xclear(0, (y == 0)? 0 : winy, borderpx,
			winy + win.ch +
			((winy + win.ch >= borderpx + win.th)? win.h : 0));
	}
	if (winx + width >= borderpx + win.tw) {
		xclear(winx + width, (y == 0)? 0 : winy, win.w,
			((winy + win.ch >= borderpx + win.th)? win.h : (winy + win.ch)));
	}
	if (y == 0)
		xclear(winx, 0, winx + width, borderpx);
	if (winy + win.ch >= borderpx + win.th)
		xclear(winx, winy + win.ch, winx + width, win.h);

	/* Clean up the region we want to draw to. */
	XftDrawRect(xw.draw, bg, winx, winy, width, win.ch);

	/* Set the clip region because Xft is sometimes dirty. */
	r.x = 0;
	r.y = 0;
	r.height = win.ch;
	r.width = width;
	XftDrawSetClipRectangles(xw.draw, winx, winy, &r, 1);

	/* Render the glyphs. */
	XftDrawGlyphFontSpec(xw.draw, fg, specs, len);

	/* URLs get a normal underline unless the application supplied its own. */
	if ((base.mode & ATTR_URL) && !(base.mode & ATTR_UNDERLINE)) {
		ustyle = 1;
		base.ucolor = UCOLOR_DEFAULT;
	}
	/* Render underline and strikethrough. */
	if ((base.mode & (ATTR_UNDERLINE | ATTR_URL)) && !dc.ugc)
		dc.ugc = XCreateGC(xw.dpy, XftDrawDrawable(xw.draw), 0, NULL);
	/* Retry GC allocation on the next draw; draw text without underlines for now. */
	if ((base.mode & (ATTR_UNDERLINE | ATTR_URL)) && dc.ugc) {
		const int widthThreshold  = 28;
		int wlw = (win.ch / widthThreshold) + 1;
		unsigned long linecolor = fg->pixel;
		if (!(base.mode & ATTR_BLINK && win.mode & MODE_BLINK) &&
		    !(base.mode & ATTR_INVISIBLE))
			linecolor = xunderlinepixel(base.ucolor, linecolor);

		XGCValues ugcv = {
			.foreground = linecolor,
			.line_width = wlw,
			.line_style = LineSolid,
			.cap_style = CapNotLast
		};

		XChangeGC(xw.dpy, dc.ugc,
			GCForeground | GCLineWidth | GCLineStyle | GCCapStyle, &ugcv);
		GC ugc = dc.ugc;

		if (ustyle != 3) {
			int thickness = MIN(wlw, MAX(1, win.ch / 3));
			int uy = MIN(dc.font.ascent + 1, win.ch - thickness);

			if (ustyle == 2 && win.ch >= 3) {
				/* Keep both strokes and their gap inside the cell. */
				uy = MIN(uy, win.ch - 3 * thickness);
				XFillRectangle(xw.dpy, XftDrawDrawable(xw.draw), ugc,
					winx, winy + uy, width, thickness);
				XFillRectangle(xw.dpy, XftDrawDrawable(xw.draw), ugc,
					winx, winy + uy + 2 * thickness, width, thickness);
			} else if (ustyle == 4 || ustyle == 5) {
				int dash = (ustyle == 4 ? 1 : 3) * thickness;
				int period = dash + (ustyle == 4 ? 1 : 2) * thickness;
				int offset, phase, span;

				/* Align the pattern across glyph runs and partial redraws. */
				for (offset = 0; offset < width; offset += span) {
					phase = (winx - borderpx + offset) % period;
					span = MIN(width - offset,
						(phase < dash ? dash : period) - phase);
					if (phase < dash)
						XFillRectangle(xw.dpy, XftDrawDrawable(xw.draw), ugc,
							winx + offset, winy + uy, span, thickness);
				}
			} else {
				XFillRectangle(xw.dpy, XftDrawDrawable(xw.draw), ugc,
					winx, winy + uy, width, thickness);
			}
		} else if (ustyle == 3 && charlen > 0) {
			int ww = win.cw;
			int wh = MAX(1, dc.font.descent - wlw/2 - 1);
			int wx = winx;
			int wy = winy + win.ch - dc.font.descent;

#if UNDERCURL_STYLE == UNDERCURL_CURLY
			int narcs = charlen * 2 + 1;
			XArc *arcs = xmalloc(sizeof(XArc) * narcs);

			int i = 0;
			for (i = 0; i < charlen-1; i++) {
				arcs[i*2] = (XArc) {
					.x = xshort(wx + win.cw * i + ww / 4),
					.y = xshort(wy),
					.width = win.cw / 2,
					.height = wh,
					.angle1 = 0,
					.angle2 = 180 * 64
				};
				arcs[i*2+1] = (XArc) {
					.x = xshort(wx + win.cw * i + ww * 0.75),
					.y = xshort(wy),
					.width = win.cw/2,
					.height = wh,
					.angle1 = 180 * 64,
					.angle2 = 180 * 64
				};
			}
			// Last wave
			arcs[i*2] = (XArc) {xshort(wx + ww * i + ww / 4), xshort(wy), ww / 2, wh,
			0, 180 * 64 };
			// Last wave tail
			arcs[i*2+1] = (XArc) {xshort(wx + ww * i + ww * 0.75), xshort(wy), ceil(ww / 2.),
			wh, 180 * 64, 90 * 64};
			// First wave tail
			i++;
			arcs[i*2] = (XArc) {xshort(wx - ww/4 - 1), xshort(wy), ceil(ww / 2.), wh, 270 * 64,
			90 * 64 };

			int batch = xrequestitems(3);

			for (i = 0; i < narcs; i += batch)
				XDrawArcs(xw.dpy, XftDrawDrawable(xw.draw), ugc, arcs + i,
				          MIN(narcs - i, batch));

			free(arcs);
#elif UNDERCURL_STYLE == UNDERCURL_SPIKY
			wh *= 2;

			/* Use 45-degree slopes. */
			ww = wh;

			/* Align waves across glyph runs. */
			wx = (wx / (ww/2)) * (ww/2);

			int marginStart = winx - wx;

			float n = width;
			n = (n / ww) * 2;
			n += 2;
			int npoints = n;

			float waveLength = 0;

			if (npoints >= 3) {
				/* Reserve one extra point to reach the run boundary. */
				XPoint *points = xmalloc(sizeof(XPoint) * (npoints + 1));

				/* Clip the first segment to the glyph run. */
				points[0] = (XPoint) {
					.x = xshort(wx + marginStart),
					.y = xshort((isSlopeRising(wx, 0, ww))
						? (wy - marginStart + ww/2.f)
						: (wy + marginStart))
				};

				points[1] = (XPoint) {
					.x = xshort((ww/2.f) - marginStart),
					.y = xshort((isSlopeRising(wx, 1, ww))
						? (ww/2.f - marginStart)
						: (-ww/2.f + marginStart))
				};
				waveLength += (ww/2.f) - marginStart;

				for (int i = 2; i < npoints-1; i++) {
					points[i] = (XPoint) {
						.x = xshort(ww/2),
						.y = xshort((isSlopeRising(wx, i, ww))
							? wh/2
							: -wh/2)
					};
					waveLength += ww/2;
				}

				points[npoints-1] = (XPoint) {
					.x = xshort(ww/2),
					.y = xshort((isSlopeRising(wx, npoints-1, ww))
						? wh/2
						: -wh/2)
				};
				waveLength += ww/2;

				if (waveLength < width) { /* Extend to the run boundary. */
					int marginEnd = width - waveLength;
					points[npoints] = (XPoint) {
						.x = xshort(marginEnd),
						.y = xshort((isSlopeRising(wx, npoints, ww))
							? (marginEnd)
							: (-marginEnd))
					};

					npoints++;
				} else if (waveLength > width) { /* Clip to the run boundary. */
					int marginEnd = waveLength - width;
					points[npoints-1].x = xshort(points[npoints-1].x - marginEnd);
					if (isSlopeRising(wx, npoints-1, ww))
						points[npoints-1].y = xshort(points[npoints-1].y - marginEnd);
					else
						points[npoints-1].y = xshort(points[npoints-1].y + marginEnd);
				}

				xdrawpolyline(ugc, points, npoints, CoordModePrevious);

				/* Thicken the stroke for taller cells. */
				if ( ((win.ch / (widthThreshold/2)) % 2)) {
					points[0].x = xshort(points[0].x + 1);

					xdrawpolyline(ugc, points, npoints, CoordModePrevious);
				}

				free(points);
			}
#else
			float capRatio = 0.5f;

			wh *= 2;

			/* Use 45-degree slopes. */
			ww = wh;
			ww *= 1 + capRatio;

			/* Align waves across glyph runs. */
			wx = (wx / ww) * ww;

			float marginStart;
			switch(getSlope(winx, 0, ww)) {
				case UNDERCURL_SLOPE_ASCENDING:
					marginStart = winx - wx;
					break;
				case UNDERCURL_SLOPE_TOP_CAP:
					marginStart = winx - (wx + (ww * (2.f/6.f)));
					break;
				case UNDERCURL_SLOPE_DESCENDING:
					marginStart = winx - (wx + (ww * (3.f/6.f)));
					break;
				case UNDERCURL_SLOPE_BOTTOM_CAP:
					marginStart = winx - (wx + (ww * (5.f/6.f)));
					break;
			}

			float n = width;
			n = (n / ww) * 4;
			n += 2;
			int npoints = n;

			float penX = 0;
			float penY = 0;

			if (npoints >= 3) {
				XPoint *points = xmalloc(sizeof(XPoint) * (npoints + 1));

				/* Clip the first segment to the glyph run. */
				penX = winx;
				switch (getSlope(winx, 0, ww)) {
					case UNDERCURL_SLOPE_ASCENDING:
						penY = wy + wh/2.f - marginStart;
						break;
					case UNDERCURL_SLOPE_TOP_CAP:
						penY = wy;
						break;
					case UNDERCURL_SLOPE_DESCENDING:
						penY = wy + marginStart;
						break;
					case UNDERCURL_SLOPE_BOTTOM_CAP:
						penY = wy + wh/2.f;
						break;
				}
				points[0].x = xshort(penX);
				points[0].y = xshort(penY);

				switch (getSlope(winx, 1, ww)) {
					case UNDERCURL_SLOPE_ASCENDING:
						penX += ww * (1.f/6.f) - marginStart;
						penY += 0;
						break;
					case UNDERCURL_SLOPE_TOP_CAP:
						penX += ww * (2.f/6.f) - marginStart;
						penY += -wh/2.f + marginStart;
						break;
					case UNDERCURL_SLOPE_DESCENDING:
						penX += ww * (1.f/6.f) - marginStart;
						penY += 0;
						break;
					case UNDERCURL_SLOPE_BOTTOM_CAP:
						penX += ww * (2.f/6.f) - marginStart;
						penY += -marginStart + wh/2.f;
						break;
				}
				points[1].x = xshort(penX);
				points[1].y = xshort(penY);

				for (int i = 2; i < npoints; i++) {
					switch (getSlope(winx, i, ww)) {
						case UNDERCURL_SLOPE_ASCENDING:
						case UNDERCURL_SLOPE_DESCENDING:
							penX += ww * (1.f/6.f);
							penY += 0;
							break;
						case UNDERCURL_SLOPE_TOP_CAP:
							penX += ww * (2.f/6.f);
							penY += -wh / 2.f;
							break;
						case UNDERCURL_SLOPE_BOTTOM_CAP:
							penX += ww * (2.f/6.f);
							penY += wh / 2.f;
							break;
					}
					points[i].x = xshort(penX);
					points[i].y = xshort(penY);
				}

				float waveLength = penX - winx;
				if (waveLength < width) { /* Extend to the run boundary. */
					int marginEnd = width - waveLength;
					penX += marginEnd;
					switch(getSlope(winx, npoints, ww)) {
						case UNDERCURL_SLOPE_ASCENDING:
						case UNDERCURL_SLOPE_DESCENDING:
							break;
						case UNDERCURL_SLOPE_TOP_CAP:
							penY += -marginEnd;
							break;
						case UNDERCURL_SLOPE_BOTTOM_CAP:
							penY += marginEnd;
							break;
					}

					points[npoints].x = xshort(penX);
					points[npoints].y = xshort(penY);

					npoints++;
				} else if (waveLength > width) { /* Clip to the run boundary. */
					int marginEnd = waveLength - width;
					points[npoints-1].x = xshort(points[npoints-1].x - marginEnd);
					switch(getSlope(winx, npoints-1, ww)) {
						case UNDERCURL_SLOPE_TOP_CAP:
							points[npoints-1].y = xshort(points[npoints-1].y + marginEnd);
							break;
						case UNDERCURL_SLOPE_BOTTOM_CAP:
							points[npoints-1].y = xshort(points[npoints-1].y - marginEnd);
							break;
						default:
							break;
					}
				}

				xdrawpolyline(ugc, points, npoints, CoordModeOrigin);

				/* Thicken the stroke for taller cells. */
				if ( ((win.ch / (widthThreshold/2)) % 2)) {
					for (int i = 0; i < npoints; i++)
						points[i].x = xshort(points[i].x + 1);

					xdrawpolyline(ugc, points, npoints, CoordModeOrigin);
				}

				free(points);
			}
#endif
		}
	}

	if (base.mode & ATTR_STRUCK) {
		XftDrawRect(xw.draw, fg, winx, winy + 2 * dc.font.ascent * chscale / 3,
				width, 1);
	}

	/* Reset clip to none. */
	XftDrawSetClip(xw.draw, 0);
}

void
xdrawglyph(Glyph g, int x, int y)
{
	int numspecs;
	XftGlyphFontSpec spec;

	if (turl(x, y))
		g.mode |= ATTR_URL;
	numspecs = xmakeglyphfontspecs(&spec, &g, 1, x, y);
	xdrawglyphfontspecs(&spec, g, numspecs, x, y);
}

void
xdrawcursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og)
{
	Color drawcol;

	/* remove the old cursor */
	if (selected(ox, oy))
		og.mode ^= ATTR_REVERSE;
	xdrawglyph(og, ox, oy);

	if (IS_SET(MODE_HIDE))
		return;

	/*
	 * Select the right color for the right mode.
	 */
	g.mode &= ATTR_BOLD|ATTR_ITALIC|ATTR_UNDERLINE|ATTR_USTYLE|ATTR_STRUCK|ATTR_WIDE|
	          ATTR_ALTPRES;

	if (IS_SET(MODE_REVERSE)) {
		g.mode |= ATTR_REVERSE;
		g.bg = defaultfg;
		if (selected(cx, cy)) {
			drawcol = dc.col[defaultcs];
			g.fg = defaultrcs;
		} else {
			drawcol = dc.col[defaultrcs];
			g.fg = defaultcs;
		}
	} else {
		if (selected(cx, cy)) {
			g.fg = defaultfg;
			g.bg = defaultrcs;
		} else {
			g.fg = defaultbg;
			g.bg = defaultcs;
		}
		drawcol = dc.col[g.bg];
	}

	/* draw the new one */
	if (IS_SET(MODE_FOCUSED)) {
		switch (win.cursor) {
		default:
		case 0: /* blinking block */
		case 1: /* blinking block (default) */
			if (IS_SET(MODE_BLINK))
				break;
			/* FALLTHROUGH */
		case 2: /* steady block */
			xdrawglyph(g, cx, cy);
			break;
		case 3: /* blinking underline */
			if (IS_SET(MODE_BLINK))
				break;
			/* FALLTHROUGH */
		case 4: /* steady underline */
			XftDrawRect(xw.draw, &drawcol,
					borderpx + cx * win.cw,
					borderpx + (cy + 1) * win.ch - \
						cursorthickness,
					win.cw, cursorthickness);
			break;
		case 5: /* blinking bar */
			if (IS_SET(MODE_BLINK))
				break;
			/* FALLTHROUGH */
		case 6: /* steady bar */
			XftDrawRect(xw.draw, &drawcol,
					borderpx + cx * win.cw,
					borderpx + cy * win.ch,
					cursorthickness, win.ch);
			break;
		case 7: /* blinking phist cursor */
			if (IS_SET(MODE_BLINK))
				break;
			/* FALLTHROUGH */
		case 8: /* steady phist cursor */
			g.u = stcursor;
			xdrawglyph(g, cx, cy);
			break;
		}
	} else {
		XftDrawRect(xw.draw, &drawcol,
				borderpx + cx * win.cw,
				borderpx + cy * win.ch,
				win.cw - 1, 1);
		XftDrawRect(xw.draw, &drawcol,
				borderpx + cx * win.cw,
				borderpx + cy * win.ch,
				1, win.ch - 1);
		XftDrawRect(xw.draw, &drawcol,
				borderpx + (cx + 1) * win.cw - 1,
				borderpx + cy * win.ch,
				1, win.ch - 1);
		XftDrawRect(xw.draw, &drawcol,
				borderpx + cx * win.cw,
				borderpx + (cy + 1) * win.ch - 1,
				win.cw, 1);
	}
}

void
xsetenv(void)
{
	char buf[sizeof(long) * 8 + 1];

	snprintf(buf, sizeof(buf), "%lu", xw.win);
	setenv("WINDOWID", buf, 1);
}

void
xseticontitle(char *p)
{
	XTextProperty prop;
	DEFAULT(p, opt_title);

	if (p[0] == '\0')
		p = opt_title;

	if (Xutf8TextListToTextProperty(xw.dpy, &p, 1, XUTF8StringStyle,
	                                &prop) != Success)
		return;
	XSetWMIconName(xw.dpy, xw.win, &prop);
	XSetTextProperty(xw.dpy, xw.win, &prop, xw.netwmiconname);
	XFree(prop.value);
}

void
xsettitle(char *p)
{
	XTextProperty prop;
	DEFAULT(p, opt_title);

	if (p[0] == '\0')
		p = opt_title;

	if (Xutf8TextListToTextProperty(xw.dpy, &p, 1, XUTF8StringStyle,
	                                &prop) != Success)
		return;
	XSetWMName(xw.dpy, xw.win, &prop);
	XSetTextProperty(xw.dpy, xw.win, &prop, xw.netwmname);
	XFree(prop.value);
}

int
xstartdraw(void)
{
	return IS_SET(MODE_VISIBLE);
}

void
xdrawline(Line line, int x1, int y1, int x2)
{
	int i, x, ox, numspecs;
	int url = 0, newurl;
	Glyph base, new;
	XftGlyphFontSpec *specs = xw.specbuf;

	numspecs = xmakeglyphfontspecs(specs, &line[x1], x2 - x1, x1, y1);
	i = ox = 0;
	for (x = x1; x < x2 && i < numspecs; x++) {
		new = line[x];
		if (new.mode & ATTR_WDUMMY)
			continue;
		newurl = turl(x, y1);
		if (newurl)
			new.mode |= ATTR_URL;
		if (selected(x, y1))
			new.mode ^= ATTR_REVERSE;
		if (i > 0 && (ATTRCMP(base, new) || url != newurl)) {
			xdrawglyphfontspecs(specs, base, i, ox, y1);
			specs += i;
			numspecs -= i;
			i = 0;
		}
		if (i == 0) {
			ox = x;
			base = new;
			url = newurl;
		}
		i++;
	}
	if (i > 0)
		xdrawglyphfontspecs(specs, base, i, ox, y1);
}

void
xfinishdraw(void)
{
	XCopyArea(xw.dpy, xw.buf, xw.win, dc.gc, 0, 0, win.w,
			win.h, 0, 0);
	XSetForeground(xw.dpy, dc.gc,
			dc.col[IS_SET(MODE_REVERSE)?
				defaultfg : defaultbg].pixel);
}

void
xximspot(int x, int y)
{
	if (xw.ime.xic == NULL)
		return;

	xw.ime.spot.x = borderpx + x * win.cw;
	xw.ime.spot.y = borderpx + (y + 1) * win.ch;

	XSetICValues(xw.ime.xic, XNPreeditAttributes, xw.ime.spotlist, NULL);
}

void
expose(XEvent *ev)
{
	/*
	 * A pending resize is applied once before run() draws the whole window,
	 * so drawing now would show a stale frame and reflowing would repeat.
	 */
	if (pendingw)
		return;
	redraw();
}

void
visibility(XEvent *ev)
{
	XVisibilityEvent *e = &ev->xvisibility;

	MODBIT(win.mode, e->state != VisibilityFullyObscured, MODE_VISIBLE);
}

void
unmap(XEvent *ev)
{
	win.mode &= ~MODE_VISIBLE;
}

void
xsetmode(int set, unsigned int flags)
{
	int mode = win.mode;
	MODBIT(win.mode, set, flags);
	if ((win.mode & MODE_REVERSE) != (mode & MODE_REVERSE))
		redraw();
}

int
xsetcursor(int cursor)
{
	if (!BETWEEN(cursor, 0, 8)) /* 7-8: phist extensions */
		return 1;
	/* DECSCUSR 0 requests the default style rather than a blinking block. */
	if (cursor == 0 && BETWEEN(cursorstyle, 1, 8))
		cursor = cursorstyle;
	win.cursor = cursor;
	cursorblinks = win.cursor == 0 || win.cursor == 1 ||
	               win.cursor == 3 || win.cursor == 5 ||
	               win.cursor == 7;
	return 0;
}

void
xseturgency(int add)
{
	XWMHints *h;

	if (!(h = XGetWMHints(xw.dpy, xw.win)))
		return;

	MODBIT(h->flags, add, XUrgencyHint);
	XSetWMHints(xw.dpy, xw.win, h);
	XFree(h);
}

void
xbell(void)
{
	if (!(IS_SET(MODE_FOCUSED)))
		xseturgency(1);
	if (bellvolume)
		XkbBell(xw.dpy, xw.win, bellvolume, (Atom)NULL);
}

void
focus(XEvent *ev)
{
	XFocusChangeEvent *e = &ev->xfocus;

	if (e->mode == NotifyGrab)
		return;

	if (ev->type == FocusIn) {
		if (xw.ime.xic)
			XSetICFocus(xw.ime.xic);
		win.mode |= MODE_FOCUSED;
		blinkreset(); /* show the cursor at once, then resume blinking */
		xseturgency(0);
		if (IS_SET(MODE_FOCUS))
			ttywrite("\033[I", 3, 0);
	} else {
		if (xw.ime.xic)
			XUnsetICFocus(xw.ime.xic);
		win.mode &= ~MODE_FOCUSED;
		if (IS_SET(MODE_FOCUS))
			ttywrite("\033[O", 3, 0);
	}
}

int
match(uint mask, uint state)
{
	return mask == XK_ANY_MOD || mask == (state & ~ignoremod);
}

char*
kmap(KeySym k, uint state)
{
	Key *kp;
	int i;

	/* Check for mapped keys out of X11 function keys. */
	for (i = 0; i < LEN(mappedkeys); i++) {
		if (mappedkeys[i] == k)
			break;
	}
	if (i == LEN(mappedkeys)) {
		if ((k & 0xFFFF) < 0xFD00)
			return NULL;
	}

	for (kp = key; kp < key + LEN(key); kp++) {
		if (kp->k != k)
			continue;

		if (!match(kp->mask, state))
			continue;

		if (IS_SET(MODE_APPKEYPAD) ? kp->appkey < 0 : kp->appkey > 0)
			continue;
		if (IS_SET(MODE_NUMLOCK) && kp->appkey == 2)
			continue;

		if (IS_SET(MODE_APPCURSOR) ? kp->appcursor < 0 : kp->appcursor > 0)
			continue;

		return kp->s;
	}

	return NULL;
}

void
kpress(XEvent *ev)
{
	XKeyEvent *e = &ev->xkey;
	KeySym ksym = NoSymbol;
	char buf[64], *customkey;
	int len;
	Rune c;
	Status status;
	Shortcut *bp;

	if (IS_SET(MODE_KBDLOCK))
		return;

	if (xw.ime.xic) {
		len = XmbLookupString(xw.ime.xic, e, buf, sizeof buf, &ksym, &status);
		if (status == XBufferOverflow)
			return;
	} else {
		len = XLookupString(e, buf, sizeof buf, &ksym, NULL);
	}
	/* 1. shortcuts */
	for (bp = shortcuts; bp < shortcuts + LEN(shortcuts); bp++) {
		if (ksym == bp->keysym && match(bp->mod, e->state)) {
			bp->func(&(bp->arg));
			return;
		}
	}

	/* 2. custom keys from config.h */
	if ((customkey = kmap(ksym, e->state))) {
		sendinput(customkey, strlen(customkey), 1);
		return;
	}

	/* 3. composed string from input method */
	if (len == 0)
		return;
	if (len == 1 && e->state & Mod1Mask) {
		if (IS_SET(MODE_8BIT)) {
			if (*buf < 0177) {
				c = *buf | 0x80;
				len = utf8encode(c, buf);
			}
		} else {
			buf[1] = buf[0];
			buf[0] = '\033';
			len = 2;
		}
	}
	sendinput(buf, len, 1);
}

void
cmessage(XEvent *e)
{
	/*
	 * See xembed specs
	 *  http://standards.freedesktop.org/xembed-spec/xembed-spec-latest.html
	 */
	if (e->xclient.message_type == xw.xembed && e->xclient.format == 32) {
		if (e->xclient.data.l[1] == XEMBED_FOCUS_IN) {
			win.mode |= MODE_FOCUSED;
			xseturgency(0);
		} else if (e->xclient.data.l[1] == XEMBED_FOCUS_OUT) {
			win.mode &= ~MODE_FOCUSED;
		}
	} else if (e->xclient.data.l[0] == xw.wmdeletewin) {
		ttyhangup();
		exit(0);
	}
}

void
resize(XEvent *e)
{
	/*
	 * Resizing may reflow the whole history, so dragging a window edge
	 * applies only the latest size once drawing is due (see run()).
	 */
	pendingw = e->xconfigure.width;
	pendingh = e->xconfigure.height;
}

static void
applyresize(void)
{
	if (!pendingw || (pendingw == win.w && pendingh == win.h)) {
		pendingw = pendingh = 0;
		return;
	}
	cresize(pendingw, pendingh);
	pendingw = pendingh = 0;
}

void
run(void)
{
	XEvent ev;
	int w = win.w, h = win.h;
	fd_set rfd;
	int xfd = XConnectionNumber(xw.dpy), ttyfd, xev, drawing;
	struct timespec seltv, *tv, now, trigger;
	double timeout, urlremain;

	/* Waiting for window mapping */
	do {
		XNextEvent(xw.dpy, &ev);
		/*
		 * This XFilterEvent call is required because of XOpenIM. It
		 * does filter out the key event and some client message for
		 * the input method too.
		 */
		if (XFilterEvent(&ev, None))
			continue;
		if (ev.type == ConfigureNotify) {
			w = ev.xconfigure.width;
			h = ev.xconfigure.height;
		}
	} while (ev.type != MapNotify);

	ttyfd = ttynew(opt_line, shell, opt_io, opt_cmd, opt_dir);
	cresize(w, h);

	clock_gettime(CLOCK_MONOTONIC, &lastblink);
	for (timeout = -1, drawing = 0;;) {
		FD_ZERO(&rfd);
		FD_SET(ttyfd, &rfd);
		FD_SET(xfd, &rfd);

		if (XPending(xw.dpy))
			timeout = 0;  /* existing events might not set xfd */

		seltv.tv_sec = timeout / 1E3;
		seltv.tv_nsec = 1E6 * (timeout - 1E3 * seltv.tv_sec);
		tv = timeout >= 0 ? &seltv : NULL;

		if (pselect(MAX(xfd, ttyfd)+1, &rfd, NULL, NULL, tv, NULL) < 0) {
			if (errno == EINTR)
				continue;
			die("select failed: %s\n", strerror(errno));
		}
		clock_gettime(CLOCK_MONOTONIC, &now);

		if (FD_ISSET(ttyfd, &rfd))
			ttyread();

		xev = 0;
		while (XPending(xw.dpy)) {
			xev = 1;
			XNextEvent(xw.dpy, &ev);
			if (XFilterEvent(&ev, None))
				continue;
			if (handler[ev.type])
				(handler[ev.type])(&ev);
		}

		/*
		 * To reduce flicker and tearing, when new content or event
		 * triggers drawing, we first wait a bit to ensure we got
		 * everything, and if nothing new arrives - we draw.
		 * We start with trying to wait minlatency ms. If more content
		 * arrives sooner, we retry with shorter and shorter periods,
		 * and eventually draw even without idle after maxlatency ms.
		 * Typically this results in low latency while interacting,
		 * maximum latency intervals during `cat huge.txt`, and perfect
		 * sync with periodic updates from animations/key-repeats/etc.
		 */
		if (FD_ISSET(ttyfd, &rfd) || xev) {
			if (!drawing) {
				trigger = now;
				drawing = 1;
			}
			timeout = (maxlatency - TIMEDIFF(now, trigger)) \
			          / maxlatency * minlatency;
			if (timeout > 0)
				continue;  /* we have time, try to find idle */
		}

		/* idle detected or maxlatency exhausted -> draw */
		applyresize();
		timeout = blinkremaining(now);

		urlremain = urlflashremaining(now);
		if (urlremain > 0 && (timeout < 0 || urlremain < timeout))
			timeout = urlremain;

		draw();
		XFlush(xw.dpy);
		drawing = 0;
	}
}

void
usage(void)
{
	die("usage: %s [-aiv] [-c class] [-d path] [-f font]"
	    " [-g geometry] [-n name] [-o file]\n"
	    "          [-T title] [-t title] [-w windowid]"
	    " [[-e] command [args ...]]\n"
	    "       %s [-aiv] [-c class] [-d path] [-f font]"
	    " [-g geometry] [-n name] [-o file]\n"
	    "          [-T title] [-t title] [-w windowid] -l line"
	    " [stty_args ...]\n", argv0, argv0);
}

int
main(int argc, char *argv[])
{
	xw.l = xw.t = 0;
	xw.isfixed = False;
	xsetcursor(cursorstyle);

	ARGBEGIN {
	case 'a':
		allowaltscreen = 0;
		break;
	case 'c':
		opt_class = EARGF(usage());
		break;
	case 'd':
		opt_dir = EARGF(usage());
		break;
	case 'e':
		if (argc > 0)
			--argc, ++argv;
		goto run;
	case 'f':
		opt_font = EARGF(usage());
		break;
	case 'g':
		xw.gm = XParseGeometry(EARGF(usage()),
				&xw.l, &xw.t, &cols, &rows);
		break;
	case 'i':
		xw.isfixed = 1;
		break;
	case 'o':
		opt_io = EARGF(usage());
		break;
	case 'l':
		opt_line = EARGF(usage());
		break;
	case 'n':
		opt_name = EARGF(usage());
		break;
	case 't':
	case 'T':
		opt_title = EARGF(usage());
		break;
	case 'w':
		opt_embed = EARGF(usage());
		break;
	case 'v':
		die("%s " VERSION "\n", argv0);
		break;
	default:
		usage();
	} ARGEND;

run:
	if (argc > 0) /* eat all remaining arguments */
		opt_cmd = argv;

	if (!opt_title)
		opt_title = (opt_line || !opt_cmd) ? "phist" : opt_cmd[0];

	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");
	cols = MAX(cols, 1);
	rows = MAX(rows, 1);
	tnew(cols, rows);
	xinit(cols, rows);
	xsetenv();
	selinit();
	run();

	return 0;
}
