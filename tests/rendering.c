#include <assert.h>
#define VERSION "test"
#define main st_main
#include "../x.c"
#undef main

static int allocations, frees, failalloc, fills;
static int contexts, freedcontexts;
static unsigned long stroke;
static unsigned long painted[2100];
static unsigned long backgrounds[2100];
static int recordglyphs;
static Color glyphcolor, backgroundcolor;

Line testrender_text(int, const char *);
void testrender_marks(void);
void testrender_clean(void);
int testrender_alldirty(void);

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

Bool
XftColorAllocName(Display *d, const Visual *v, Colormap m, const char *name, XftColor *color)
{
	allocations++;
	color->pixel = 0x123456;
	return 1;
}

void XftColorFree(Display *d, Visual *v, Colormap m, XftColor *color) { frees++; }
void
XftDrawRect(XftDraw *d, const XftColor *c, int x, int y, unsigned int w, unsigned int h)
{
	int i;

	if (h == win.ch) {
		backgroundcolor = *c;
		if (recordglyphs)
			for (i = MAX(0, (x - borderpx) / win.cw);
			     i < MIN(LEN(backgrounds), (x - borderpx + w) / win.cw); i++)
				backgrounds[i] = c->pixel;
	}
}
Bool XftDrawSetClipRectangles(XftDraw *d, int x, int y, const XRectangle *r, int n) { return 1; }
Bool XftDrawSetClip(XftDraw *d, Region r) { return 1; }
void
XftDrawGlyphFontSpec(XftDraw *d, const XftColor *c, const XftGlyphFontSpec *s, int n)
{
	int i, col;

	glyphcolor = *c;
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

static void
setcolor(Color *color, unsigned int rgb)
{
	color->pixel = rgb;
	color->color = (XRenderColor) { .red = ((rgb >> 16) & 255) * 257,
		.green = ((rgb >> 8) & 255) * 257, .blue = (rgb & 255) * 257,
		.alpha = 0xffff };
}

static void
testvisibility(void)
{
	Glyph base = { .u = 'X', .fg = 1, .bg = 2, .ucolor = UCOLOR_DEFAULT };
	XftGlyphFontSpec spec = {0};
	Line line;
	int before, i;
	char *copy;

	assert(revealhidden == 1 && mincontrast == 3.0);
	failalloc = 0;
	recordglyphs = 0;
	win.mode = 0;
	setcolor(&dc.col[1], 0x101010);
	setcolor(&dc.col[2], 0x111111);
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == 0xffffff && backgroundcolor.pixel == 0x111111);
	/* Different indices can resolve to exactly the same RGB value. */
	setcolor(&dc.col[1], 0xffffff);
	setcolor(&dc.col[2], 0xffffff);
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == 0 && backgroundcolor.pixel == 0xffffff);
	/* Legible colors remain unchanged, including after reversal. */
	setcolor(&dc.col[1], 0xeeeeee);
	setcolor(&dc.col[2], 0x111111);
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == 0xeeeeee && backgroundcolor.pixel == 0x111111);
	base.mode = ATTR_REVERSE;
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == 0x111111 && backgroundcolor.pixel == 0xeeeeee);
	/* Faint and reverse video are applied before the contrast check. */
	base.mode = ATTR_FAINT;
	setcolor(&dc.col[1], 0x888888);
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == 0xffffff);
	base.mode = ATTR_REVERSE;
	setcolor(&dc.col[1], 0xeeeeee);
	setcolor(&dc.col[2], 0xefefef);
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == 0 && backgroundcolor.pixel == 0xeeeeee);
	base.mode = 0;
	win.mode = MODE_REVERSE;
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == 0xffffff && backgroundcolor.pixel == 0x101010);
	win.mode = 0;
	/* RGB colors and backgrounds retain their exact original values. */
	base.fg = TRUECOLOR(20, 21, 22);
	base.bg = TRUECOLOR(21, 22, 23);
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == 0xffffff && backgroundcolor.pixel == 0x141516);
	mincontrast = 0;
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == 0x131415 && backgroundcolor.pixel == 0x141516);
	mincontrast = 3;
	/* Concealment gets a fixed warning background, even while blinking. */
	base.mode = ATTR_INVISIBLE | ATTR_BLINK | ATTR_FAINT | ATTR_REVERSE |
	            ATTR_UNDERLINE | (1 << 13);
	base.ucolor = TRUECOLOR(1, 2, 3);
	win.mode = MODE_BLINK | MODE_REVERSE;
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == 0x990000 && backgroundcolor.pixel == 0xffffff);
	assert(stroke == 0x990000);
	base.mode ^= ATTR_REVERSE;
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == 0xffffff && backgroundcolor.pixel == 0x990000);
	assert(stroke == 0xffffff);
	/* Neither OSC palette changes nor repeated drawing alter the marker. */
	win.mode = MODE_BLINK;
	before = allocations;
	for (i = 0; i < 100; i++) {
		setcolor(&dc.col[1], i);
		setcolor(&dc.col[2], i);
		base.fg = 1;
		base.bg = 2;
		base.mode = ATTR_INVISIBLE;
		xdrawglyphfontspecs(&spec, base, 1, 1, 1);
		assert(glyphcolor.pixel == 0xffffff && backgroundcolor.pixel == 0x990000);
	}
	assert(allocations == before);
	revealhidden = 0;
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == backgroundcolor.pixel);
	revealhidden = 1;
	/* Ordinary blinking still has an invisible phase. */
	base.mode = ATTR_BLINK;
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == backgroundcolor.pixel);
	win.mode = 0;
	/* Exercise escape parsing, mixed runs, leading spaces and copying. */
	line = testrender_text(8, "\033[8mhide\033[28m\033[30;40m X");
	xw.specbuf = xmalloc(8 * sizeof(*xw.specbuf));
	recordglyphs = 1;
	xdrawline(line, 0, 0, 8);
	assert(painted[0] == 0xffffff && painted[3] == 0xffffff);
	assert(painted[5] == 0xffffff);
	assert(backgrounds[0] == 0x990000 && backgrounds[3] == 0x990000);
	assert(backgrounds[4] == 0 && backgrounds[5] == 0);
	assert(line[0].mode & ATTR_INVISIBLE);
	assert(!(line[4].mode & ATTR_INVISIBLE));
	assert(line[5].fg == 0 && line[5].bg == 0);
	/* Partial selection must reverse only the selected concealed cells. */
	selstart(1, 0, 0);
	selextend(2, 0, SEL_REGULAR, 0);
	selextend(2, 0, SEL_REGULAR, 1);
	xdrawline(line, 0, 0, 8);
	assert(painted[0] == 0xffffff && backgrounds[0] == 0x990000);
	assert(painted[1] == 0x990000 && backgrounds[1] == 0xffffff);
	assert(painted[2] == 0x990000 && backgrounds[2] == 0xffffff);
	assert(painted[3] == 0xffffff && backgrounds[3] == 0x990000);
	selclear();
	xdrawline(line, 0, 0, 8);
	assert(painted[1] == 0xffffff && backgrounds[1] == 0x990000);
	/* The keyboard selection cursor reverses its cell, showing through a
	 * selection, and is drawn without the terminal's cursor. */
	tvimstart();
	tvimkey('0', 0);
	tvimkey('l', 0);
	xdrawline(line, 0, 0, 8);
	assert(painted[0] == 0xffffff && backgrounds[0] == 0x990000);
	assert(painted[1] == 0x990000 && backgrounds[1] == 0xffffff);
	assert(painted[2] == 0xffffff && backgrounds[2] == 0x990000);
	tvimkey('v', 0);
	tvimkey('l', 0);
	xdrawline(line, 0, 0, 8);
	assert(painted[1] == 0x990000 && backgrounds[1] == 0xffffff);
	assert(painted[2] == 0xffffff && backgrounds[2] == 0x990000);
	tvimend();
	xdrawline(line, 0, 0, 8);
	assert(painted[1] == 0xffffff && backgrounds[1] == 0x990000);
	selstart(0, 0, 0);
	selextend(5, 0, SEL_REGULAR, 0);
	selextend(5, 0, SEL_REGULAR, 1);
	copy = getsel();
	assert(copy && !strcmp(copy, "hide X"));
	free(copy);
	selclear();
	free(xw.specbuf);
	xw.specbuf = NULL;
	recordglyphs = 0;
	puts("PASS: concealed text warnings, selection feedback, minimum contrast, RGB/palette colors, faint/reverse/blink, opt-outs and unchanged copying");
}

