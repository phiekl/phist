#include <assert.h>
#define VERSION "test"
#define main st_main
#include "../x.c"
#undef main

static int allocations, frees, failalloc, fills;
static int contexts, freedcontexts;
static unsigned long stroke;
static unsigned long painted[2100];
static int recordglyphs;

Line testrender_text(int, const char *);

FT_UInt XftCharIndex(Display *d, XftFont *f, FcChar32 c) { return 1; }

Bool
XftColorAllocValue(Display *d, Visual *v, Colormap m, const XRenderColor *value, XftColor *color)
{
	if (failalloc) return 0;
	allocations++;
	color->color = *value;
	color->pixel = ((value->red / 257) << 16) | ((value->green / 257) << 8) | (value->blue / 257);
	return 1;
}

void XftColorFree(Display *d, Visual *v, Colormap m, XftColor *color) { frees++; }
void XftDrawRect(XftDraw *d, const XftColor *c, int x, int y, unsigned int w, unsigned int h) {}
Bool XftDrawSetClipRectangles(XftDraw *d, int x, int y, const XRectangle *r, int n) { return 1; }
Bool XftDrawSetClip(XftDraw *d, Region r) { return 1; }
void
XftDrawGlyphFontSpec(XftDraw *d, const XftColor *c, const XftGlyphFontSpec *s, int n)
{
	int i, col;

	if (!recordglyphs)
		return;
	for (i = 0; i < n; i++) {
		col = (s[i].x - borderpx) / win.cw;
		assert(BETWEEN(col, 0, LEN(painted) - 1));
		painted[col] = c->pixel;
	}
}
Drawable XftDrawDrawable(XftDraw *d) { return 1; }
GC XCreateGC(Display *d, Drawable dr, unsigned long mask, XGCValues *v) { contexts++; return (GC)1; }
int XChangeGC(Display *d, GC gc, unsigned long mask, XGCValues *v) { stroke = v->foreground; return 0; }
int XFreeGC(Display *d, GC gc) { freedcontexts++; return 0; }
int XFillRectangle(Display *d, Drawable dr, GC gc, int x, int y, unsigned int w, unsigned int h) { fills++; return 0; }
static short undercurly;
/* What a long underline's requests add up to, and whether they join up. */
static int largestrequest, requestunits = 65535, drawnsegments, drawnarcs, disjoint;
static XPoint penat;
static int penset;
long XMaxRequestSize(Display *d) { return requestunits; }
int
XDrawLines(Display *d, Drawable dr, GC gc, XPoint *p, int n, int mode)
{
	int i;

	fills++;
	undercurly = p[0].y;
	largestrequest = MAX(largestrequest, 3 + n);
	drawnsegments += n - 1;
	if (penset && (p[0].x != penat.x || p[0].y != penat.y))
		disjoint++;
	penat = p[0];
	for (i = 1; i < n; i++) {
		penat.x = mode == CoordModeOrigin ? p[i].x : penat.x + p[i].x;
		penat.y = mode == CoordModeOrigin ? p[i].y : penat.y + p[i].y;
	}
	penset = 1;
	return 0;
}
int XDrawArcs(Display *d, Drawable dr, GC gc, XArc *a, int n) { fills++; undercurly = a[0].y; largestrequest = MAX(largestrequest, 3 + 3 * n); drawnarcs += n; return 0; }

/* With every UNDERCURL_STYLE: see rendering.sh. */
static void
testundercurl(void)
{
	XftGlyphFontSpec spec = {0};
	Glyph base = { .u = 'x', .fg = 1, .bg = 0, .ucolor = UCOLOR_DEFAULT,
		.mode = ATTR_UNDERLINE | (3 << 13) };
	int savecw = win.cw, savech = win.ch, savetw = win.tw, saveth = win.th;
	int saveascent = dc.font.ascent, savedescent = dc.font.descent;
	int savew = win.w, saveh = win.h, x, before;

	/* A run with no glyphs, such as a dummy cell alone, draws no curl. */
	before = fills;
	xdrawglyphfontspecs(&spec, base, 0, 0, 0);
	assert(fills == before);

	/* A thin cell at the bottom of a maximum-height window: coordinates at
	 * 32,768 must clamp rather than wrap to the top. */
	win.cw = 9;
	win.ch = 1;
	dc.font.ascent = 0;
	dc.font.descent = 1;
	win.w = win.tw = SHRT_MAX;
	win.h = win.th = SHRT_MAX;
	for (x = 0; x < 8; x++) {
		undercurly = 0;
		xdrawglyphfontspecs(&spec, base, 2, x, 32768 - borderpx);
		assert(undercurly > 0);
	}
	win.cw = savecw;
	win.ch = savech;
	win.tw = savetw;
	win.th = saveth;
	win.w = savew;
	win.h = saveh;
	dc.font.ascent = saveascent;
	dc.font.descent = savedescent;
	puts("PASS: curly underlines stay in bounds and in X's coordinates");
}

