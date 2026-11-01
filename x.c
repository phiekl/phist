/* See LICENSE for license details. */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <poll.h>
#include <locale.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include <libgen.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
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
#define MOUSE_BUTTONS (Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask)

/* function definitions used in config.h */
static void clipcopy(const Arg *);
static void clippaste(const Arg *);
static void selpaste(const Arg *);
static void pastestart(Atom);
static void pasteclear(void);
static double pasteremaining(struct timespec);
static int pastekey(KeySym, uint);
static void pastedraw(void);
static void searchstart(const Arg *);
static void searchinsert(const char *, size_t);
static int searchkey(KeySym, uint, const char *, int);
static void searchdraw(void);
static void vimstart(const Arg *);
static int vimkey(KeySym, uint, const char *, int);
static void vimdraw(void);
static int urlboxmouse(XEvent *);
static int urlboxkey(KeySym, uint);
static void urlboxdraw(void);
static void urlboxopen(char *, int, int, int);
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
	Pixmap nextbuf; /* created by cresize() for xresize() */
	GlyphFontSpec *specbuf; /* font spec buffer used for rendering */
	Atom xembed, wmprotocols, wmdeletewin, netwmname, netwmiconname, netwmpid;
	Atom cwduri, utf8string;
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
	int colormapped; /* does a color's cell stay in use by what was drawn in it? */
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
static void xlimitcells(int *, int *);
static void xresize(int, int);
static void vbellfree(void);
static void xhints(void);
static int xloadcolor(int, const char *, Color *);
static int xloadfont(Font *, FcPattern *);
static int xloadfonts(const char *, double);
static XftFont *xloademojifont(int);
static FcPattern *xtextfallback(FcFontSet *, FcPattern *, Rune);
static FcPattern *xfallbackmatch(Font *, FcFontSet **, FcPattern *, Rune, int);
static void xunloadfont(Font *);
static void xunloadfonts(void);
static void xsetenv(void);
static void xseturgency(int);
static void xflushtitles(void);
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
static void xpropnotify(XEvent *);
static double selsendremaining(struct timespec);
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
	[PropertyNotify] = xpropnotify,
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
static uint mouseignored; /* buttons whose releases belong to a modal UI */
static uint mousepasting; /* paste bindings pressed under its failure box */

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

/* Fallback lookup scans this array, so clear it at FRC_MAX between glyph runs.
 * Keep uncovered characters in a separate table to avoid filling the font cache;
 * a collision in that table can require another font search. */
enum { FRC_MAX = 1024, FRC_UNCOVERED = -2, UNCOVERED_BITS = 14 };
static uint32_t uncovered[1 << UNCOVERED_BITS]; /* see xuncoveredkey() */
/* The largest cell size, and font ascent or descent, in pixels. */
enum { CELL_MAX = 4096 };
enum { CELLS_MAX = 4 << 20 }; /* of a screen, at 16 bytes each on two screens */
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
	int active, col, row, explicit, anchorcol, anchorrow, anchorendcol, anchorendrow;
	char *text;
} urlclick;
static int urlflashset;
static struct timespec lasturlflash;

static struct {
	char *text;
	int top, armed, flash;
	int col, width, height, y;
	int anchorcol, anchorrow, anchorendrow;
	struct timespec flashed;
} urlbox;

static void
urlboxclose(void)
{
	free(urlbox.text);
	urlbox.text = NULL;
	urlbox.flash = urlbox.armed = 0;
}

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
	double remain = -1, boxremain;

	if (urlflashset) {
		remain = urlflashtimeout - TIMEDIFF(now, lasturlflash);
		if (remain <= 0) {
			urlflashset = 0;
			tclearurlflash();
			remain = -1;
		}
	}
	if (urlbox.text && urlbox.flash) {
		boxremain = urlflashtimeout - TIMEDIFF(now, urlbox.flashed);
		if (boxremain <= 0)
			urlboxclose();
		else
			remain = remain < 0 ? boxremain : MIN(remain, boxremain);
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
	pastestart(XInternAtom(xw.dpy, "CLIPBOARD", 0));
}

void
selpaste(const Arg *dummy)
{
	pastestart(XA_PRIMARY);
}

/* What to draw with while a full colormap has no room for a color, keeping
 * light on dark and dark on light. Allocating is tried again the next time. */
static Color *
xnearestcolor(const XRenderColor *value)
{
	return &dc.col[0.2126 * value->red + 0.7152 * value->green +
	               0.0722 * value->blue >= 0x8000 ? 15 : 0];
}

/* Cache a successful allocation; use the fallback and retry after failure. */
static Color *
xlazycolor(Color *color, int *loaded, const XRenderColor *value, Color *fallback)
{
	if (!*loaded)
		*loaded = XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, value, color);
	return *loaded ? color : fallback;
}

#include "paste.c"

static char searchtext[SEARCH_MAX];
static size_t searchlen;

static void
searchstart(const Arg *arg)
{
	(void)arg;
	pasteclear();
	searchlen = 0;
	searchtext[0] = '\0';
	tsearchstart();
	redraw();
}

static void
searchinsert(const char *text, size_t len)
{
	Rune u;
	size_t i, n, before = searchlen;

	for (i = 0; i < len; i += n) {
		n = pasteutf8(text + i, len - i, &u);
		if (!n) {
			n = 1;
			continue;
		}
		if (u < 0x20 || BETWEEN(u, 0x7f, 0x9f))
			continue;
		if (n >= sizeof(searchtext) - searchlen)
			break;
		memcpy(searchtext + searchlen, text + i, n);
		searchlen += n;
	}
	if (searchlen == before)
		return;
	searchtext[searchlen] = '\0';
	tsearchset(searchtext);
	redraw();
}

/* Search consumes keys before the ordinary shortcuts and tty mappings.
 * Clipboard and font bindings still work; pasted data goes to the query. */
static int
searchkey(KeySym key, uint state, const char *text, int len)
{
	Shortcut *bp;
	uint mods = state & ~(ignoremod | LockMask);
	/* Caps Lock reports Ctrl+C as XK_C, for example. */
	KeySym lower = key >= XK_A && key <= XK_Z ? key + XK_a - XK_A : key;
	int cancel = key == XK_Escape || (mods == ControlMask && lower == XK_c);

	if (!tsearchactive())
		return 0;
	if (cancel || ((key == XK_Return || key == XK_KP_Enter) && !mods)) {
		pasteclear();
		tsearchend(cancel);
		redraw();
		return 1;
	}
	for (bp = shortcuts; bp < shortcuts + LEN(shortcuts); bp++) {
		if ((key == bp->keysym || lower == bp->keysym) &&
		    match(bp->mod, state & ~LockMask) &&
		    (pasteaction(bp->func) || bp->func == clipcopy ||
		     bp->func == zoom || bp->func == zoomreset)) {
			bp->func(&bp->arg);
			return 1;
		}
	}
	if ((mods == 0 && (key == XK_Up || key == XK_Down ||
	                  key == XK_KP_Up || key == XK_KP_Down)) ||
	    (mods == ControlMask && (lower == XK_p || lower == XK_n)) ||
	    (mods == ShiftMask && (key == XK_Return || key == XK_KP_Enter))) {
		tsearchnext(key == XK_Down || key == XK_KP_Down || lower == XK_n ? 1 : -1);
		redraw();
		return 1;
	}
	if (mods == 0 && (key == XK_Home || key == XK_End ||
	                  key == XK_KP_Home || key == XK_KP_End)) {
		tsearchedge(key == XK_End || key == XK_KP_End);
		redraw();
		return 1;
	}
	if (mods == 0 && (key == XK_Prior || key == XK_Next ||
	                  key == XK_KP_Prior || key == XK_KP_Next)) {
		tsearchpage(key == XK_Next || key == XK_KP_Next ? 1 : -1);
		redraw();
		return 1;
	}
	if ((key == XK_BackSpace || key == XK_Delete || key == XK_KP_Delete) && !mods) {
		if (searchlen) {
			do {
				searchlen--;
			} while (searchlen && ((uchar)searchtext[searchlen] & 0xc0) == 0x80);
			searchtext[searchlen] = '\0';
			tsearchset(searchtext);
		}
	} else if (mods == ControlMask && lower == XK_u) {
		searchlen = 0;
		searchtext[0] = '\0';
		tsearchset(searchtext);
	} else if (!(mods & ~(ShiftMask | Mod5Mask | XK_SWITCH_MOD)) && len > 0) {
		/* AltGr commonly uses Mod5 for third/fourth-level text. */
		searchinsert(text, len);
		return 1;
	}
	redraw();
	return 1;
}

/* Key caps in the search bar, in groups for the same action. */
static const char *searchkeys[][2] = {
	{ "\u2191", "\u2193" }, { "PgUp", "PgDn" }, { "Home", "End" }, { "Enter" },
	{ "Esc" },
};

/* How far the bar's field reaches above a row of text, how far the bar extends
 * past the field, and its height: like the paste preview's field, unless the
 * window is too short for that. */
static void
searchbarsize(int *ft, int *b, int *h)
{
	*ft = pastefieldtop();
	*b = MAX(2, win.ch / 4);
	*h = *ft + win.ch + 2 * *b;
	if (*h > win.h)
		*ft = *b = 0, *h = win.ch;
}

static XRenderColor
rgbvalue(unsigned int rgb)
{
	return (XRenderColor){ .red = ((rgb >> 16) & 255) * 257,
	                       .green = ((rgb >> 8) & 255) * 257,
	                       .blue = (rgb & 255) * 257, .alpha = 0xffff };
}