static void
testconcealedurl(void)
{
	const char *url = "https://example.com";
	Line line = testrender_text(32, "\033[8mhttps://example.com\033[0m plain");
	int i, reverse, urllen = strlen(url);

	xw.specbuf = xmalloc(32 * sizeof(*xw.specbuf));
	recordglyphs = 1;
	for (reverse = 0; reverse <= 1; reverse++) {
		for (i = 0; i < urllen; i++)
			MODBIT(line[i].mode, reverse, ATTR_REVERSE);
		xdrawline(line, 0, 0, 32);
		assert(painted[0] == (reverse ? 0x990000 : 0xffffff));
		assert(backgrounds[0] == (reverse ? 0xffffff : 0x990000));
		tflashurl(0, 0, url);
		xdrawline(line, 0, 0, 32);
		for (i = 0; i < urllen; i++) {
			assert(painted[i] == (reverse ? 0xffffff : 0x990000));
			assert(backgrounds[i] == (reverse ? 0x990000 : 0xffffff));
		}
		assert(backgrounds[urllen] == dc.col[defaultbg].pixel);
		tclearurlflash();
		xdrawline(line, 0, 0, 32);
		assert(painted[0] == (reverse ? 0x990000 : 0xffffff));
		assert(backgrounds[0] == (reverse ? 0xffffff : 0x990000));
	}
	free(xw.specbuf);
	xw.specbuf = NULL;
	recordglyphs = 0;
	puts("PASS: concealed URLs flash and restore, including reverse-video URLs");
}

