#include <assert.h>
#include <stddef.h>
#include <fontconfig/fontconfig.h>

/* Fail the Fontconfig calls of the fallback search on demand. */
static int failfc;
enum { FAIL_DUPLICATE = 1, FAIL_CHARSET, FAIL_ADDCHAR, FAIL_SORT, FAIL_MATCH };

static FcPattern *
testduplicate(const FcPattern *p)
{
	return failfc == FAIL_DUPLICATE ? NULL : FcPatternDuplicate(p);
}
static FcCharSet *
testcharset(void)
{
	return failfc == FAIL_CHARSET ? NULL : FcCharSetCreate();
}
static FcBool
testaddchar(FcCharSet *c, FcChar32 r)
{
	return failfc == FAIL_ADDCHAR ? FcFalse : FcCharSetAddChar(c, r);
}
static FcFontSet *
testsort(FcConfig *c, FcPattern *p, FcBool trim, FcCharSet **cs, FcResult *r)
{
	return failfc == FAIL_SORT ? NULL : FcFontSort(c, p, trim, cs, r);
}
static FcPattern *
testmatch(FcConfig *c, FcFontSet **s, int n, FcPattern *p, FcResult *r)
{
	return failfc == FAIL_MATCH ? NULL : FcFontSetMatch(c, s, n, p, r);
}
#define FcPatternDuplicate testduplicate
#define FcCharSetCreate testcharset
#define FcCharSetAddChar testaddchar
#define FcFontSort testsort
#define FcFontSetMatch testmatch

#define VERSION "test"
#define main st_main
#include "../x.c"
#undef main

enum { UNCOVERED = 0x100000 }; /* no font has this character or later ones */
enum { FALLBACKONLY = 0xF0000 }; /* the fallback found for it has these */
static XftFont textfonts[4], fallbackfont, monofont;
static int available = 1, fixedsize, failopen, loads, livefonts, ascii_queries;

/* Exercise the real glyph selector with deterministic font coverage/metrics. */
void
XftDefaultSubstitute(Display *d, int screen, FcPattern *pattern)
{
	FcDefaultSubstitute(pattern);
}

FcPattern *
FcFontMatch(FcConfig *config, FcPattern *pattern, FcResult *result)
{
	FcPattern *match = FcPatternDuplicate(pattern);

	FcPatternDel(match, FC_COLOR);
	FcPatternAddBool(match, FC_COLOR, available);
	*result = FcResultMatch;
	return match;
}

XftFont *
XftFontOpenPattern(Display *d, FcPattern *pattern)
{
	XftFont *font;
	double size;

	loads++;
	if (failopen)
		return NULL;
	assert(FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &size) == FcResultMatch);
	font = calloc(1, sizeof(*font));
	assert(font);
	font->pattern = pattern;
	font->height = font->max_advance_width = fixedsize ? 100 : (int)ceil(size * 1.2);
	font->ascent = font->height * 3 / 4;
	font->descent = font->height - font->ascent;
	livefonts++;
	return font;
}

static int
istextfont(XftFont *font)
{
	int i;

	for (i = 0; i < LEN(textfonts); i++)
		if (font == &textfonts[i])
			return 1;
	return font == &fallbackfont || font == &monofont;
}

void
XftFontClose(Display *d, XftFont *font)
{
	if (istextfont(font))
		return;
	assert(font && livefonts > 0);
	FcPatternDestroy(font->pattern);
	free(font);
	livefonts--;
}

FT_UInt
XftCharIndex(Display *d, XftFont *font, FcChar32 rune)
{
	if (rune == 0xFFFD && istextfont(font))
		return 5;
	if (rune >= UNCOVERED)
		return 0;
	if (rune >= FALLBACKONLY)
		return istextfont(font) ? 0 : 6;
	if (font == &fallbackfont)
		return 2;
	if (font == &monofont)
		return 4;
	if (istextfont(font))
		return rune == 0x1F680 ? 0 : 1;
	if (rune < 128)
		ascii_queries++;
	return rune < 128 || rune == 0x263A || rune == 0x2665 || rune == 0x1F600 ? 3 : 0;
}