static uint
rgbtruecolor(unsigned int rgb)
{
	return TRUECOLOR((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

/* A color from config.h to draw text in, allocated once. */
static Color *
rgbcolor(unsigned int rgb)
{
	static struct RgbColor {
		struct RgbColor *next;
		unsigned int rgb;
		Color color;
	} *colors;
	struct RgbColor *c;
	XRenderColor value = rgbvalue(rgb);

	for (c = colors; c; c = c->next)
		if (c->rgb == rgb)
			return &c->color;
	c = xmalloc(sizeof(*c));
	if (!XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &value, &c->color)) {
		free(c);
		return xnearestcolor(&value);
	}
	c->rgb = rgb;
	c->next = colors;
	colors = c;
	return &c->color;
}

/* Convert to XRender's signed 16.16 format, clamping before conversion to
 * avoid overflow in windows wider than its coordinate range. */
static XFixed
xfixed(double v)
{
	if (!(v > -32768))
		return INT_MIN;
	return v >= 32768 ? INT_MAX : XDoubleToFixed(v);
}

/* Create a horizontal gradient; return 0 if clamping collapses its endpoints. */
static int
xgradientline(XLinearGradient *line, double x1, double x2)
{
	line->p1 = (XPointFixed){ xfixed(x1), 0 };
	line->p2 = (XPointFixed){ xfixed(x2), 0 };
	return line->p1.x < line->p2.x;
}

/* Fill a strip between two columns of pixels with a gradient between colors. */
static void
rgbfade(const XRenderColor *from, const XRenderColor *to, int x1, int x2, int y, int h)
{
	XLinearGradient line;
	XFixed stops[] = { 0, XDoubleToFixed(1) };
	XRenderColor colors[] = { *from, *to };
	Picture gradient;

	if (!xgradientline(&line, x1, x2))
		return;
	gradient = XRenderCreateLinearGradient(xw.dpy, &line, stops, colors, LEN(stops));
	XRenderComposite(xw.dpy, PictOpOver, gradient, None, XftDrawPicture(xw.draw),
	                 x1, y, 0, 0, x1, y, x2 - x1, h);
	XRenderFreePicture(xw.dpy, gradient);
}

/* The target in a single row as tall as the search bar, starting just below
 * the label, or as far along as lets it fit. A target longer than the window
 * shows a window's width at a time, with wheel/key scrolling. */
static void
urlboxlayout(void)
{
	int cols = MAX(1, win.tw / win.cw), rows = MAX(1, win.th / win.ch);
	int ft, b, len = strlen(urlbox.text), anchorrow, anchorendrow;

	urlbox.width = MAX(1, MIN(cols, len));
	LIMIT(urlbox.top, 0, MAX(0, DIVCEIL(len, urlbox.width) - 1));
	anchorrow = MAX(0, MIN(urlbox.anchorrow, rows - 1));
	anchorendrow = MAX(anchorrow, MIN(urlbox.anchorendrow, rows - 1));
	/* Only the last row of a wrapped label is above the bar. */
	urlbox.col = anchorendrow > anchorrow ? 0 : urlbox.anchorcol;
	LIMIT(urlbox.col, 0, cols - urlbox.width);
	searchbarsize(&ft, &b, &urlbox.height);
	urlbox.y = borderpx + (anchorendrow + 1) * win.ch;
	if (urlbox.y + urlbox.height > win.h)
		urlbox.y = borderpx + anchorrow * win.ch - urlbox.height;
	LIMIT(urlbox.y, 0, MAX(0, win.h - urlbox.height));
}

static void
urlboxopen(char *text, int col, int row, int endrow)
{
	pasteclear();
	free(urlbox.text);
	memset(&urlbox, 0, sizeof(urlbox));
	urlbox.text = text;
	urlbox.anchorcol = col;
	urlbox.anchorrow = row;
	urlbox.anchorendrow = endrow;
	urlboxlayout();
	redraw();
}

static int
urlboxhit(XEvent *e)
{
	int col = (e->xbutton.x - borderpx) / win.cw - urlbox.col;

	return e->xbutton.x >= borderpx + urlbox.col * win.cw &&
	       BETWEEN(e->xbutton.y, urlbox.y, urlbox.y + urlbox.height - 1) &&
	       BETWEEN(col, 0, urlbox.width - 1) &&
	       (size_t)(urlbox.top * urlbox.width + col) < strlen(urlbox.text);
}

static void
urlboxactivate(Time time)
{
	if (urlbox.flash)
		return;
	setsel(xstrdup(urlbox.text), time);
	clipcopy(NULL);
	if (urlflashtimeout) {
		urlbox.flash = 1;
		clock_gettime(CLOCK_MONOTONIC, &urlbox.flashed);
	} else
		urlboxclose();
}

static int
urlboxmouse(XEvent *e)
{
	int btn = e->xbutton.button;

	if (!urlbox.text)
		return 0;
	urlboxlayout();
	if (e->type == ButtonPress) {
		mouseignored |= buttonmask(btn);
		if (btn == Button4 || btn == Button5) {
			urlbox.top += btn == Button4 ? -1 : 1;
			urlbox.armed = 0;
		} else if (btn == Button1 && urlboxhit(e))
			urlbox.armed = 1;
		else
			urlboxclose();
	} else if (e->type == ButtonRelease) {
		mouseignored &= ~buttonmask(btn);
		if (btn == Button1 && urlbox.armed) {
			if (urlboxhit(e))
				urlboxactivate(e->xbutton.time);
			else
				urlboxclose();
		}
		urlbox.armed = 0;
	}
	redraw();
	XFlush(xw.dpy);
	return 1;
}

static int
urlboxkey(KeySym key, uint state)
{
	uint mods = state & ~(ignoremod | LockMask | MOUSE_BUTTONS);
	int scrolls;

	if (!urlbox.text)
		return 0;
	/* Only plain keys act on the bar, and only scroll a target that needs it. */
	scrolls = !mods && strlen(urlbox.text) > (size_t)urlbox.width;
	if (!mods && (key == XK_Return || key == XK_KP_Enter)) {
		urlboxactivate(CurrentTime);
		redraw();
		XFlush(xw.dpy);
		return 1;
	}
	if (key == XK_Escape) {
		urlboxclose();
		redraw();
		return 1;
	}
	if (scrolls && (key == XK_Up || key == XK_Prior || key == XK_KP_Up || key == XK_KP_Prior))
		urlbox.top--;
	else if (scrolls && (key == XK_Down || key == XK_Next || key == XK_KP_Down || key == XK_KP_Next))
		urlbox.top++;
	else if (!IsModifierKey(key)) {
		urlboxclose();
		redraw();
		/* Continue through normal shortcuts and input, even without text. */
		return 0;
	}
	redraw();
	return 1;
}

/* The bar is solid behind the target and a cell either side, and fades out
 * towards the window's edges. */
static void
urlboxdraw(void)
{
	char line[2049];
	int ft, b, h, x, y, n, solid, end;
	size_t off;
	unsigned int fg = urlbox.flash ? urlboxflashtextcolor : urlboxtextcolor;
	XRenderColor text = rgbvalue(fg);
	XRenderColor bar = rgbvalue(urlbox.flash ? urlboxflashcolor : urlboxcolor);
	XRenderColor faded = bar;

	if (!urlbox.text)
		return;
	urlboxlayout();
	searchbarsize(&ft, &b, &h);
	off = (size_t)urlbox.top * urlbox.width;
	n = MIN((size_t)urlbox.width, strlen(urlbox.text) - off);
	memcpy(line, urlbox.text + off, n);
	line[n] = '\0';
	x = borderpx + urlbox.col * win.cw;
	y = urlbox.y + b + ft;
	solid = MAX(0, x - win.cw);
	end = MIN(win.w, x + (n + 1) * win.cw);
	faded.alpha = MAX(0, MIN(1, urlboxfadeopacity)) * 0xffff;
	if (solid > 0)
		rgbfade(&faded, &bar, 0, solid, urlbox.y, h);
	pasteblend(&bar, 0xffff, solid, urlbox.y, end - solid, h);
	if (end < win.w)
		rgbfade(&bar, &faded, end, win.w, urlbox.y, h);
	pasteshift = y - borderpx;
	pastedrawline(line, urlbox.col, 0, urlbox.width, rgbcolor(fg));
	pasteblend(&text, 0xffff, x, y + win.ch - 2, n * win.cw, 1);
	pasteshift = 0;
}

/* Columns group i takes: each key cap with a cell on either side, a cell
 * apart. Draw it at col if asked, as tall as the field. */
static int
searchcaps(int i, int col, int draw, Color *fg)
{
	int k, w, x = col;
	XRenderColor fill = rgbvalue(searchkeycolor);

	for (k = 0; k < 2 && searchkeys[i][k]; k++) {
		w = pastewidth(searchkeys[i][k]);
		if (draw) {
			pastedrawkey(&fill, x + 1, w, 0, 1);
			pastedrawline(searchkeys[i][k], x + 1, 0, w, fg);
		}
		x += w + 3;
	}
	return x - 1 - col;
}

/* Draw the query, status and key caps in a padded search bar. Drop key groups
 * in priority order until the field fits its hint. Move the bar to the top
 * if it would cover the current match; pasteshift places text off the grid. */
static void
searchdraw(void)
{
	/* The order in which groups of keys are dropped. */
	static const int drop[] = { 3, 2, 1, 4, 0 };
	static const char hint[] = "Type to search scrollback ...";
	/* The field fits the hint and the padding either side of it. */
	enum { FIELDMIN = sizeof(hint) - 1 + 2, CAPGAP = 2 };
	const char *tail = searchtext;
	char status[32] = "", first[2] = { hint[0] };
	XRenderColor bar = rgbvalue(searchbarcolor);
	XRenderColor outline = rgbvalue(searchbaroutlinecolor);
	XRenderColor sunk = rgbvalue(searchfieldcolor);
	XRenderColor cursor = rgbvalue(searchcursorcolor);
	Rune u;
	size_t i, n;
	int cols = win.tw / win.cw, rows = win.th / win.ch;
	int shown[LEN(searchkeys)];
	int full, field, pad, fieldw, room, capsw, width = 0, statusw, count, index;
	int ft, b, h, top, y, x, k, covered = 0;

	if (!tsearchactive() || cols < 1 || rows < 1)
		return;
	/* A cell of padding at the right end, before the field and inside it,
	 * given room. */
	full = cols >= FIELDMIN + 2;
	field = pad = full;
	for (i = 0; i < LEN(shown); i++)
		shown[i] = 1;
	for (k = 0;; k++) {
		capsw = 0;
		for (i = 0; i < LEN(searchkeys); i++)
			if (shown[i])
				capsw += CAPGAP + searchcaps(i, 0, 0, NULL);
		fieldw = cols - field - pad - capsw;
		if (!full || fieldw >= FIELDMIN || k == LEN(drop))
			break;
		shown[drop[k]] = 0;
	}
	if (!full)
		capsw = 0, fieldw = cols;

	count = tsearchcount(&index);
	if (searchlen && count > 0)
		snprintf(status, sizeof(status), "%d/%d", index, count);
	else if (searchlen && count == 0)
		snprintf(status, sizeof(status), "No match");
	room = MAX(0, fieldw - 2 * pad - 1); /* less the cursor */
	statusw = strlen(status);
	if (statusw && room >= statusw + 5)
		room -= statusw + 1;
	else
		statusw = 0;
	for (i = 0; i < searchlen; i += n) {
		n = pasteutf8(searchtext + i, searchlen - i, &u);
		width += MAX(1, wcwidth(u));
	}
	while (width > room && *tail) {
		n = pasteutf8(tail, strlen(tail), &u);
		width -= MAX(1, wcwidth(u));
		tail += n;
	}

	searchbarsize(&ft, &b, &h);
	/* Move the bar to the top if it would cover the match, including in
	 * live output. */
	top = win.h - h;
	for (y = MAX(0, (top - borderpx) / win.ch); y < rows && !covered; y++)
		for (x = 0; x < cols && !covered; x++)
			covered = tsearchmatch(x, y) == SEARCH_CURRENT;
	if (covered)
		top = 0;
	y = top + b + ft;
	pasteshift = y - borderpx;

	pasteblend(&bar, 0xffff, 0, top, win.w, h);
	/* An outline on the side facing the terminal, as the boxes have. */
	pasteblend(&outline, 0x9000, 0, top ? top - 1 : h, win.w, 1);
	pasteblend(&sunk, 0x4000, borderpx + field * win.cw, y - ft,
	           fieldw * win.cw, win.ch + ft);
	x = field + pad + width;
	if (searchlen)
		pastedrawline(tail, field + pad, 0, room, rgbcolor(searchtextcolor));
	else
		pastedrawline(hint, x, 0, room + 1, rgbcolor(searchhintcolor));
	if (width < fieldw - 2 * pad) {
		pasteblend(&cursor, 0xffff, borderpx + x * win.cw, y, win.cw, win.ch);
		if (!searchlen)
			pastedrawline(first, x, 0, 1, rgbcolor(searchcursortextcolor));
	}
	if (statusw)
		pastedrawline(status, field + fieldw - pad - statusw, 0, statusw,
		              rgbcolor(count ? searchstatuscolor : searchnomatchcolor));
	for (i = 0, x = field + fieldw; i < LEN(searchkeys); i++) {
		if (shown[i] && capsw) {
			x += CAPGAP;
			x += searchcaps(i, x, 1, rgbcolor(searchkeytextcolor));
		}
	}
	pasteshift = 0;
}

/* Whether the bar with keyboard selection's mode shows. */
static int vimbar;

/* Keyboard selection takes every key, with those of Vim's own meaning
 * translated here; st.c does the rest. Only font bindings still work. */
static void
vimstart(const Arg *arg)
{
	(void)arg;
	pasteclear();
	urlboxclose();
	/* Discard a mouse gesture begun before keyboard selection took over. */
	mouseignored |= MOUSE_BUTTONS;
	mousepasting = 0;
	free(urlclick.text);
	memset(&urlclick, 0, sizeof(urlclick));
	tvimstart();
	vimbar = 1;
	redraw();
}

static int
vimkey(KeySym key, uint state, const char *text, int len)
{
	Shortcut *bp;
	uint mods = state & ~(ignoremod | LockMask);
	KeySym lower = key >= XK_A && key <= XK_Z ? key + XK_a - XK_A : key;
	Rune u = 0;
	int ctrl = 0, ctrlv, mode = tvimactive();

	if (!tvimactive() || tsearchactive() || pastemodal())
		return 0;
	/* Pressing Shift or Ctrl alone is not yet another key. */
	if (!IsModifierKey(key)) {
		ctrlv = lower == XK_v && mods == ControlMask;
		if (vimbar && !ctrlv) {
			vimbar = 0;
			redraw();
		}
		vimbar = ctrlv;
	}
	for (bp = shortcuts; bp < shortcuts + LEN(shortcuts); bp++) {
		if ((key != bp->keysym && lower != bp->keysym) ||
		    !match(bp->mod, state & ~LockMask))
			continue;
		if (bp->func == zoom || bp->func == zoomreset) {
			bp->func(&bp->arg);
			return 1;
		}
	}
	if (mods & (Mod1Mask | Mod4Mask))
		return 1;
	switch (key) {
	case XK_Escape: u = VIM_ESCAPE; break;
	case XK_Return: case XK_KP_Enter: u = VIM_ENTER; break;
	case XK_Left: case XK_KP_Left: u = VIM_LEFT; break;
	case XK_Down: case XK_KP_Down: u = VIM_DOWN; break;
	case XK_Up: case XK_KP_Up: u = VIM_UP; break;
	case XK_Right: case XK_KP_Right: u = VIM_RIGHT; break;
	case XK_Home: case XK_KP_Home: u = VIM_HOME; break;
	case XK_End: case XK_KP_End: u = VIM_END; break;
	case XK_Prior: case XK_KP_Prior: u = VIM_PAGEUP; break;
	case XK_Next: case XK_KP_Next: u = VIM_PAGEDOWN; break;
	/* A dead key on many layouts, without text of its own. */
	case XK_dead_circumflex: u = '^'; break;
	}
	if (!u && (mods & ControlMask)) {
		if (lower == XK_bracketleft)
			u = VIM_ESCAPE;
		else if (BETWEEN(lower, XK_a, XK_z))
			u = lower, ctrl = 1;
		else
			return 1;
	}
	if (!u && (!len || !pasteutf8(text, len, &u)))
		return 1;
	if (tvimkey(u, ctrl)) {
		searchstart(NULL);
	} else {
		if (tvimactive() != mode)
			vimbar = 1;
		redraw();
	}
	return 1;
}

/* A strip across the window, fading from alpha at the left. */
static void
vimfade(unsigned int rgb, unsigned short alpha, int y, int h)
{
	XRenderColor from = rgbvalue(rgb), to = from;

	from.alpha = alpha;
	to.alpha = alpha * MAX(0, MIN(1, vimlabelfadeopacity));
	rgbfade(&from, &to, 0, win.w, y, h);
}

/* A tint over the window while the mode lasts, under any search bar. The bar
 * with the mode's name is as tall as that one, and follows Ctrl+V or a change
 * of mode until the next key. */
static void
vimdraw(void)
{
	static const char *const names[] = {
		[VIM_NORMAL] = " NORMAL ", [VIM_VISUAL] = " VISUAL ",
		[VIM_LINE] = " V-LINE ", [VIM_BLOCK] = " V-BLOCK ",
	};
	int cols = win.tw / win.cw, rows = win.th / win.ch, mode = tvimactive();
	int ft, b, h, top;
	XRenderColor tint = rgbvalue(vimtintcolor);

	if (!mode || pastemodal())
		return;
	pasteblend(&tint, MAX(0, MIN(1, vimtintopacity)) * 0xffff, 0, 0, win.w, win.h);
	if (!vimbar || tsearchactive() || cols < 1 || rows < 1)
		return;
	searchbarsize(&ft, &b, &h);
	top = win.h - h;
	vimfade(vimlabelcolor, 0xffff, top, h);
	/* An outline on the side facing the terminal. */
	if (top)
		vimfade(vimlabeloutlinecolor, 0x9000, top - 1, 1);
	pasteshift = top + b + ft - borderpx;
	pastedrawline(names[mode], 0, 0, MIN(cols, (int)strlen(names[mode])),
	              rgbcolor(vimlabeltextcolor));
	pasteshift = 0;
}

void
zoom(const Arg *arg)
{
	Arg larg;

	larg.f = usedfontsize + arg->f;
	/* xloadfonts() takes a size of 1 or less to mean the font's own size. */
	if (larg.f > 1)
		zoomabs(&larg);
}

static void
xswapfonts(Font fonts[4])
{
	Font *loaded[] = { &dc.font, &dc.bfont, &dc.ifont, &dc.ibfont }, swap;
	int i;

	for (i = 0; i < LEN(loaded); i++) {
		swap = *loaded[i];
		*loaded[i] = fonts[i];
		fonts[i] = swap;
	}
}

/*
 * A size that cannot be loaded leaves the fonts as they are, even if they could
 * not be opened again.
 */
static void
xzoomfonts(double size)
{
	Font fonts[] = { dc.font, dc.bfont, dc.ifont, dc.ibfont };
	double current = usedfontsize, currentdefault = defaultfontsize;
	int cw = win.cw, ch = win.ch;

	if (xloadfonts(usedfont, size)) {
		xswapfonts(fonts);
		usedfontsize = current;
		defaultfontsize = currentdefault;
		win.cw = cw;
		win.ch = ch;
		return;
	}
	/* Unload the old fonts, with what was cached for them. */
	xswapfonts(fonts);
	xunloadfonts();
	xswapfonts(fonts);
}

void
zoomabs(const Arg *arg)
{
	xzoomfonts(arg->f);
	cresize(0, 0);
	redraw();
	xhints();
}

void
zoomreset(const Arg *arg)
{
	Arg larg;

	if (defaultfontsize > 0) {
		/* A default of 1 or less can only be asked for as the font's own. */
		larg.f = defaultfontsize > 1 ? defaultfontsize : 0;
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

	/* A fresh press starts a new gesture, even if an old release was lost. */
	mouseignored &= ~buttonmask(btn);
	if (urlboxmouse(e))
		return;
	/* The wheel scrolls history, also beneath alternate-screen programs
	 * and the paste boxes, but the text in the paste's full view. */
	if (btn == Button4 || btn == Button5) {
		Arg a;
		a.i = MAX(1, mousescrolllines);
		xsel.clicks = 0;
		/* Not even its release reaches a binding beneath them. */
		if (pastemodal() || tsearchactive() || tvimactive())
			mouseignored |= buttonmask(btn);
		if (pastewheel(btn == Button4 ? -a.i : a.i))
			return;
		if (btn == Button4) {
			kscrollup(&a);
		} else {
			kscrolldown(&a);
		}
		return;
	}

	/* The paste dialog and failure box are modal: clicks do nothing
	 * beneath them, not even on release, which could republish an old
	 * selection. A drag begun before them still ends. Under the failure
	 * box, a paste binding retries, and does nothing else. In the paste's
	 * full view, Button1 selects its text, ignored once the view closes. */
	if (pastemodal()) {
		if (paste.state != PASTE_CONFIRM && pastemousebinding(e)) {
			mousepasting |= buttonmask(btn);
			pastemouseaction(e, 0);
		} else {
			mouseignored |= buttonmask(btn);
			pastemouse(e);
		}
		return;
	}
	if (tsearchactive() || tvimactive()) {
		mouseignored |= buttonmask(btn);
		return;
	}

	if (btn != Button1)
		xsel.clicks = 0;

	if (btn == Button1 && match(XK_NO_MOD, e->xbutton.state & ~LockMask) &&
	    (urlclick.text = tgeturl(evcol(e), evrow(e)))) {
		xsel.clicks = 0;
		urlclick.active = 1;
		urlclick.explicit = thyperlink(evcol(e), evrow(e));
		urlclick.col = evcol(e);
		urlclick.row = evrow(e);
		urlclick.anchorcol = urlclick.col;
		urlclick.anchorrow = urlclick.row;
		urlclick.anchorendcol = urlclick.col;
		urlclick.anchorendrow = urlclick.row;
		if (urlclick.explicit)
			thyperlinkbounds(&urlclick.anchorcol, &urlclick.anchorrow,
			                &urlclick.anchorendcol, &urlclick.anchorendrow);
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
xclipcopy(void)
{
	clipcopy(NULL);
}

void
selclear_(XEvent *e)
{
	selclear();
}

/*
 * Selections too large for one request go to their requestors in
 * increments (ICCCM 2.7.2): phist announces INCR, writes each chunk once the
 * requestor deleted the one before, and ends with an empty one. The text is
 * copied, as the selection may change meanwhile. A transfer making no
 * progress for SELSEND_TIMEOUT ms is dropped. Any client can ask for any number
 * of them from any number of windows, so their copies are limited in total.
 */
enum { SELSEND_TIMEOUT = 5000, SELSEND_MAX = 16, SELSEND_MAXPERWINDOW = 4 };
#define SELSEND_MAXBYTES ((size_t)256 << 20)
typedef struct SelSend {
	Window requestor;
	Atom property, target;
	char *data;
	size_t len, pos;
	long mask; /* the requestor's events phist selected before */
	struct timespec progress;
	struct SelSend *next;
} SelSend;
static SelSend *selsends;
static int (*xerrorprev)(Display *, XErrorEvent *);
static int xpixmapcreating, xpixmapfailed;
static unsigned long xpixmapserial;
static Window embedparent; /* the window given by -w, if any */

/* Requestors may vanish at any time, also during a transfer. One going
 * between the two requests of XGetWindowAttributes fails only the second,
 * GetGeometry, with BadDrawable; Xlib hides that if both fail. Other
 * BadDrawable errors, e.g. from rendering phist's own window, and bad windows
 * of its own, such as a stale -w ID, still belong to the previous handler. */
static int
xerror(Display *dpy, XErrorEvent *e)
{
	int own = e->resourceid == xw.win ||
	          (embedparent && e->resourceid == embedparent);

	/* Only this request's error: syncing also delivers those of earlier ones. */
	if (xpixmapcreating && e->serial == xpixmapserial &&
	    e->error_code == BadAlloc && e->request_code == X_CreatePixmap) {
		xpixmapfailed = 1;
		return 0;
	}
	if ((e->error_code == BadWindow && !own) ||
	    (e->error_code == BadDrawable && e->request_code == X_GetGeometry))
		return 0;
	return xerrorprev(dpy, e);
}

/* The most text one ChangeProperty request carries: its 24 bytes and a big
 * request's 4-byte length are reserved, and text is padded to units. */
static size_t
selmaxbytes(void)
{
	long units = XExtendedMaxRequestSize(xw.dpy);

	if (!units)
		units = XMaxRequestSize(xw.dpy);
	return (size_t)units * 4 - 28;
}

/* Stop a transfer, restoring the requestor's events once phist has no other
 * transfer to it. */
static void
selsendend(SelSend *s)
{
	SelSend **p, *o;

	for (p = &selsends; *p != s; p = &(*p)->next)
		;
	*p = s->next;
	for (o = selsends; o && o->requestor != s->requestor; o = o->next)
		;
	if (!o)
		XSelectInput(xw.dpy, s->requestor, s->mask);
	free(s->data);
	free(s);
}

/* Whether another transfer of len bytes may start. Without others, any can. */
static int
selsendfits(Window requestor, size_t len)
{
	SelSend *s;
	size_t bytes = 0;
	int all = 0, window = 0;

	for (s = selsends; s; s = s->next) {
		all++;
		window += s->requestor == requestor;
		bytes += s->len;
	}
	return all < SELSEND_MAX && window < SELSEND_MAXPERWINDOW &&
	       (!all || (bytes <= SELSEND_MAXBYTES && len <= SELSEND_MAXBYTES - bytes));
}

/* Announce an incremental transfer; return 0 if it is refused or the requestor
 * is gone. */
static int
selsendstart(XSelectionRequestEvent *xsre, const char *text, size_t textlen)
{
	XWindowAttributes wa;
	SelSend *s, *o;
	char *data;
	long len;

	if (!selsendfits(xsre->requestor, textlen))
		return 0;
	for (o = selsends; o && o->requestor != xsre->requestor; o = o->next)
		;
	if (!o && !XGetWindowAttributes(xw.dpy, xsre->requestor, &wa))
		return 0;
	/* Any client can ask, which must not end the terminal for lack of memory. */
	if (!(data = malloc(textlen + 1)))
		return 0;
	memcpy(data, text, textlen + 1);
	s = xmalloc(sizeof(*s));
	s->requestor = xsre->requestor;
	s->property = xsre->property;
	s->target = xsre->target;
	s->len = textlen;
	s->data = data;
	s->pos = 0;
	s->mask = o ? o->mask : wa.your_event_mask;
	clock_gettime(CLOCK_MONOTONIC, &s->progress);
	s->next = selsends;
	selsends = s;
	/* Watch for deletions before the requestor can see the property. */
	XSelectInput(xw.dpy, s->requestor, s->mask | PropertyChangeMask);
	len = s->len;
	XChangeProperty(xw.dpy, s->requestor, s->property,
	                XInternAtom(xw.dpy, "INCR", 0), 32, PropModeReplace,
	                (uchar *)&len, 1);
	return 1;
}

/* Write the next chunk after the requestor deleted the last one, or the
 * empty one ending the transfer. */
static void
selsendnext(XEvent *e)
{
	SelSend *s;
	size_t n;

	for (s = selsends; s; s = s->next)
		if (s->requestor == e->xproperty.window &&
		    s->property == e->xproperty.atom)
			break;
	if (!s)
		return;
	n = MIN(s->len - s->pos, selmaxbytes());
	XChangeProperty(xw.dpy, s->requestor, s->property, s->target, 8,
	                PropModeReplace, (uchar *)s->data + s->pos, n);
	s->pos += n;
	clock_gettime(CLOCK_MONOTONIC, &s->progress);
	if (!n)
		selsendend(s);
}

void
xpropnotify(XEvent *e)
{
	if (e->xproperty.state == PropertyDelete)
		selsendnext(e);
	propnotify(e);
}

/* Drop transfers making no progress; return the ms until the next might
 * time out, or -1 if there is none. */
double
selsendremaining(struct timespec now)
{
	SelSend *s, *next;
	double remaining, soonest = -1;

	for (s = selsends; s; s = next) {
		next = s->next;
		remaining = SELSEND_TIMEOUT - TIMEDIFF(now, s->progress);
		if (remaining <= 0)
			selsendend(s);
		else if (soonest < 0 || remaining < soonest)
			soonest = remaining;
	}
	return soonest;
}

void
selrequest(XEvent *e)
{
	XSelectionRequestEvent *xsre;
	XSelectionEvent xev;
	Atom xa_targets, string, clipboard;
	char *seltext;
	size_t seltextlen;
	SelSend *s;

	xsre = (XSelectionRequestEvent *) e;
	xev.type = SelectionNotify;
	xev.requestor = xsre->requestor;
	xev.selection = xsre->selection;
	xev.target = xsre->target;
	xev.time = xsre->time;
	if (xsre->property == None)
		xsre->property = xsre->target;

	/* Every conversion replaces a transfer on this property, including a
	 * direct response, TARGETS or a refusal. Never send its stale chunks. */
	for (s = selsends; s; s = s->next) {
		if (s->requestor == xsre->requestor &&
		    s->property == xsre->property) {
			selsendend(s);
			break;
		}
	}

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
		seltextlen = seltext ? strlen(seltext) : 0;
		if (seltext != NULL && seltextlen > selmaxbytes()) {
			if (selsendstart(xsre, seltext, seltextlen))
				xev.property = xsre->property;
		} else if (seltext != NULL) {
			XChangeProperty(xsre->display, xsre->requestor,
					xsre->property, xsre->target,
					8, PropModeReplace,
					(uchar *)seltext, seltextlen);
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

	if (urlboxmouse(e))
		return;
	if (pastemouse(e)) {
		mouseignored &= ~buttonmask(btn);
		return;
	}
	if (mouseignored & buttonmask(btn)) {
		mouseignored &= ~buttonmask(btn);
		return;
	}
	if (mousepasting & buttonmask(btn)) {
		mousepasting &= ~buttonmask(btn);
		pastemouseaction(e, 1);
		return;
	}

	if (btn == Button1 && urlclick.active) {
		if (urlclick.text && evcol(e) == urlclick.col && evrow(e) == urlclick.row) {
			selclear();
			if (urlclick.explicit) {
				urlboxopen(urlclick.text, urlclick.anchorcol, urlclick.anchorrow,
				           urlclick.anchorendrow);
				urlclick.text = NULL;
			} else {
				setsel(urlclick.text, e->xbutton.time);
				urlclick.text = NULL;
				clipcopy(NULL);
				urlflashbegin(urlclick.col, urlclick.row);
			}
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
	if (urlbox.text)
		return;
	if (pastemouse(e))
		return;
	if ((mouseignored | mousepasting) & Button1Mask)
		return;
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

/* Keep every cell in the coordinates of X requests, which are 16-bit and
 * signed, and the screens to a sane size, however large a window or -g asks. */
static void
xlimitcells(int *col, int *row)
{
	*col = MAX(1, MIN(*col, (SHRT_MAX - 2 * borderpx) / win.cw));
	*row = MAX(1, MIN(*row, (SHRT_MAX - 2 * borderpx) / win.ch));
	*row = MIN(*row, CELLS_MAX / *col);
}

/* Create a pixmap for the window's size; return None if the server refuses. */
static Pixmap
xcreatepixmap(void)
{
	Pixmap pixmap;

	xpixmapfailed = 0;
	xpixmapcreating = 1;
	xpixmapserial = NextRequest(xw.dpy);
	pixmap = XCreatePixmap(xw.dpy, xw.win, win.w, win.h,
	                       DefaultDepth(xw.dpy, xw.scr));
	XSync(xw.dpy, False);
	xpixmapcreating = 0;
	return xpixmapfailed ? None : pixmap;
}

void
cresize(int width, int height)
{
	int col, row, oldw = win.w, oldh = win.h;

	/* X refuses larger pixmaps, so what is drawn stops here. */
	if (width != 0)
		win.w = MIN(width, SHRT_MAX);
	if (height != 0)
		win.h = MIN(height, SHRT_MAX);

	/* Without the memory, draw at the size of the buffer that exists. */
	if (!(xw.nextbuf = xcreatepixmap())) {
		fprintf(stderr, "can't allocate a %dx%d buffer, keeping %dx%d\n",
		        win.w, win.h, oldw, oldh);
		win.w = oldw;
		win.h = oldh;
	}

	col = (win.w - 2 * borderpx) / win.cw;
	row = (win.h - 2 * borderpx) / win.ch;
	xlimitcells(&col, &row);

	tresize(col, row);
	xresize(col, row);
	ttyresize(win.tw, win.th);
}

void
xresize(int col, int row)
{
	win.tw = col * win.cw;
	win.th = row * win.ch;

	vbellfree();
	if (xw.nextbuf) {
		XFreePixmap(xw.dpy, xw.buf);
		xw.buf = xw.nextbuf;
		xw.nextbuf = None;
		XftDrawChange(xw.draw, xw.buf);
	}
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

/*
 * Load the palette into a new array, so that the one in use survives a failure.
 * That only ends phist when it starts, not when a program resets the colors.
 */
void
xloadcols(void)
{
	static int loaded;
	Color *next;
	int i, len = MAX(LEN(colorname), 256);

	xclearunderlinecolors();

	next = xmalloc(len * sizeof(Color));
	for (i = 0; i < len; i++) {
		if (xloadcolor(i, NULL, &next[i]))
			continue;
		if (!loaded) {
			if (colorname[i])
				die("could not allocate color '%s'\n", colorname[i]);
			die("could not allocate color %d\n", i);
		}
		fprintf(stderr, "could not allocate color %d, keeping the palette\n", i);
		while (i > 0)
			XftColorFree(xw.dpy, xw.vis, xw.cmap, &next[--i]);
		free(next);
		return;
	}
	if (!loaded) {
		dc.col = next;
		dc.collen = len;
		loaded = 1;
		return;
	}
	for (i = 0; i < len; i++)
		XftColorFree(xw.dpy, xw.vis, xw.cmap, &dc.col[i]);
	memcpy(dc.col, next, len * sizeof(Color));
	free(next);
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

	/* Keep the existing size hints if allocation fails. */
	if (!(sizeh = XAllocSizeHints()))
		return;

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

	/* Unusable height, without adding values that could overflow. */
	f->height = xmetricok(f->ascent) && xmetricok(f->descent) ?
	            f->ascent + f->descent : 0;
	f->width = DIVCEIL(extents.xOff, strlen(ascii_printable));

	return 0;
}

/*
 * Validate the cell size, scaled by cwscale and chscale, before using it as
 * a divisor or in X coordinates: it must be at least 1, and not NaN, and at
 * most CELL_MAX, so the two cells of a wide character fit in X's 16-bit
 * coordinates.
 */
static int
xsetcellsize(const Font *f)
{
	float cw = ceilf(f->width * cwscale), ch = ceilf(f->height * chscale);

	if (f->width <= 0 || f->height <= 0 || !(cw >= 1 && cw <= CELL_MAX) ||
	    !(ch >= 1 && ch <= CELL_MAX))
		return 0;
	win.cw = cw;
	win.ch = ch;
	return 1;
}

/* Load the fonts; return nonzero, with none loaded, if one cannot be opened or
 * the metrics are unusable. */
int
xloadfonts(const char *fontstr, double fontsize)
{
	FcPattern *pattern;
	double fontval = 0;
	int bad = 0;

	if (fontstr[0] == '-')
		pattern = XftXlfdParse(fontstr, False, False);
	else
		pattern = FcNameParse((const FcChar8 *)fontstr);

	if (!pattern)
		return 1;

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

	if (xloadfont(&dc.font, pattern)) {
		FcPatternDestroy(pattern);
		return 1;
	}

	if (usedfontsize < 0) {
		bad = FcPatternGetDouble(dc.font.match->pattern, FC_PIXEL_SIZE, 0,
		                         &fontval) != FcResultMatch || !(fontval > 0);
		usedfontsize = fontval;
		if (fontsize == 0)
			defaultfontsize = fontval;
	}

	/* Setting character width and height. */
	if (bad || !xsetcellsize(&dc.font))
		goto nofont;

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
	if (xloadfont(&dc.ifont, pattern))
		goto nofont;

	FcPatternDel(pattern, FC_WEIGHT);
	FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
	if (xloadfont(&dc.ibfont, pattern))
		goto noitalic;

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
	if (xloadfont(&dc.bfont, pattern))
		goto nobold;

	FcPatternDestroy(pattern);
	return 0;

nobold:
	xunloadfont(&dc.ibfont);
noitalic:
	xunloadfont(&dc.ifont);
nofont:
	xunloadfont(&dc.font);
	FcPatternDestroy(pattern);
	return 1;
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

static void
xclearfontcache(void)
{
	while (frclen > 0)
		XftFontClose(xw.dpy, frc[--frclen].font);
	memset(uncovered, 0, sizeof(uncovered));
}

/* Reserve zero for an empty uncovered slot. */
static uint32_t
xuncoveredkey(int frcflags, Rune rune)
{
	return ((uint32_t)frcflags << 21 | rune) + 1;
}

static uint32_t *
xuncoveredslot(uint32_t key)
{
	return &uncovered[key * 2654435761u >> (32 - UNCOVERED_BITS)];
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

	xclearfontcache();

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

/* A window position from -g, from the far edge of the screen if negative. X
 * positions are 16-bit, and -g accepts any int, which must not overflow here. */
static int
xwindoworigin(int offset, int negative, int screen, int size)
{
	LIMIT(offset, SHRT_MIN, SHRT_MAX);
	if (negative)
		offset += screen - size - 2;
	LIMIT(offset, SHRT_MIN, SHRT_MAX);
	return offset;
}

/* Connect and load the fonts, whose size limits the terminal's. */
static void
xopenfonts(void)
{
	if (!(xw.dpy = XOpenDisplay(NULL)))
		die("can't open display\n");
	xerrorprev = XSetErrorHandler(xerror);
	xw.scr = XDefaultScreen(xw.dpy);
	xw.vis = XDefaultVisual(xw.dpy, xw.scr);
	xw.colormapped = xw.vis->class != TrueColor;

	if (!FcInit())
		die("could not init fontconfig.\n");

	usedfont = (opt_font == NULL)? font : opt_font;
	if (xloadfonts(usedfont, 0))
		die("can't use font %s\n", usedfont);
}

void
xinit(int cols, int rows)
{
	XGCValues gcvalues;
	Cursor cursor;
	Window parent, root;
	pid_t thispid = getpid();
	XColor xmousefg, xmousebg;

	/* colors */
	xw.cmap = XDefaultColormap(xw.dpy, xw.scr);
	xloadcols();

	/* adjust fixed window geometry */
	win.w = 2 * borderpx + cols * win.cw;
	win.h = 2 * borderpx + rows * win.ch;
	xw.l = xwindoworigin(xw.l, xw.gm & XNegative, DisplayWidth(xw.dpy, xw.scr), win.w);
	xw.t = xwindoworigin(xw.t, xw.gm & YNegative, DisplayHeight(xw.dpy, xw.scr), win.h);

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
	if (parent != root) {
		embedparent = parent;
		XReparentWindow(xw.dpy, xw.win, parent, xw.l, xw.t);
	}

	memset(&gcvalues, 0, sizeof(gcvalues));
	gcvalues.graphics_exposures = False;
	if (!(dc.gc = XCreateGC(xw.dpy, xw.win, GCGraphicsExposures, &gcvalues)))
		die("can't create graphics context\n");
	if (!(xw.buf = xcreatepixmap()))
		die("can't allocate a %dx%d buffer\n", win.w, win.h);
	XSetForeground(xw.dpy, dc.gc, dc.col[defaultbg].pixel);
	XFillRectangle(xw.dpy, xw.buf, dc.gc, 0, 0, win.w, win.h);

	/* font spec buffer */
	xw.specbuf = xmalloc(cols * sizeof(GlyphFontSpec));

	/* Xft rendering context */
	if (!(xw.draw = XftDrawCreate(xw.dpy, xw.buf, xw.vis, xw.cmap)))
		die("can't create Xft drawing context\n");

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
	xw.wmprotocols = XInternAtom(xw.dpy, "WM_PROTOCOLS", False);
	xw.wmdeletewin = XInternAtom(xw.dpy, "WM_DELETE_WINDOW", False);
	xw.netwmname = XInternAtom(xw.dpy, "_NET_WM_NAME", False);
	xw.netwmiconname = XInternAtom(xw.dpy, "_NET_WM_ICON_NAME", False);
	xw.cwduri = XInternAtom(xw.dpy, "_PHIST_CWD_URI", False);
	xw.utf8string = XInternAtom(xw.dpy, "UTF8_STRING", False);
	XSetWMProtocols(xw.dpy, xw.win, &xw.wmdeletewin, 1);

	xw.netwmpid = XInternAtom(xw.dpy, "_NET_WM_PID", False);
	XChangeProperty(xw.dpy, xw.win, xw.netwmpid, XA_CARDINAL, 32,
			PropModeReplace, (uchar *)&thispid, 1);

	win.mode = MODE_NUMLOCK;
	resettitle();
	xflushtitles(); /* name the window before it is mapped */
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

/* Cache a fallback font for rune. Return its index, -1 on loading failure,
 * or FRC_UNCOVERED if the best match lacks the character. */
static int
xaddfallback(Font *font, int frcflags, Rune rune)
{
	FcPattern *fcpattern = NULL, *fontpattern = NULL;
	FcCharSet *fccharset = NULL;
	FcFontSet *fcsets[1];
	FcResult fcres;
	FcBool color;
	XftFont *opened = NULL;
	int iscolor = 0, f = -1;

	if (!font->set)
		font->set = FcFontSort(0, font->pattern, 1, 0, &fcres);
	fcsets[0] = font->set;
	if (font->set && (fcpattern = FcPatternDuplicate(font->pattern)) &&
	    (fccharset = FcCharSetCreate()) &&
	    FcCharSetAddChar(fccharset, rune) &&
	    FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset) &&
	    FcPatternAddBool(fcpattern, FC_SCALABLE, 1) &&
	    FcConfigSubstitute(0, fcpattern, FcMatchPattern)) {
		FcDefaultSubstitute(fcpattern);
		fontpattern = xfallbackmatch(font, fcsets, fcpattern, rune,
		                             frcflags & FRC_TEXT);
	}
	if (fontpattern) {
		iscolor = FcPatternGetBool(fontpattern, FC_COLOR, 0, &color) ==
		          FcResultMatch && color;
		/* The font owns the pattern once opened. */
		if (!(opened = XftFontOpenPattern(xw.dpy, fontpattern)))
			FcPatternDestroy(fontpattern);
	}
	if (opened && !XftCharIndex(xw.dpy, opened, rune)) {
		XftFontClose(xw.dpy, opened);
		*xuncoveredslot(xuncoveredkey(frcflags, rune)) = xuncoveredkey(frcflags, rune);
		f = FRC_UNCOVERED;
	} else if (opened) {
		if (frclen >= frccap) {
			frccap += 16;
			frc = xrealloc(frc, frccap * sizeof(Fontcache));
		}
		frc[frclen].color = iscolor;
		frc[frclen].font = opened;
		frc[frclen].flags = frcflags;
		frc[frclen].unicodep = rune;
		f = frclen++;
	}
	if (fccharset)
		FcCharSetDestroy(fccharset);
	if (fcpattern)
		FcPatternDestroy(fcpattern);
	return f;
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
	int i, f, numspecs = 0;

	/* Specs refer to cached fonts until drawn, so only start over between runs. */
	if (frclen >= FRC_MAX)
		xclearfontcache();

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
			specs[numspecs].x = xshort(xp);
			specs[numspecs].y = xshort(yp);
			xp += runewidth;
			numspecs++;
			continue;
		}

		/* A character known to be in no font needs no search. */
		f = *xuncoveredslot(xuncoveredkey(frcflags, rune)) ==
		    xuncoveredkey(frcflags, rune) ? FRC_UNCOVERED : 0;

		/* Fallback on font cache, search the font cache for match. */
		for (; f >= 0 && f < frclen; f++) {
			/* Other characters may have a non-color fallback to find. */
			if ((frcflags & FRC_TEXT) && frc[f].color && frc[f].unicodep != rune)
				continue;
			glyphidx = XftCharIndex(xw.dpy, frc[f].font, rune);
			/* Everything correct. */
			if (glyphidx && frc[f].flags == frcflags)
				break;
		}

		/* Nothing was found. Use fontconfig to find matching font. */
		if (f >= frclen) {
			f = xaddfallback(font, frcflags, rune);
			if (f >= 0)
				glyphidx = XftCharIndex(xw.dpy, frc[f].font, rune);
		}
		if (f < 0) {
			/* The missing glyph, or a replacement if the fallback failed to load. */
			specs[numspecs].font = font->match;
			specs[numspecs].glyph = f == FRC_UNCOVERED ? 0 :
			                        XftCharIndex(xw.dpy, font->match, 0xFFFD);
			specs[numspecs].x = xshort(xp);
			specs[numspecs].y = xshort(yp);
			xp += runewidth;
			numspecs++;
			continue;
		}

		specs[numspecs].font = frc[f].font;
		specs[numspecs].glyph = glyphidx;
		specs[numspecs].x = xshort(xp);
		specs[numspecs].y = xshort(yp);
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

/* Fixed colors cannot be hidden by application palette changes (OSC 4/10/11). */
enum { VISIBLE_BLACK, VISIBLE_WHITE, VISIBLE_RED };

static Color *
xvisibilitycolor(int index)
{
	static Color colors[3];
	static int loaded[3];
	static const int nearest[] = { 0, 15, 1 }; /* black, white and red */
	XRenderColor values[] = {
		{ .alpha = 0xffff },
		{ .red = 0xffff, .green = 0xffff, .blue = 0xffff, .alpha = 0xffff },
		{ .red = 0x9999, .alpha = 0xffff },
	};

	return xlazycolor(&colors[index], &loaded[index], &values[index],
	                  &dc.col[nearest[index]]);
}

static double
xlinearcolor(ushort channel)
{
	double value = channel / 65535.0;

	return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

static double
xluminance(const Color *color)
{
	return 0.2126 * xlinearcolor(color->color.red) +
	       0.7152 * xlinearcolor(color->color.green) +
	       0.0722 * xlinearcolor(color->color.blue);
}

static Color *
xvisibleforeground(Color *fg, Color *bg)
{
	double light, dark, background;

	if (mincontrast <= 1)
		return fg;
	background = xluminance(bg) + 0.05;
	light = xluminance(fg) + 0.05;
	dark = MIN(light, background);
	light = MAX(light, background);
	if (light / dark >= mincontrast)
		return fg;
	return xvisibilitycolor(background / 0.05 >= 1.05 / background ?
	                        VISIBLE_BLACK : VISIBLE_WHITE);
}

/*
 * On a visual with a colormap, text keeps using a color's cell after it is drawn,
 * and another color in a freed cell would change that text. So colors are kept
 * until there is no room for more. xstartdraw() then frees them and has the whole
 * screen drawn again, in what it needs now.
 */
static struct {
	XRenderColor value;
	Color color;
} keptcolors[128];
static int nkeptcolors, keptcolorsfull;

static Color *
xkeptcolor(const XRenderColor *value, Color *fallback)
{
	int i;

	for (i = 0; i < nkeptcolors; i++)
		if (!memcmp(&keptcolors[i].value, value, sizeof(*value)))
			return &keptcolors[i].color;
	if (nkeptcolors == LEN(keptcolors) ||
	    !XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, value, &keptcolors[nkeptcolors].color)) {
		keptcolorsfull = nkeptcolors > 0;
		return fallback;
	}
	keptcolors[nkeptcolors].value = *value;
	return &keptcolors[nkeptcolors++].color;
}

static void
xreleasekeptcolors(void)
{
	if (!keptcolorsfull)
		return;
	while (nkeptcolors > 0)
		XftColorFree(xw.dpy, xw.vis, xw.cmap, &keptcolors[--nkeptcolors].color);
	keptcolorsfull = 0;
	tfulldirt();
}

/* Allocate a color for one run into temps; the caller frees it after drawing.
 * Visuals with a colormap can run out of cells, leaving a palette color. */
static Color *
xtempcolor(const XRenderColor *value, Color *temps, int *ntemps, Color *fallback)
{
	if (xw.colormapped)
		return xkeptcolor(value, fallback);
	if (!XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, value, &temps[*ntemps]))
		return fallback;
	return &temps[(*ntemps)++];
}

void
xdrawglyphfontspecs(const XftGlyphFontSpec *specs, Glyph base, int len, int x, int y)
{
	int charlen = len * ((base.mode & ATTR_WIDE) && win.tw > win.cw ? 2 : 1);
	int winx = borderpx + x * win.cw, winy = borderpx + y * win.ch,
	    width = charlen * win.cw;
	/* Up to true fg and bg, their reversals and faint fg, all freed below. */
	Color *fg, *bg, *temp, temps[5];
	int ntemps = 0;
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
		fg = xtempcolor(&colfg, temps, &ntemps, &dc.col[defaultfg]);
	} else {
		fg = &dc.col[base.fg];
	}

	if (IS_TRUECOL(base.bg)) {
		colbg.alpha = 0xffff;
		colbg.green = TRUEGREEN(base.bg);
		colbg.red = TRUERED(base.bg);
		colbg.blue = TRUEBLUE(base.bg);
		bg = xtempcolor(&colbg, temps, &ntemps, &dc.col[defaultbg]);
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
			fg = xtempcolor(&colfg, temps, &ntemps, &dc.col[defaultbg]);
		}

		if (bg == &dc.col[defaultbg]) {
			bg = &dc.col[defaultfg];
		} else {
			colbg.red = ~bg->color.red;
			colbg.green = ~bg->color.green;
			colbg.blue = ~bg->color.blue;
			colbg.alpha = bg->color.alpha;
			bg = xtempcolor(&colbg, temps, &ntemps, &dc.col[defaultfg]);
		}
	}

	if ((base.mode & ATTR_BOLD_FAINT) == ATTR_FAINT) {
		colfg.red = fg->color.red / 2;
		colfg.green = fg->color.green / 2;
		colfg.blue = fg->color.blue / 2;
		colfg.alpha = fg->color.alpha;
		fg = xtempcolor(&colfg, temps, &ntemps, fg);
	}

	if (base.mode & ATTR_REVERSE) {
		temp = fg;
		fg = bg;
		bg = temp;
	}

	if (revealhidden && (base.mode & ATTR_INVISIBLE)) {
		/* Selection and URL flashes toggle reverse video. */
		fg = xvisibilitycolor(base.mode & ATTR_REVERSE ?
		                      VISIBLE_RED : VISIBLE_WHITE);
		bg = xvisibilitycolor(base.mode & ATTR_REVERSE ?
		                      VISIBLE_WHITE : VISIBLE_RED);
	} else {
		fg = xvisibleforeground(fg, bg);
		/* Preserve ordinary blinking and the opt-out for concealed text. */
		if ((base.mode & ATTR_BLINK && win.mode & MODE_BLINK) ||
		    (base.mode & ATTR_INVISIBLE))
			fg = bg;
	}

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

	while (ntemps > 0)
		XftColorFree(xw.dpy, xw.vis, xw.cmap, &temps[--ntemps]);
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

static Glyph
xhyperlinkstyle(Glyph g)
{
	if (g.link) {
		g.fg = pastecellcolor(PASTE_BLUE);
		g.ucolor = UCOLOR_DEFAULT;
		g.mode &= ~(ATTR_REVERSE | ATTR_BLINK | ATTR_FAINT | ATTR_INVISIBLE | ATTR_USTYLE);
		g.mode |= ATTR_UNDERLINE | (1 << 13);
	}
	return g;
}

void
xdrawcursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og)
{
	Color drawcol;

	/* remove the old cursor */
	og = xhyperlinkstyle(og);
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

/*
 * Titles are only recorded here and applied once per draw by xflushtitles(),
 * so a flood of title changes cannot keep the window manager and task bar
 * redrawing.
 */
static char *pendingtitle, *pendingicontitle;

static void
xpendtitle(char **pending, char *p)
{
	DEFAULT(p, opt_title);

	if (p[0] == '\0')
		p = opt_title;
	free(*pending);
	*pending = xstrdup(p);
}

void
xseticontitle(char *p)
{
	xpendtitle(&pendingicontitle, p);
}

void
xsettitle(char *p)
{
	xpendtitle(&pendingtitle, p);
}

void
xflushtitles(void)
{
	XTextProperty prop;

	if (pendingicontitle) {
		if (Xutf8TextListToTextProperty(xw.dpy, &pendingicontitle, 1,
		                                XUTF8StringStyle, &prop) == Success) {
			XSetWMIconName(xw.dpy, xw.win, &prop);
			XSetTextProperty(xw.dpy, xw.win, &prop, xw.netwmiconname);
			XFree(prop.value);
		}
		free(pendingicontitle);
		pendingicontitle = NULL;
	}
	if (pendingtitle) {
		if (Xutf8TextListToTextProperty(xw.dpy, &pendingtitle, 1,
		                                XUTF8StringStyle, &prop) == Success) {
			XSetWMName(xw.dpy, xw.win, &prop);
			XSetTextProperty(xw.dpy, xw.win, &prop, xw.netwmname);
			XFree(prop.value);
		}
		free(pendingtitle);
		pendingtitle = NULL;
	}
}

/* Directory metadata follows the same batching as titles, including while
 * hidden. Keep the last value to avoid notifying watchers of duplicates. */
static char *cwduri;
static int cwddirty;

void
xsetcwd(const char *uri)
{
	if (!uri || !*uri)
		uri = NULL;
	if ((!uri && !cwduri) || (uri && cwduri && !strcmp(uri, cwduri)))
		return;
	free(cwduri);
	cwduri = uri ? xstrdup(uri) : NULL;
	cwddirty = 1;
}

static void
xflushcwd(void)
{
	if (!cwddirty)
		return;
	if (cwduri)
		XChangeProperty(xw.dpy, xw.win, xw.cwduri, xw.utf8string, 8,
		                PropModeReplace, (uchar *)cwduri, strlen(cwduri));
	else
		XDeleteProperty(xw.dpy, xw.win, xw.cwduri);
	cwddirty = 0;
}


static int vbellset = 0; /* 1 during visual bell, 0 otherwise */
static struct timespec lastvbell = {0}, lastvbellframe = {0};
static struct {
	Pixmap buf;
	Draw draw;
} vbell;

static void
vbellfree(void)
{
	if (vbell.draw)
		XftDrawDestroy(vbell.draw);
	if (vbell.buf)
		XFreePixmap(xw.dpy, vbell.buf);
	vbell.draw = NULL;
	vbell.buf = None;
}

static double
vbellsmooth(double t)
{
	t = MAX(0, MIN(1, t));
	return t * t * (3 - 2 * t);
}

/* Straight RGBA for gradient stops; rectangle fills need premultiplication. */
static XRenderColor
vbellblend(double mix, double strength)
{
	double alpha = MAX(0, MIN(1, vbellopacity)) * MAX(0, MIN(1, strength));
	XRenderColor color;

	mix = MAX(0, MIN(1, mix));
	color.red = (ushort)(257 * (((vbellcolor >> 16) & 255) * (1 - mix) +
	                          ((vbellinnercolor >> 16) & 255) * mix));
	color.green = (ushort)(257 * (((vbellcolor >> 8) & 255) * (1 - mix) +
	                            ((vbellinnercolor >> 8) & 255) * mix));
	color.blue = (ushort)(257 * ((vbellcolor & 255) * (1 - mix) +
	                           (vbellinnercolor & 255) * mix));
	color.alpha = (ushort)(65535 * alpha);
	return color;
}

static XRenderColor
vbellpremultiply(XRenderColor color)
{
	color.red = (uint32_t)color.red * color.alpha / 65535;
	color.green = (uint32_t)color.green * color.alpha / 65535;
	color.blue = (uint32_t)color.blue * color.alpha / 65535;
	return color;
}

/* Static glow along the window edges. */
static XRenderColor
vbellcolorat(int inset, int depth)
{
	double t = depth > 1 ? (double)inset / (depth - 1) : 0;

	return vbellpremultiply(vbellblend(vbellsmooth(t / 0.8),
	                                  1 - vbellsmooth((t - 0.6) / 0.4)));
}

/* Soft band with a colored trail and a brighter leading crest. */
static XRenderColor
vbellbandcolor(double offset)
{
	double strength = MAX(0, 1 - offset * offset);

	return vbellblend(vbellsmooth((offset + 1) / 1.5), strength * strength);
}

static double
vbellprogress(struct timespec now)
{
	return vbelltimeout > 0 ? MAX(0, MIN(1, TIMEDIFF(now, lastvbell) / vbelltimeout)) : 1;
}

static int
vbellanimated(void)
{
	return BETWEEN(vbelleffect, VBELL_WAVE, VBELL_PULSE);
}

/* Create concentric gradient rings; return 0 if their clamped radii coincide. */
static int
vbellcircles(XRadialGradient *radial, double inner, double outer)
{
	radial->inner = (XCircle){ xfixed(win.w / 2.0), xfixed(win.h / 2.0), xfixed(inner) };
	if (radial->inner.radius == INT_MAX)
		return 0;
	radial->outer = (XCircle){ radial->inner.x, radial->inner.y,
	                           MAX(radial->inner.radius + 1, xfixed(outer)) };
	return 1;
}

static Drawable
vbellrender(double progress)
{
	XRenderColor color, colors[33];
	XFixed stops[33];
	XRectangle rects[4];
	Picture picture, gradient;
	XLinearGradient linear;
	XRadialGradient radial;
	Draw draw;
	double half = vbellwidth / 2.0, center, inner, outer, distance;
	int inset, width, height, n, i;
	int depth = (MIN(win.w, win.h) + 1) / 2;
	int bell = vbellset && depth >= 1 && vbellwidth && vbellopacity > 0 &&
	           (!vbellanimated() || progress < 1);

	if (!bell && !pastemodal() && !tsearchactive() && !urlbox.text && !tvimactive())
		return xw.buf;
	progress = MAX(0, MIN(1, progress));
	if (!vbell.buf) {
		vbell.buf = xcreatepixmap();
		if (vbell.buf)
			vbell.draw = XftDrawCreate(xw.dpy, vbell.buf, xw.vis, xw.cmap);
	}
	if (!vbell.draw || !(picture = XftDrawPicture(vbell.draw))) {
		vbellfree();
		return xw.buf;
	}

	/* Always start from normal content, including text/cursor updates. */
	XCopyArea(xw.dpy, xw.buf, vbell.buf, dc.gc, 0, 0, win.w, win.h, 0, 0);
	draw = xw.draw;
	xw.draw = vbell.draw;
	/* The tint goes first, under the search bar and any dialog. */
	if (tvimactive())
		vimdraw();
	if (tsearchactive())
		searchdraw();
	/* Both overlays share the presentation pixmap. The terminal buffer
	 * stays untouched, so dirty-line drawing works while a dialog is open. */
	if (pastemodal())
		pastedraw();
	if (urlbox.text)
		urlboxdraw();
	xw.draw = draw;
	if (!bell)
		return vbell.buf;
	if (vbelleffect == VBELL_WAVE || vbelleffect == VBELL_RIPPLE) {
		inner = 0;
		outer = vbellwidth;
		if (vbelleffect == VBELL_WAVE) {
			center = -half + progress * (win.w + 2 * half);
			if (!xgradientline(&linear, center - half, center + half))
				return vbell.buf;
		} else {
			center = -half + progress * (hypot(win.w / 2.0, win.h / 2.0) + 2 * half);
			inner = MAX(0, center - half);
			outer = center + half;
			if (outer <= 0)
				return vbell.buf;
			if (!vbellcircles(&radial, inner, outer))
				return vbell.buf;
		}
		for (i = 0; i < LEN(stops); i++) {
			stops[i] = XDoubleToFixed((double)i / (LEN(stops) - 1));
			distance = inner + (outer - inner) * i / (LEN(stops) - 1);
			colors[i] = vbellbandcolor(vbelleffect == VBELL_WAVE ?
			                          2 * distance / vbellwidth - 1 :
			                          (distance - center) / half);
		}
		gradient = vbelleffect == VBELL_WAVE ?
		           XRenderCreateLinearGradient(xw.dpy, &linear, stops, colors, LEN(stops)) :
		           XRenderCreateRadialGradient(xw.dpy, &radial, stops, colors, LEN(stops));
		XRenderComposite(xw.dpy, PictOpOver, gradient, None, picture,
		                 0, 0, 0, 0, 0, 0, win.w, win.h);
		XRenderFreePicture(xw.dpy, gradient);
		return vbell.buf;
	}
	if (vbelleffect == VBELL_PULSE) {
		center = sin(3.141592653589793 * progress);
		color = vbellpremultiply(vbellblend(vbellsmooth(progress), center * center));
		rects[0] = (XRectangle){ 0, 0, win.w, win.h };
		XRenderFillRectangles(xw.dpy, PictOpOver, picture, &color, rects, 1);
		return vbell.buf;
	}

	center = -half + progress * (depth + 2 * half);
	if (vbelleffect != VBELL_INWARD)
		depth = MIN(vbellwidth, depth);
	for (inset = 0; inset < depth; inset++) {
		color = vbelleffect == VBELL_INWARD ?
		        vbellpremultiply(vbellbandcolor((inset - center) / half)) :
		        vbellcolorat(inset, depth);
		if (!color.alpha)
			continue;
		width = win.w - 2 * inset;
		height = win.h - 2 * inset;
		n = 0;
		rects[n++] = (XRectangle){ inset, inset, width, 1 };
		if (height > 1)
			rects[n++] = (XRectangle){ inset, inset + height - 1, width, 1 };
		if (height > 2) {
			rects[n++] = (XRectangle){ inset, inset + 1, 1, height - 2 };
			if (width > 1)
				rects[n++] = (XRectangle){ inset + width - 1, inset + 1, 1, height - 2 };
		}
		XRenderFillRectangles(xw.dpy, PictOpOver, picture, &color, rects, n);
	}
	return vbell.buf;
}

static void
vbellbegin(void)
{
	clock_gettime(CLOCK_MONOTONIC, &lastvbell);
	if (!vbellset)
		lastvbellframe = (struct timespec){0};
	vbellset = 1;
}

static double
vbellremaining(struct timespec now)
{
	double remain;

	if (!vbellset)
		return -1;
	remain = vbelltimeout - TIMEDIFF(now, lastvbell);
	if (remain > 0)
		return remain;
	vbellset = 0;
	return -1;
}

/* Wake for both the next animation frame and the final restoration. */
static double
vbellnextframe(struct timespec now)
{
	double remain = vbellremaining(now);

	if (remain > 0 && vbellanimated() && IS_SET(MODE_VISIBLE))
		remain = MIN(remain, MAX(0, (double)MAX(1, vbellinterval) -
		                             TIMEDIFF(now, lastvbellframe)));
	return remain;
}

int
xstartdraw(void)
{
	if (!IS_SET(MODE_VISIBLE))
		return 0;
	xreleasekeptcolors();
	return 1;
}

void
xdrawline(Line line, int x1, int y1, int x2)
{
	int i, x, ox, numspecs;
	int url = 0, newurl, match;
	Glyph base, new;
	XftGlyphFontSpec *specs = xw.specbuf;

	numspecs = xmakeglyphfontspecs(specs, &line[x1], x2 - x1, x1, y1);
	i = ox = 0;
	for (x = x1; x < x2 && i < numspecs; x++) {
		new = xhyperlinkstyle(line[x]);
		if (new.mode & ATTR_WDUMMY)
			continue;
		newurl = turl(x, y1);
		if (newurl)
			new.mode |= ATTR_URL;
		if (selected(x, y1))
			new.mode ^= ATTR_REVERSE;
		if (tvimcursor(x, y1))
			new.mode ^= ATTR_REVERSE;
		if ((match = tsearchmatch(x, y1))) {
			new.fg = rgbtruecolor(match == SEARCH_CURRENT ? searchmatchtextcolor :
			                      searchothermatchtextcolor);
			new.bg = rgbtruecolor(match == SEARCH_CURRENT ? searchmatchcolor :
			                      searchothermatchcolor);
			new.mode &= ~(ATTR_REVERSE | ATTR_BLINK | ATTR_FAINT | ATTR_INVISIBLE);
		}
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
	struct timespec now;
	Drawable frame;
	double progress = 0;

	if (vbellset) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		progress = vbellprogress(now);
		lastvbellframe = now;
	}
	frame = vbellrender(progress);
	XCopyArea(xw.dpy, frame, xw.win, dc.gc, 0, 0, win.w,
			win.h, 0, 0);
	/* If the overlay could not be set up, reuse the terminal's drawing
	 * context on the window. A modal dialog must stay visible, and drawing
	 * it here still leaves the terminal buffer clean for the next frame. */
	if (frame == xw.buf && (pastemodal() || tsearchactive() || urlbox.text ||
	                        tvimactive())) {
		XftDrawChange(xw.draw, xw.win);
		vimdraw();
		searchdraw();
		pastedraw();
		urlboxdraw();
		XftDrawChange(xw.draw, xw.buf);
	}
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
	/*
	 * Redraw with the next frame rather than at once, so a flood of
	 * reverse video toggles (CSI ? 5 h/l) costs one redraw per frame.
	 */
	if ((win.mode & MODE_REVERSE) != (mode & MODE_REVERSE))
		tfulldirt();
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

/* Limit audible bells, visual restarts and urgency updates to one per BELLGAP
 * ms. This bounds X traffic while allowing held keys to renew the visual bell. */
#define BELLGAP 100

void
xbell(void)
{
	static struct timespec last;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (TIMEDIFF(now, last) < BELLGAP)
		return;
	last = now;
	if (!(IS_SET(MODE_FOCUSED)))
		xseturgency(1);
	if (bellvolume)
		XkbBell(xw.dpy, xw.win, bellvolume, (Atom)NULL);
	if (vbelltimeout)
		vbellbegin();
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
	} else {
		if (xw.ime.xic)
			XUnsetICFocus(xw.ime.xic);
		win.mode &= ~MODE_FOCUSED;
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
	KeySym ksym = NoSymbol, lower;
	char buf[64], *customkey;
	int len;
	uint mods;
	Rune c;
	Status status;
	Shortcut *bp;

	if (xw.ime.xic) {
		len = XmbLookupString(xw.ime.xic, e, buf, sizeof buf, &ksym, &status);
		if (status == XBufferOverflow)
			return;
	} else {
		len = XLookupString(e, buf, sizeof buf, &ksym, NULL);
	}
	/* Before the shortcuts: Ctrl+V selects a block here. */
	if (vimkey(ksym, e->state, buf, len))
		return;
	lower = ksym >= XK_A && ksym <= XK_Z ? ksym + XK_a - XK_A : ksym;
	mods = e->state & ~(ignoremod | LockMask);
	if (urlboxkey(ksym, e->state))
		return;
	if (pastekey(ksym, e->state))
		return;
	if (searchkey(ksym, e->state, buf, len))
		return;
	/* 1. shortcuts, also with Caps Lock, which reports Ctrl+V as XK_V */
	for (bp = shortcuts; bp < shortcuts + LEN(shortcuts); bp++) {
		/* Keyboard selection may interrupt any held mouse button. */
		uint state = e->state & ~(LockMask | (bp->func == vimstart ? MOUSE_BUTTONS : 0));
		if ((ksym == bp->keysym || lower == bp->keysym) &&
		    match(bp->mod, state)) {
			bp->func(&(bp->arg));
			return;
		}
	}
	/* Honor configured shortcuts first, but keep disabled, unbound keys
	 * from falling through as control bytes or terminal mappings. */
	if (ksym == XK_Break || ksym == XK_Print ||
	    (mods == (ControlMask | ShiftMask) &&
	     (lower == XK_v || lower == XK_c || lower == XK_y ||
	      ksym == XK_Num_Lock)) ||
	    (lower == XK_v && mods == ControlMask))
		return;

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

/* Accept close requests only in WM_PROTOCOLS format for this window. */
static int
xclosemessage(const XClientMessageEvent *e)
{
	return e->window == xw.win && e->message_type == xw.wmprotocols &&
	       e->format == 32 && (Atom)e->data.l[0] == xw.wmdeletewin;
}

void
cmessage(XEvent *e)
{
	/*
	 * See xembed specs
	 *  http://standards.freedesktop.org/xembed-spec/xembed-spec-latest.html
	 */
	if (e->xclient.window == xw.win && e->xclient.message_type == xw.xembed &&
	    e->xclient.format == 32) {
		if (e->xclient.data.l[1] == XEMBED_FOCUS_IN) {
			win.mode |= MODE_FOCUSED;
			xseturgency(0);
		} else if (e->xclient.data.l[1] == XEMBED_FOCUS_OUT) {
			win.mode &= ~MODE_FOCUSED;
		}
	} else if (xclosemessage(&e->xclient)) {
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

/* Extensions deliver event types beyond the core ones the table covers. */
static void
xdispatch(XEvent *e)
{
	if (BETWEEN(e->type, 0, LEN(handler) - 1) && handler[e->type])
		handler[e->type](e);
}

/*
 * Wait up to timeout ms, or without limit if negative, for the tty or the X
 * connection. Unlike select(), poll() takes descriptors of any number, and a
 * program may start phist with many open. A hung up or failed tty counts as
 * ready, for its read or write to report. Return -1 if a signal interrupted.
 */
static int
xwait(int xfd, int ttyfd, int wantwrite, double timeout, int *readable, int *writable)
{
	struct pollfd fds[] = {
		{ .fd = ttyfd, .events = POLLIN | (wantwrite ? POLLOUT : 0) },
		{ .fd = xfd, .events = POLLIN },
	};
	int ms = timeout < 0 ? -1 : timeout >= INT_MAX ? INT_MAX : ceil(timeout);

	*readable = *writable = 0;
	if (poll(fds, LEN(fds), ms) < 0) {
		if (errno == EINTR)
			return -1;
		die("poll failed: %s\n", strerror(errno));
	}
	if ((fds[0].revents | fds[1].revents) & POLLNVAL)
		die("poll failed: %s\n", strerror(EBADF));
	*readable = !!(fds[0].revents & (POLLIN | POLLHUP | POLLERR));
	*writable = wantwrite && (fds[0].revents & (POLLOUT | POLLHUP | POLLERR));
	return 0;
}

void
run(void)
{
	XEvent ev;
	int w = win.w, h = win.h;
	int xfd = XConnectionNumber(xw.dpy), ttyfd, xev, drawing, readable, writable;
	struct timespec now, trigger;
	double timeout, urlremain, bellremain, pasteremain, sendremain;

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
		clock_gettime(CLOCK_MONOTONIC, &now);
		pasteremain = pasteremaining(now);
		if (pasteremain >= 0 && (timeout < 0 || pasteremain < timeout))
			timeout = pasteremain;
		sendremain = selsendremaining(now);
		if (sendremain >= 0 && (timeout < 0 || sendremain < timeout))
			timeout = sendremain;
		if (XPending(xw.dpy))
			timeout = 0;  /* existing events might not set xfd */

		/* Deliver queued input and query replies as the child reads. */
		if (xwait(xfd, ttyfd, ttywritepending(), timeout, &readable, &writable) < 0)
			continue;
		clock_gettime(CLOCK_MONOTONIC, &now);
		pasteremaining(now);
		selsendremaining(now);

		if (writable)
			ttyflushwrites();
		if (readable)
			ttyread();

		xev = 0;
		while (XPending(xw.dpy)) {
			xev = 1;
			XNextEvent(xw.dpy, &ev);
			if (XFilterEvent(&ev, None))
				continue;
			xdispatch(&ev);
		}

		if (vbellset)
			clock_gettime(CLOCK_MONOTONIC, &now);
		bellremain = vbellnextframe(now);

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
		if (readable || xev) {
			if (!drawing) {
				trigger = now;
				drawing = 1;
			}
			timeout = (maxlatency - TIMEDIFF(now, trigger)) \
			          / maxlatency * minlatency;
			if (bellremain >= 0)
				timeout = MIN(timeout, bellremain);
			if (timeout > 0)
				continue;  /* we have time, try to find idle */
		}

		/* idle detected or maxlatency exhausted -> draw */
		applyresize();
		timeout = blinkremaining(now);

		urlremain = urlflashremaining(now);
		if (urlremain > 0 && (timeout < 0 || urlremain < timeout))
			timeout = urlremain;

		/* Even when hidden, so task bars never show a stale title. */
		xflushtitles();
		xflushcwd();
		draw();
		if (vbellset) {
			clock_gettime(CLOCK_MONOTONIC, &now);
			bellremain = vbellnextframe(now);
			if (bellremain >= 0 && (timeout < 0 || bellremain < timeout))
				timeout = bellremain;
		}
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
	int col, row;

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
	xopenfonts();
	/* Nothing is allocated for the terminal before its size is limited. */
	col = MIN(cols, SHRT_MAX);
	row = MIN(rows, SHRT_MAX);
	xlimitcells(&col, &row);
	tnew(col, row);
	xinit(col, row);
	xsetenv();
	selinit();
	run();

	return 0;
}