/* Colors made for one run are freed with it, and a failure leaves the palette. */
static void
testtempcolors(void)
{
	Glyph base = { .u = 'x', .fg = TRUECOLOR(90, 0, 0), .bg = TRUECOLOR(0, 90, 0),
	               .ucolor = UCOLOR_DEFAULT };
	XftGlyphFontSpec spec = {0};
	int i, before;

	setcolor(&dc.col[defaultfg], 0xffffff);
	setcolor(&dc.col[defaultbg], 0x000000);
	setcolor(&dc.col[1], 0x00ff00);
	setcolor(&dc.col[15], 0xfefefe);

	failalloc = 1;
	assert(xvisibilitycolor(VISIBLE_WHITE) == &dc.col[15]);
	assert(xvisibilitycolor(VISIBLE_RED) == &dc.col[1]);
	assert(xvisibilitycolor(VISIBLE_BLACK) == &dc.col[0]);
	failalloc = 0;
	for (i = 0; i < 3; i++)
		assert(xvisibilitycolor(i) != &dc.col[((int[]){ 0, 15, 1 })[i]]);

	/* Dialog and bar colors keep light on dark, and are not stuck on failure. */
	failalloc = 1;
	assert(rgbcolor(0xfff0a0) == &dc.col[15] && rgbcolor(0x202040) == &dc.col[0]);
	assert(pastecolor(PASTE_WHITE) == &dc.col[15] && pastecolor(PASTE_BLACK) == &dc.col[0]);
	failalloc = 0;
	assert(rgbcolor(0xfff0a0) != &dc.col[15] && rgbcolor(0xfff0a0)->pixel == 0xfff0a0);
	assert(pastecolor(PASTE_WHITE) != &dc.col[15]);

	for (i = 0; i < 4; i++) {
		allocations = frees = 0;
		win.mode = i & 1 ? MODE_REVERSE : 0;
		base.mode = i & 2 ? ATTR_FAINT : 0;
		xdrawglyphfontspecs(&spec, base, 1, 1, 1);
		assert(allocations == 2 + (i & 1 ? 2 : 0) + (i & 2 ? 1 : 0));
		assert(frees == allocations);
	}

	/* Nothing to free after failing, and nothing drawn in a stale color. */
	failalloc = 1;
	allocations = frees = 0;
	win.mode = 0;
	base.mode = 0;
	glyphcolor.pixel = backgroundcolor.pixel = 0x123456;
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == 0xffffff && backgroundcolor.pixel == 0);
	/* Reversing palette colors falls back to the reversed defaults. */
	base.fg = 1;
	base.bg = 2;
	setcolor(&dc.col[2], 0x0000ff);
	win.mode = MODE_REVERSE;
	glyphcolor.pixel = backgroundcolor.pixel = 0x123456;
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(backgroundcolor.pixel == 0xffffff);
	assert(glyphcolor.pixel == 0);
	/* Faint text that cannot be dimmed stays as it was. */
	win.mode = 0;
	base.bg = defaultbg;
	base.mode = ATTR_FAINT;
	glyphcolor.pixel = 0x123456;
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(glyphcolor.pixel == 0x00ff00);
	assert(!frees);
	failalloc = 0;
	win.mode = 0;
	allocations = frees = 0;

	/* With a colormap, colors outlive the run, whose text still uses their cells. */
	xw.colormapped = 1;
	base.mode = 0;
	base.fg = TRUECOLOR(255, 0, 0);
	base.bg = defaultbg;
	for (i = 0; i < 3; i++)
		xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(allocations == 1 && !frees && glyphcolor.pixel == 0xfe0000);
	/* Without room, a palette color until everything is drawn again. */
	win.mode = MODE_VISIBLE;
	assert(xstartdraw() && !frees);
	for (i = 1; i <= LEN(keptcolors); i++) {
		base.fg = TRUECOLOR(255, i / 256, i % 256);
		xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	}
	assert(nkeptcolors == LEN(keptcolors) && keptcolorsfull && !frees);
	assert(glyphcolor.pixel == dc.col[defaultfg].pixel);
	testrender_clean();
	assert(xstartdraw() && frees == LEN(keptcolors) && !nkeptcolors && !keptcolorsfull);
	assert(testrender_alldirty());
	testrender_clean();
	assert(xstartdraw() && frees == LEN(keptcolors) && !testrender_alldirty());
	/* Failing with nothing to free is not worth drawing everything again. */
	failalloc = 1;
	xdrawglyphfontspecs(&spec, base, 1, 1, 1);
	assert(!keptcolorsfull);
	failalloc = 0;
	xw.colormapped = 0;
	win.mode = 0;
	allocations = frees = 0;
	puts("PASS: colors made for a run are freed, and failures use the palette");
}