/* Each request fits the minimum server limit; together they draw one line. */
static void
testlongunderline(XftGlyphFontSpec *spec, Glyph base)
{
	int cw = win.cw, ch = win.ch, segments, arcs, limits[] = { 4096, 65535 }, i;
	XPoint end;

	base.mode = ATTR_UNDERLINE | (3 << 13);
	win.cw = 1;
	win.ch = 28; /* a single line, with no second one offset from it */
	requestunits = 1 << 20;
	drawnsegments = drawnarcs = penset = 0;
	xdrawglyphfontspecs(spec, base, 30000, 0, 1);
	segments = drawnsegments;
	arcs = drawnarcs;
	end = penat;
	assert(segments || arcs);
	for (i = 0; i < LEN(limits); i++) {
		requestunits = limits[i];
		largestrequest = drawnsegments = drawnarcs = disjoint = penset = 0;
		xdrawglyphfontspecs(spec, base, 30000, 0, 1);
		assert(largestrequest <= limits[i] && !disjoint);
		assert(drawnsegments == segments && drawnarcs == arcs);
		assert(!segments || (penat.x == end.x && penat.y == end.y));
	}
	requestunits = 65535;
	win.cw = cw;
	win.ch = ch;
}

int
main(void)
{
	Color colors[260] = {0};
	XftGlyphFontSpec spec = {0};
	Glyph base = { .u = 'x', .fg = 1, .bg = 0, .ucolor = TRUECOLOR(1, 2, 3) };
	int i;

	dc.col = colors;
	colors[1].pixel = 0xabcdef;
	colors[2].pixel = 0x654321;
	dc.font.ascent = 10;
	dc.font.descent = 3;
	win.w = win.tw = 800;
	win.h = win.th = 600;
	win.cw = 8;
	win.ch = 16;
	tnew(20, 4);
	for (i = 1; i <= 5; i++) {
		base.mode = ATTR_UNDERLINE | (i << 13);
		xdrawglyphfontspecs(&spec, base, 1, 1, 1);
		assert(stroke == 0x010203);
		assert(contexts == 1 && !freedcontexts); /* one reused context */
	}
	assert(allocations == 1 && !frees && fills);
	testlongunderline(&spec, base);
	base.ucolor = 2;
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(stroke == colors[2].pixel && allocations == 1);
	base.mode = ATTR_URL;
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(stroke == colors[1].pixel && allocations == 1);
	for (i = 0; i < 100; i++)
		assert(xunderlinepixel(TRUECOLOR(i, 2, 3), 0) == ((i << 16) | 0x0203));
	assert(allocations - frees == LEN(underlinecolors.entries));
	failalloc = 1;
	assert(xunderlinepixel(TRUECOLOR(200, 1, 2), 0x123456) == 0x123456);
	xclearunderlinecolors();
	assert(allocations == frees);
	assert(contexts == 1 && !freedcontexts);
	/* Adjacent capped URL occurrences must flash independently. */
	{
		const char *second = "https://second.example/";
		char text[2100];
		Line line;

		memset(text, 'a', 2048);
		memcpy(text, "https://", 8);
		strcpy(text + 2048, second);
		line = testrender_text(LEN(painted), text);
		borderpx = 0;
		win.w = win.tw = LEN(painted) * win.cw;
		win.h = win.th = win.ch;
		xw.specbuf = xmalloc(LEN(painted) * sizeof(*xw.specbuf));
		colors[defaultfg].pixel = 0x123456;
		colors[defaultbg].pixel = 0x654321;
		recordglyphs = 1;
		tflashurl(2048, 0, second);
		xdrawline(line, 0, 0, LEN(painted));
		assert(painted[0] == colors[defaultfg].pixel);
		assert(painted[2048] == colors[defaultbg].pixel);
		text[2048] = '\0';
		tflashurl(0, 0, text);
		xdrawline(line, 0, 0, LEN(painted));
		assert(painted[0] == colors[defaultbg].pixel);
		assert(painted[2048] == colors[defaultfg].pixel);
		free(xw.specbuf);
	}
	puts("PASS: underline styles, RGB cache reuse/eviction, fallback, balanced frees and a reused GC");
	testundercurl();
	return 0;
}