static void
setup(void)
{
	static int initialized;
	Font *fonts[] = {&dc.font, &dc.ifont, &dc.bfont, &dc.ibfont};
	int i;

	if (initialized) {
		xunloadfonts();
		assert(livefonts == 0);
	}
	initialized = 1;
	for (i = 0; i < LEN(fonts); i++) {
		fonts[i]->match = &textfonts[i];
		fonts[i]->pattern = FcNameParse((const FcChar8 *)"monospace");
		fonts[i]->ascent = 12;
		fonts[i]->set = NULL;
	}
	if (!frc)
		frc = calloc(1, sizeof(*frc));
	frccap = frclen = 1;
	frc[0] = (Fontcache){ .font = &fallbackfont, .flags = FRC_NORMAL };
	memset(uncovered, 0, sizeof(uncovered));
	borderpx = 0;
	win.cw = 8;
	win.tw = 80 * win.cw;
	win.ch = 16;
	usedfontsize = 13;
	emojifont = "Noto Color Emoji";
	available = 1;
	fixedsize = failopen = loads = ascii_queries = 0;
}

static XftGlyphFontSpec
spec(Rune rune, int mode)
{
	Glyph glyph = { .u = rune, .mode = mode };
	XftGlyphFontSpec result;

	assert(xmakeglyphfontspecs(&result, &glyph, 1, 0, 0) == 1);
	return result;
}