/* Resetting the palette keeps the one in use if a color cannot be allocated. */
static void
testpalette(void)
{
	Color *palette;
	int before;

	failalloc = 0;
	xloadcols();
	palette = dc.col;
	assert(dc.collen >= 256 && dc.col[1].pixel == 0x123456);
	setcolor(&dc.col[1], 0xabcdef);

	/* The named colors load, the rest fail, and only the new ones are freed. */
	failalloc = 1;
	before = frees;
	xloadcols();
	assert(frees == before + 16);
	assert(dc.col == palette && dc.col[1].pixel == 0xabcdef);

	failalloc = 0;
	before = frees;
	xloadcols();
	assert(frees == before + dc.collen);
	assert(dc.col == palette && dc.col[1].pixel == 0x123456);
	puts("PASS: a failed palette reset keeps the palette in use");
}

int
main(void)
{
	Color colors[260] = {0};
	XftGlyphFontSpec spec = {0};
	Glyph base = { .u = 'x', .fg = 1, .bg = 0, .ucolor = TRUECOLOR(1, 2, 3) };
	int i;

	dc.col = colors;
	setcolor(&colors[1], 0xabcdef);
	setcolor(&colors[2], 0x654321);
	dc.font.ascent = 10;
	dc.font.descent = 3;
	win.w = win.tw = 800;
	win.h = win.th = 600;
	win.cw = 8;
	win.ch = 16;
	tnew(20, 4);
	testtempcolors();
	setcolor(&colors[1], 0xabcdef);
	setcolor(&colors[2], 0x654321);
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
		setcolor(&colors[defaultfg], 0xeeeeee);
		setcolor(&colors[defaultbg], 0x222222);
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
		/* Explicit link labels are blue and underlined on their original
		 * background, retaining their attributes in stored cells. URL feedback still
		 * inverts the link's foreground and background. */
		line = testrender_text(20, "\033[5;7;8m\033]8;;https://example.com\033\\label\033]8;;\033\\\033[0m plain");
		failalloc = 0;
		win.mode |= MODE_BLINK;
		xdrawline(line, 0, 0, 20);
		for (i = 0; i < 5; i++) {
			assert(painted[i] == 0x2462ea && backgrounds[i] == colors[defaultbg].pixel);
			assert(line[i].mode & ATTR_REVERSE);
			assert(line[i].mode & ATTR_BLINK);
		}
		assert(stroke == 0x2462ea);
		win.mode |= MODE_HIDE;
		xdrawcursor(0, 0, line[0], 0, 0, line[0]);
		assert(painted[0] == 0x2462ea && backgrounds[0] == colors[defaultbg].pixel);
		win.mode &= ~MODE_HIDE;
		tflashurl(0, 0, "https://example.com");
		xdrawline(line, 0, 0, 20);
		assert(painted[0] == colors[defaultbg].pixel && backgrounds[0] == 0x2462ea);
		tclearurlflash();
		/* Search colors stay visible over reversed, concealed and blinking
		 * text, and never modify the terminal's stored glyph attributes.
		 * The current match, the last, is bright yellow; others in view
		 * are muted amber. */
		line = testrender_text(20, "before \033[5;7;8mneedle\033[0m needle");
		failalloc = 0;
		tsearchstart();
		tsearchset("needle");
		testrender_marks();
		win.mode |= MODE_BLINK;
		xdrawline(line, 0, 0, 20);
		for (i = 7; i <= 12; i++) {
			assert(painted[i] == 0xfee39f && backgrounds[i] == 0x6f5717);
			assert(line[i].mode & ATTR_REVERSE);
			assert(line[i].mode & ATTR_BLINK);
		}
		for (i = 14; i <= 19; i++)
			assert(painted[i] == 0 && backgrounds[i] == 0xfeef27);
		assert(painted[0] == colors[defaultfg].pixel);
		assert(painted[13] == colors[defaultfg].pixel);
		/* The colors are configurable. */
		searchmatchcolor = 0x112233;
		searchmatchtextcolor = 0xddeeff;
		searchothermatchcolor = 0x445566;
		searchothermatchtextcolor = 0xaabbcc;
		testrender_marks();
		xdrawline(line, 0, 0, 20);
		assert(painted[7] == 0xa9bacb && backgrounds[7] == 0x435465);
		assert(painted[14] == 0xdcedfe && backgrounds[14] == 0x102132);
		searchmatchcolor = 0xfff028;
		searchmatchtextcolor = 0x000000;
		searchothermatchcolor = 0x705818;
		searchothermatchtextcolor = 0xffe4a0;
		tsearchend(0);
		win.mode &= ~MODE_BLINK;
		free(xw.specbuf);
	}
	puts("PASS: underline styles, RGB cache reuse/eviction, fallback, balanced frees and a reused GC");
	testvisibility();
	testconcealedurl();
	testundercurl();
	testpalette();
	return 0;
}