int
main(void)
{
	FcConfig *config = FcConfigCreate();
	XftGlyphFontSpec s, specs[5], specs32[32];
	Glyph mixed[] = {
		{.u = 'A'}, {.u = 0x263A, .mode = ATTR_ALTPRES}, {.u = 0x1F600, .mode = ATTR_WIDE},
		{.u = 0, .mode = ATTR_WDUMMY}, {.u = 0x4E2D}, {.u = 'B'}
	};
	int i, before;
	const int styles[] = {0, ATTR_BOLD, ATTR_ITALIC, ATTR_BOLD | ATTR_ITALIC};

	assert(config && FcConfigSetCurrent(config));
	FcConfigDestroy(config);
	setup();
	/* ASCII, including emoji keycap bases, stays in the primary font. */
	for (i = 32; i < 127; i++)
		assert(spec(i, 0).font == &textfonts[0]);
	assert(!loads && !ascii_queries);

	/* Text-default symbols keep the primary font without U+FE0F. */
	assert(spec(0x263A, 0).font == &textfonts[0]);
	assert(spec(0x2665, 0).font == &textfonts[0]);
	assert(!loads);

	/* A smiley overrides an existing primary-font glyph in every text style. */
	for (i = 0; i < LEN(styles); i++) {
		s = spec(0x263A, styles[i] | ATTR_ALTPRES);
		assert(s.font == emoji[0].font && s.glyph == 3);
		assert(s.font->max_advance_width <= win.cw && s.font->height <= win.ch);
		assert(s.y - s.font->ascent >= 0 && s.y + s.font->descent <= win.ch);
	}
	assert(spec(0x2665, ATTR_ALTPRES).font == emoji[0].font);
	before = loads;
	spec(0x263A, ATTR_ALTPRES);
	assert(loads == before); /* reuse the cached face */

	/* Wide emoji have their own fitting size; all following cells stay aligned. */
	assert(xmakeglyphfontspecs(specs, mixed, LEN(mixed), 1, 1) == LEN(specs));
	assert(specs[0].font == &textfonts[0] && specs[0].x == 8);
	assert(specs[1].font == emoji[0].font && specs[1].x == 16);
	assert(specs[2].font == emoji[1].font && specs[2].x == 24);
	assert(specs[2].font->max_advance_width <= 2 * win.cw);
	assert(specs[2].font->height <= win.ch);
	assert(specs[3].font == &textfonts[0] && specs[3].x == 40);
	assert(specs[4].font == &textfonts[0] && specs[4].x == 48);
	/* A dummy at the wrap boundary never becomes an extra glyph. */
	mixed[3].mode |= ATTR_WRAP;
	assert(xmakeglyphfontspecs(specs, mixed, LEN(mixed), 1, 1) == LEN(specs));
	assert(specs[3].x == 40 && specs[4].x == 48);
	mixed[3].mode = ATTR_WDUMMY;
	assert(!ascii_queries);

	/* U+FE0E keeps a wide emoji in the text fonts. */
	setup();
	assert(spec(0x1F600, ATTR_WIDE | ATTR_ALTPRES).font == &textfonts[0]);
	assert(!loads && !emoji[1].loaded);
	{
		FcFontSet *set = FcFontSetCreate();
		FcPattern *color = FcPatternCreate(), *mono = FcPatternCreate();
		FcPattern *other = FcPatternCreate(), *want = FcPatternCreate(), *got;
		FcCharSet *has = FcCharSetCreate(), *lacks = FcCharSetCreate();
		FcChar8 *family;

		FcCharSetAddChar(has, 0x1F600);
		FcCharSetAddChar(lacks, 'a');
		FcPatternAddString(other, FC_FAMILY, (const FcChar8 *)"Other");
		FcPatternAddCharSet(other, FC_CHARSET, lacks);
		FcPatternAddString(color, FC_FAMILY, (const FcChar8 *)"Color");
		FcPatternAddCharSet(color, FC_CHARSET, has);
		FcPatternAddBool(color, FC_COLOR, FcTrue);
		FcPatternAddString(mono, FC_FAMILY, (const FcChar8 *)"Mono");
		FcPatternAddCharSet(mono, FC_CHARSET, has);
		FcFontSetAdd(set, other);
		FcFontSetAdd(set, color);
		assert(!xtextfallback(set, want, 0x1F600));
		FcFontSetAdd(set, mono);
		got = xtextfallback(set, want, 0x1F600);
		assert(got && FcPatternGetString(got, FC_FAMILY, 0, &family) == FcResultMatch);
		assert(!strcmp((char *)family, "Mono"));
		assert(!xtextfallback(NULL, want, 0x1F600));

		/* Fontconfig's own choice stands unless it is a color font. */
		{
			FcFontSet *best = FcFontSetCreate(), *sets[] = { best };
			FcPattern *mono2 = FcPatternDuplicate(mono), *m;
			Font f = { .textset = set };
			FcChar8 *name;

			FcPatternDel(mono2, FC_FAMILY);
			FcPatternAddString(mono2, FC_FAMILY, (const FcChar8 *)"Mono2");
			FcFontSetAdd(best, mono2);
			m = xfallbackmatch(&f, sets, want, 0x1F600, 1);
			assert(m && FcPatternGetString(m, FC_FAMILY, 0, &name) == FcResultMatch);
			assert(!strcmp((char *)name, "Mono2"));
			FcPatternDestroy(m);
			FcFontSetDestroy(best);
			best = FcFontSetCreate();
			sets[0] = best;
			FcFontSetAdd(best, FcPatternDuplicate(color));
			m = xfallbackmatch(&f, sets, want, 0x1F600, 0);
			assert(m && FcPatternGetString(m, FC_FAMILY, 0, &name) == FcResultMatch);
			assert(!strcmp((char *)name, "Color"));
			FcPatternDestroy(m);
			m = xfallbackmatch(&f, sets, want, 0x1F600, 1);
			assert(m && FcPatternGetString(m, FC_FAMILY, 0, &name) == FcResultMatch);
			assert(!strcmp((char *)name, "Mono"));
			FcPatternDestroy(m);
			FcFontSetDestroy(best);
		}
		FcPatternDestroy(got);
		FcPatternDestroy(want);
		FcCharSetDestroy(has);
		FcCharSetDestroy(lacks);
		FcFontSetDestroy(set);
	}
	setup();

	/* A color fallback cached for one text-presentation rune serves only it. */
	frc = xrealloc(frc, 2 * sizeof(*frc));
	frccap = frclen = 2;
	frc[0] = (Fontcache){ .font = &fallbackfont, .flags = FRC_TEXT,
	                      .unicodep = 0x1F600, .color = 1 };
	frc[1] = (Fontcache){ .font = &monofont, .flags = FRC_TEXT };
	assert(spec(0x1F680, ATTR_WIDE | ATTR_ALTPRES).font == &monofont);
	frc[0].unicodep = 0x1F680;
	assert(spec(0x1F680, ATTR_WIDE | ATTR_ALTPRES).font == &fallbackfont);
	frc[0].unicodep = 0x1F600;
	frc[0].color = 0;
	assert(spec(0x1F680, ATTR_WIDE | ATTR_ALTPRES).font == &fallbackfont);
	setup();

	/* Unsupported symbols retain normal primary and cached fallback lookup. */
	assert(spec(0xE9, 0).font == &textfonts[0]);
	assert(spec(0x4E2D, 0).font == &textfonts[0]);
	assert(spec(0x1F680, ATTR_WIDE).font == &fallbackfont);

	/* Missing color fonts are optional, and failed attempts are cached. */
	setup();
	available = 0;
	assert(spec(0x263A, ATTR_ALTPRES).font == &textfonts[0]);
	assert(!loads && emoji[0].loaded);
	assert(spec(0x1F680, ATTR_WIDE).font == &fallbackfont);
	assert(!loads && emoji[1].loaded);
	setup();
	failopen = 1;
	assert(spec(0x263A, ATTR_ALTPRES).font == &textfonts[0]);
	assert(spec(0x263A, ATTR_ALTPRES).font == &textfonts[0]);
	assert(loads == 1 && !livefonts);

	/* A font that cannot fit is rejected after bounded retries. */
	setup();
	fixedsize = 1;
	assert(spec(0x263A, ATTR_ALTPRES).font == &textfonts[0]);
	assert(loads == 3 && !livefonts);

	/* Zoom's font unload invalidates both cached sizes, then uses new metrics. */
	setup();
	usedfontsize = 20;
	win.cw = 12;
	win.ch = 24;
	s = spec(0x1F600, ATTR_WIDE);
	assert(s.font == emoji[1].font && s.font->height == 24);
	s = spec(0x263A, ATTR_ALTPRES);
	assert(s.font == emoji[0].font && s.font->max_advance_width <= 12);
	setup();
	emojifont = NULL;
	assert(spec(0x263A, ATTR_ALTPRES).font == &textfonts[0] && !loads);
	emojifont = "";
	assert(spec(0x263A, ATTR_ALTPRES).font == &textfonts[0] && !loads);

	/* Characters no font covers cannot grow the cache without end. */
	setup();
	dc.font.set = FcFontSetCreate();
	FcFontSetAdd(dc.font.set, FcNameParse((const FcChar8 *)"Fallback:pixelsize=13"));
	before = frclen;
	for (i = 0; i < 3 * FRC_MAX + 1; i++) {
		s = spec(UNCOVERED + i, 0);
		assert(s.font == &textfonts[0] && !s.glyph);
		assert(frclen == before && !livefonts);
	}
	/* They are not searched for again each time they are drawn. */
	loads = 0;
	for (i = 0; i < 3 * FRC_MAX + 1; i++)
		assert(spec(UNCOVERED + i, 0).font == &textfonts[0]);
	assert(!loads);
	/* Each style looks for itself, and zooming starts over. */
	dc.bfont.set = FcFontSetCreate();
	FcFontSetAdd(dc.bfont.set, FcNameParse((const FcChar8 *)"Fallback:pixelsize=13"));
	assert(spec(UNCOVERED, ATTR_BOLD).font == &textfonts[2] && loads == 1);
	xclearfontcache();
	assert(spec(UNCOVERED, 0).font == &textfonts[0] && loads == 2);
	/* A full cache starts over, but not while a row still uses its fonts. */
	{
		Glyph row[32];

		frc = xrealloc(frc, FRC_MAX * sizeof(*frc));
		frccap = FRC_MAX;
		for (frclen = 0; frclen < FRC_MAX - 1; frclen++)
			frc[frclen] = (Fontcache){ .font = &monofont, .flags = FRC_ITALIC };
		for (i = 0; i < LEN(row); i++)
			row[i] = (Glyph){ .u = FALLBACKONLY + i };
		assert(xmakeglyphfontspecs(specs32, row, LEN(row), 0, 0) == LEN(row));
		assert(frclen == FRC_MAX);
		for (i = 0; i < LEN(row); i++)
			assert(specs32[i].font == frc[FRC_MAX - 1].font && specs32[i].glyph == 6);
		assert(xmakeglyphfontspecs(specs32, row, LEN(row), 0, 0) == LEN(row));
		assert(frclen == 1);
	}

	/* A fallback that cannot be loaded leaves U+FFFD and nothing behind. */
	setup();
	frclen = 0;
	dc.font.set = FcFontSetCreate();
	FcFontSetAdd(dc.font.set, FcNameParse((const FcChar8 *)"Fallback:pixelsize=13"));
	before = livefonts;
	for (i = FAIL_DUPLICATE; i <= FAIL_MATCH; i++) {
		FcFontSet *set = dc.font.set;

		failfc = i;
		if (i == FAIL_SORT)
			dc.font.set = NULL;
		s = spec(UNCOVERED, 0);
		assert(s.font == &textfonts[0] && s.glyph == 5);
		assert(!frclen && livefonts == before);
		if (i == FAIL_SORT)
			dc.font.set = set;
	}
	failfc = 0;
	failopen = 1;
	s = spec(UNCOVERED, 0);
	assert(s.font == &textfonts[0] && s.glyph == 5 && !frclen);
	/* The next glyph of the run stays aligned, and it recovers later. */
	{
		Glyph row[] = { {.u = UNCOVERED}, {.u = 'x'} };

		assert(xmakeglyphfontspecs(specs, row, LEN(row), 0, 0) == LEN(row));
		assert(specs[0].x == 0 && specs[1].x == win.cw);
	}
	failopen = 0;
	s = spec(UNCOVERED, 0);
	assert(s.font == &textfonts[0] && !s.glyph && !frclen && !failopen);

	xunloadfonts();
	assert(!livefonts);
	free(frc);
	puts("PASS: emoji font priority, presentation selection, primary/fallback preservation, styles, cell fitting, alignment, optional fonts, zoom cache cleanup, the fallback cache limit and fallback failures");
	return 0;
}
