#include <stdlib.h>
static size_t largest;
static int watching;
static void *observed_malloc(size_t size)
{
	if (watching && size > largest)
		largest = size;
	return malloc(size);
}
static void *observed_realloc(void *ptr, size_t size)
{
	if (watching && size > largest)
		largest = size;
	return realloc(ptr, size);
}
#define malloc observed_malloc
#define realloc observed_realloc
static unsigned long rowvisits;
#define ROWVISIT() (rowvisits++)
#include "terminal.h"

#ifndef FUZZLONG_RUNS
#define FUZZLONG_RUNS 200
#endif

static int fuzzcap = -1, fuzzseed, fuzzstep, fuzzcols, fuzzrows, fuzzvaried;

static void
expect(int col, int row, const char *want)
{
	char *got = tgeturl(col, row);

	if ((want && (!got || strcmp(got, want))) || (!want && got)) {
		fprintf(stderr, "URL at %d,%d: expected %s, got %s\n", col, row,
		        want ? want : "(none)", got ? got : "(none)");
		fprintf(stderr, "history %d view %d size %dx%d\n", sb.len, sb.view_offset, term.col, term.row);
		fprintf(stderr, "fuzz initial %dx%d capacity %d seed %d step %d varied %d; current limit %d\n",
		        fuzzcols, fuzzrows, fuzzcap, fuzzseed, fuzzstep, fuzzvaried, sb.limit);
		for (int h = MAX(0, sb.len - 8); h < sb.len; h++) {
			Line line = sb_get(h);
			for (int x = 0; x < term.col; x++)
				fputc(line[x].u < 128 ? line[x].u : '?', stderr);
			fprintf(stderr, " history wrap=%d\n", !!(line[term.col - 1].mode & ATTR_WRAP));
		}
		for (int y = 0; y < term.row; y++) {
			for (int x = 0; x < term.col; x++)
				fputc(renderline(y)[x].u < 128 ? renderline(y)[x].u : '?', stderr);
			fputc('\n', stderr);
		}
		abort();
	}
	assert(!!turl(col, row) == !!want);
	free(got);
}

static void
expectwrapped(int start, const char *url)
{
	int i;

	for (i = 0; url[i]; i++)
		expect((start + i) % term.col, (start + i) / term.col, url);
	expect((start + i) % term.col, (start + i) / term.col, NULL);
}

static int refcapped;

/* A deliberately naive parser with the same rules, sharing no code with phist. */
static int
refmatch(const char *text, size_t length, size_t *cursor, size_t *begin, size_t *finish)
{
	size_t start, prefix, end, trimmed, limit;
	int p, parens, brackets;
	uchar c;

	while (*cursor < length) {
		start = (*cursor)++;
		for (p = 0, prefix = 0; urlprefixes[p] && !prefix; p++)
			if (strlen(urlprefixes[p]) <= length - start &&
			    !strncasecmp(text + start, urlprefixes[p], strlen(urlprefixes[p])))
				prefix = strlen(urlprefixes[p]);
		if (!prefix)
			continue;
		limit = start + URLTARGET_MAX < length ? start + URLTARGET_MAX : length;
		parens = brackets = 0;
		for (end = trimmed = start + prefix; end < limit; end++) {
			c = text[end];
			if (!c || !strchr(urlchars, c))
				break;
			parens += (c == '(') - (c == ')');
			brackets += (c == '[') - (c == ']');
			if (parens < 0 || brackets < 0)
				break;
			if (!strchr(".,;:!?'", c))
				trimmed = end + 1;
		}
		refcapped |= end == limit && limit < length;
		*cursor = trimmed;
		if (trimmed > start + prefix) {
			*begin = start;
			*finish = trimmed;
			return 1;
		}
	}
	return 0;
}

static int
refwraps(int y)
{
	Line line = viewline(y);

	return line && line[term.col - 1].mode & ATTR_WRAP;
}

/*
 * Compare every visible cell with a naive parse of its entire logical line.
 * Lines where a URL reaches the length cap are skipped: there, nested schemes
 * may legitimately depend on how much context phist reads.
 */
static void
expectreference(void)
{
	int y = 0, first, last, r, x, count, capacity = 0;
	size_t length, cursor, start, end, pos, *spans = NULL;
	char *text, *want;
	Line line;

	while (y < term.row) {
		for (first = y; refwraps(first - 1); first--)
			;
		for (last = y; refwraps(last) && viewline(last + 1); last++)
			;
		text = xmalloc((size_t)(last - first + 1) * term.col + 1);
		for (length = 0, r = first; r <= last; r++) {
			line = renderline(r);
			for (x = 0; x < term.col; x++)
				text[length++] = !(line[x].mode & ATTR_WDUMMY) &&
					line[x].u > 0 && line[x].u < 128 ? line[x].u : ' ';
		}
		refcapped = 0;
		for (count = 0, cursor = 0; refmatch(text, length, &cursor, &start, &end); count++) {
			if (2 * count + 2 > capacity) {
				capacity = capacity ? 2 * capacity : 16;
				spans = xrealloc(spans, capacity * sizeof(*spans));
			}
			spans[2 * count] = start;
			spans[2 * count + 1] = end;
		}
		for (r = MAX(first, 0); !refcapped && r <= MIN(last, term.row - 1); r++) {
			for (x = 0; x < term.col; x++) {
				pos = (size_t)(r - first) * term.col + x;
				want = NULL;
				for (int i = 0; i < count && !want; i++)
					if (spans[2 * i] <= pos && pos < spans[2 * i + 1]) {
						want = xmalloc(spans[2 * i + 1] - spans[2 * i] + 1);
						memcpy(want, text + spans[2 * i], spans[2 * i + 1] - spans[2 * i]);
						want[spans[2 * i + 1] - spans[2 * i]] = '\0';
					}
				expect(x, r, want);
				free(want);
			}
		}
		free(text);
		y = last + 1;
	}
	free(spans);
}

/* Compare results after varied operations with a fresh parse and the reference. */
static void
expectfresh(void)
{
	char **snapshot = xmalloc((size_t)term.row * term.col * sizeof(*snapshot));
	int x, y;

	for (y = 0; y < term.row; y++)
		for (x = 0; x < term.col; x++)
			snapshot[y * term.col + x] = tgeturl(x, y);
	urls.dirty = 1;
	for (y = 0; y < term.row; y++)
		for (x = 0; x < term.col; x++) {
			expect(x, y, snapshot[y * term.col + x]);
			free(snapshot[y * term.col + x]);
		}
	free(snapshot);
	expectreference();
}

static void
fuzzurls(int cols, int rows, int limit, unsigned int seed, int steps, int varied)
{
	unsigned int random = seed;
	char text[256];
	Arg step = { .i = 1 }, oldest = { .i = 100000 };
	int i, op;

	reset(cols, rows);
	sb_setlimit(limit);
	fuzzcap = limit;
	fuzzseed = seed;
	fuzzcols = cols;
	fuzzrows = rows;
	fuzzvaried = varied;
	if (varied) {
		put("old\r\nold\r\nhttp://host/http://inner/a_(b)[x] tail\r\n");
		kscrollup(&oldest);
		urlupdate();
	}
	for (i = 0; i < steps; i++) {
		fuzzstep = i;
		random = random * 1664525U + 1013904223U;
		op = (random >> 16) % (varied ? 24 : 16);
		step.i = varied ? 1 + (random >> 24) % (term.row + 2) : 1;
		switch (op) {
		case 0: put("http://example.org/abcdefghij"); break;
		case 1: put("\r\n"); break;
		case 2: put("tail"); break;
		case 3: kscrollup(&step); break;
		case 4: kscrolldown(&step); break;
		case 5:
			snprintf(text, sizeof(text), "\033[%u;%uHX",
			         1 + (random >> 8) % term.row, 1 + random % term.col);
			put(text);
			break;
		case 6: put("\033[K"); break;
		case 7: put("\033[H\033M"); break;
		case 8: put("\033[S"); break;
		case 9: put("\033[T"); break;
		case 10: put("\033[L"); break;
		case 11: put("\033[M"); break;
		case 12: put("ordinary output without a URL"); break;
		case 13: put("https://host/path\r\n"); break;
		case 14: put(varied ? "xhttp://host/http://inner/a_(b)?q=[v]" : "xhttp://host/"); break;
		case 15: urlupdate(); break;
		case 16: tresize(1 + random % 24, 1 + (random >> 8) % 9); break;
		case 17: put("\033[?1049hhttp://alt/path \033[?1049l"); break;
		case 18: put("\033[H\033[2J"); break;
		case 19:
			if (term.row > 1) {
				snprintf(text, sizeof(text), "\033[2;%dr\033[%d;1H\n\033[r", term.row, term.row);
				put(text);
			} else {
				put("\n");
			}
			break;
		case 20: put(term.col > 1 ? "\303\251 \347\225\214 http://wide/" : "\303\251 http://narrow/"); break;
		case 21: put("http://a/(x[http://b/]y)http://c/ "); break;
		case 22: put("http://host/one\r\nhttps://host/two\r\nhttp://host/http://nested/\r\n"); break;
		case 23: sb_setlimit((random >> 8) % 66); break;
		}
		if (i % (varied ? 13 : 5) == 0)
			expectfresh();
	}
	expectfresh();
}

/* Long URLs with nested schemes past their targets, steady eviction and far scrolling. */
static void
fuzzlong(unsigned int seed, int steps)
{
	static char longurl[3001];
	const char *pieces[] = {
		"http://a/", "http://", "(", ")", "[", "]", "aa", " ", ".,!?", "\r\n", "/",
		"http://)x/http://y/", "\303\251", longurl, longurl + 1000
	};
	unsigned int random = seed;
	Arg top = { .i = 100000 }, move;
	int i, n, op, limit;

#define NEXT() (random = random * 1664525U + 1013904223U, random >> 8)
	if (!*longurl) {
		memset(longurl, 'a', 3000);
		memcpy(longurl, "http://long/", 12);
		memcpy(longurl + URLTARGET_MAX + 2, "http://nested/", 14);
	}
	fuzzcols = 1 + NEXT() % 90;
	fuzzrows = 1 + NEXT() % 8;
	limit = 4 + NEXT() % 400;
	fuzzcap = limit;
	fuzzseed = seed;
	fuzzvaried = 2;
	reset(fuzzcols, fuzzrows);
	sb_setlimit(1000);
	for (i = 0, n = 40 + NEXT() % 400; i < n; i++)
		put(pieces[NEXT() % LEN(pieces)]);
	put(" \r\none\r\ntwo\r\nthree");
	kscrollup(&top);
	urlupdate();
	sb_setlimit(NEXT() % 2 ? sb.len : limit);
	for (i = 0; i < steps; i++) {
		fuzzstep = i;
		op = NEXT() % 10;
		move.i = 1 + NEXT() % (term.row + 2);
		switch (op) {
		case 0: case 1: put("\r\nlog"); break;
		case 2: put(pieces[NEXT() % LEN(pieces)]); break;
		case 3: kscrollup(&move); break;
		case 4: kscrolldown(&move); break;
		case 5: urlupdate(); break;
		case 6:
			kscrollup(&top);
			move.i = NEXT() % 60;
			kscrolldown(&move);
			break;
		case 7: put("\r\n"); break;
		case 8: put(pieces[NEXT() % 4]); put("\r\n"); break;
		case 9: if (NEXT() % 8 == 0) sb_setlimit(1 + NEXT() % 80); break;
		}
		if (NEXT() % 3 == 0)
			urlupdate();
		if (NEXT() % 2 == 0)
			expectfresh();
	}
	expectfresh();
#undef NEXT
}

/* Compare the parser with the reference from random cuts of random text. */
static void
fuzzparser(int iterations)
{
	const char *pieces[] = {
		"http://a/", "https://b/", "http://", "(", ")", "[", "]", "a", " ", ".", ",", ":",
		"'", "?", "/", "HTTP://C/", "http://)x/http://y/"
	};
	unsigned int random = 7;
	size_t length, target, n, cut, c1, c2, s1, s2, e1, e2;
	char *text = xmalloc(6000);
	int i, k, r1, r2;

	for (i = 0; i < iterations; i++) {
		random = random * 1664525U + 1013904223U;
		target = 1 + (random >> 8) % 5000;
		for (length = 0; length < target; length += n) {
			random = random * 1664525U + 1013904223U;
			n = strlen(pieces[(random >> 8) % LEN(pieces)]);
			memcpy(text + length, pieces[(random >> 8) % LEN(pieces)], n);
		}
		text[length] = '\0';
		for (k = 0; k < 6; k++) {
			random = random * 1664525U + 1013904223U;
			cut = k ? (random >> 8) % (length + 1) : 0;
			c1 = c2 = cut;
			do {
				r1 = refmatch(text, length, &c1, &s1, &e1);
				r2 = urlmatch(text, length, &c2, &s2, &e2);
				assert(r1 == r2 && c1 == c2 && (!r1 || (s1 == s2 && e1 == e2)));
			} while (r1);
		}
	}
	free(text);
}

int
main(void)
{
	const char *url = "https://example.org/long/path?q=one&next=two#anchor";
	const char *first = "http://first.example/a";
	const char *second = "https://second.example/b";
	char text[256];
	char *longurl, *longtarget;
	Arg top = { .i = 1000 };
	int i, x, y, found;
	unsigned int random = 1;
	Arg step = { .i = 1 };
	Arg far = { .i = 5990 };

	setlocale(LC_CTYPE, "");
	/* Only "scheme://" prefixes are accepted; overlapping schemes are harmless. */
	{
		const char *rejected[] = { "tp:", "::", "x y", "ftp", "1abc://", "http://x", "mailto:", "ftp:/" };
		int prefix;

		for (prefix = 0; prefix < LEN(rejected); prefix++) {
			urlprefixes[1] = rejected[prefix];
			reset(70, 3);
			put("http://a/ tp:b ::c x y:d ftp:e 1abc://i mailto:h ftp:/j");
			expect(0, 0, "http://a/");
			for (x = 9; x < term.col; x++)
				expect(x, 0, NULL);
		}
		urlprefixes[1] = "ftp://";
		reset(40, 3);
		put("http://a/ ftp://b/");
		expect(0, 0, "http://a/");
		expect(10, 0, "ftp://b/");
		urlprefixes[1] = "tp://";
		reset(40, 3);
		put("http://a/ tp://b/");
		expect(0, 0, "http://a/");
		expect(10, 0, "tp://b/");
		urlprefixes[1] = "https://";
	}
	/* A long line of nested schemes and brackets matches the reference throughout. */
	{
		const char *pieces[] = { "http://a/", "http://", "(", ")", "[", "]", "aa", " ", ".,!?" };
		char sample[20001];
		size_t used = 0, n;
		Arg page = { .i = 31 };

		while (used + 16 < sizeof(sample)) {
			random = random * 1664525U + 1013904223U;
			const char *piece = pieces[(random >> 16) % LEN(pieces)];
			n = strlen(piece);
			memcpy(sample + used, piece, n);
			used += n;
		}
		sample[used] = '\0';
		reset(20, 5);
		sb_setlimit(2048);
		put(sample);
		put(" \r\none\r\ntwo\r\nthree\r\nfour\r\nfive");
		kscrollup(&top);
		while (sb.view_offset > term.row) {
			expectfresh();
			kscrolldown(&page);
		}
	}
	/* Evicting a hidden prefix also changes its live continuation. */
	reset(10, 3);
	sb_setlimit(1);
	put("http://example.org/abcdefghij\r\n");
	expect(0, 0, "http://example.org/abcdefghij");
	put("\r\n");
	expect(0, 0, NULL);
	expectfresh();
	sb_setlimit(scrollback_lines);
	/* Wheel scrolling must retain pending edits to a hidden URL suffix. */
	reset(10, 3);
	put("old1\r\nold2\r\nhttp://example.org/abcdefghij");
	kscrollup(&step);
	expect(0, 1, "http://example.org/abcdefghij");
	put("X");
	kscrollup(&step);
	expect(0, 2, "http://example.org/abcdefghijX");
	expectfresh();
	/* Removing a blank top row may reconnect a suffix to a history prefix. */
	reset(10, 3);
	sb_setlimit(1);
	put("   https://host/path\r\n\r\n");
	expect(0, 0, "https://host/path");
	put("\033[T");
	expect(0, 1, NULL);
	put("\033[S");
	expect(0, 0, "https://host/path");
	expectfresh();
	sb_setlimit(scrollback_lines);
	/* An evicted outer scheme can expose a nested scheme in history. */
	reset(10, 3);
	put("http://aa/http://b/path \r\none\r\ntwo\r\nthree");
	kscrollup(&top);
	expect(0, 0, "http://aa/http://b/path");
	sb_setlimit(sb.len);
	put("\r\nfour");
	expect(0, 0, "http://b/path");
	expectfresh();
	sb_setlimit(scrollback_lines);
	/* A URL keeps its target while its wrapped line grows far below the view. */
	reset(20, 3);
	sb_setlimit(8192);
	longurl = xmalloc(120001);
	memset(longurl, 'a', 120000);
	memcpy(longurl, "http://host/", 12);
	longurl[120000] = '\0';
	longtarget = xmalloc(URLTARGET_MAX + 1);
	memcpy(longtarget, longurl, URLTARGET_MAX);
	longtarget[URLTARGET_MAX] = '\0';
	twrite(longurl, 80, 0);
	kscrollup(&top);
	assert(turl(0, 0));
	twrite(longurl + 80, 120000 - 80, 0);
	expect(0, 0, longtarget);

	/* Unrelated output below a long wrapped line leaves its target alone. */
	reset(20, 3);
	sb_setlimit(20000);
	twrite(longurl, 120000, 0);
	put("\r\n");
	kscrollup(&far);
	expect(0, 0, longtarget);
	memset(text, 'b', 200);
	for (i = 0; i < 1000; i++) {
		twrite(text, 200, 0);
		urlupdate();
	}
	expect(0, 0, longtarget);
	/* At full history, draw between batches while eviction advances the view. */
	reset(20, 3);
	sb_setlimit(6000);
	memset(longurl, 'a', 120000);
	twrite(longurl, 120000, 0);
	put("\r\n");
	for (i = 0; i < 20; i++)
		put("log\r\n");
	far.i = 20000;
	kscrollup(&far);
	expect(0, 0, NULL);
	assert(sb.len == sb.limit && sb.view_offset == sb.len);
	for (i = 0; i < 500; i++) {
		put("log\r\n");
		urlupdate();
	}
	expect(0, 0, NULL);
	expectfresh();
	kscrolldown(&far);
	put("\r\nhttp://new/");
	expect(0, term.row - 1, "http://new/");
	expectfresh();
	/* A URL ends after URLTARGET_MAX characters; a later scheme starts another. */
	reset(80, 40);
	sb_setlimit(1000);
	longurl[0] = '\0';
	strcat(longurl, "http://host/");
	memset(longurl + 12, 'a', 2600 - 12);
	memcpy(longurl + 2100, "http://x/", 9);
	longurl[2600] = '\0';
	memcpy(longtarget, longurl, URLTARGET_MAX);
	put(longurl);
	expect(0, 0, longtarget);
	expect(47, 25, longtarget);
	expect(48, 25, NULL);
	expect(2100 % 80, 2100 / 80, longurl + 2100);
	expectfresh();
	for (i = 0; i < 80; i++)
		put("\r\nlog");
	kscrollup(&top);
	expect(47, 25, longtarget);
	expect(48, 25, NULL);
	/* The same holds after scrolling the boundary away and back. */
	for (i = 0; i < 2; i++) {
		far.i = 1 + i;
		kscrolldown(&far);
		urlupdate();
		kscrollup(&far);
		expect(47, 25, longtarget);
		expect(48, 25, NULL);
		expect(0, 26, NULL);
	}
	expectfresh();
	/* Evicting a long URL's start leaves a URL past its cap unchanged. */
	reset(80, 4);
	sb_setlimit(1000);
	memset(longurl, 'a', 5000);
	memcpy(longurl, "http://host/", 12);
	memcpy(longurl + 3000, "http://inner/", 13);
	longurl[5000] = '\0';
	put(longurl);
	for (i = 0; i < 10; i++)
		put("\r\nlog");
	kscrollup(&top);
	far.i = 36;
	kscrolldown(&far);
	expect(40, 1, longurl + 3000);
	sb_setlimit(sb.len);
	put("\r\nlog");
	expect(40, 1, longurl + 3000);
	expectfresh();
	far.i = 5990;
	free(longtarget);
	free(longurl);
	sb_setlimit(scrollback_lines);
	/* Clearing or trimming history removes URLs shown from it. */
	reset(20, 3);
	put("http://old/\r\none\r\ntwo\r\nthree");
	kscrollup(&top);
	expect(0, 0, "http://old/");
	sb_clear();
	expect(0, 0, NULL);
	expectfresh();
	reset(20, 3);
	put("http://old/\r\none\r\ntwo\r\nthree");
	kscrollup(&top);
	expect(0, 0, "http://old/");
	sb_setlimit(0);
	expect(0, 0, NULL);
	sb_setlimit(scrollback_lines);

	/* Inserting blank rows breaks a URL's connection to its history prefix. */
	reset(10, 3);
	put("http://example.org/abcdefghij\r\n");
	assert(sb.len == 1);
	expect(0, 0, "http://example.org/abcdefghij");
	put("\033[1T");
	expect(0, 1, NULL);
	expectfresh();
	reset(10, 3);
	put("http://example.org/abcdefghij\r\n");
	expect(0, 0, "http://example.org/abcdefghij");
	put("\033[2T");
	expect(0, 2, NULL);
	expectfresh();
	reset(10, 3);
	put("http://example.org/abcdefghij");
	expect(0, 0, "http://example.org/abcdefghij");
	put("\033[H\033M");
	expect(0, 1, "http://example.org/a");
	expectfresh();

	reset(80, 4);
	put("older\r\nplain\r\nhttp://host/path\r\nplain\r\nplain");
	expect(0, 1, "http://host/path");
	kscrollup(&step);
	expect(0, 2, "http://host/path");
	kscrolldown(&step);
	expect(0, 1, "http://host/path");
	put("\r\nnext");
	expect(0, 0, "http://host/path");
	put("\r\nlast");
	expect(0, 0, NULL);
	expectfresh();

	reset(10, 3);
	put(url);
	expect(0, 0, url);
	kscrollup(&step);
	expect(0, 0, url);
	kscrolldown(&step);
	expect(0, 0, url);
	expectfresh();

	/* A wrapped viewport edge need not reach the live output being edited. */
	reset(10, 3);
	put("older\r\nhttp://example.org/abcdefghij\r\nother\r\nclock");
	kscrollup(&top);
	expect(0, 1, "http://example.org/abcdefghij");
	assert(renderline(2)[term.col - 1].mode & ATTR_WRAP);
	put(" updates");
	expect(0, 1, "http://example.org/abcdefghij");
	for (i = 0; i < 10; i++) {
		put("\r\nclock");
		expect(0, 1, "http://example.org/abcdefghij");
	}
	expectfresh();

	/* A hard break still in the live screen can turn into a soft wrap. */
	reset(10, 3);
	put("older\r\nhttp://example.org/abcdefghij\r\nother\r\nclock");
	kscrollup(&top);
	expect(0, 1, "http://example.org/abcdefghij");
	put("x");
	put("\033[1;10HXZ");
	expect(0, 1, "http://example.org/abcdefghijXZther");
	expectfresh();

	/* Edits in an actual hidden continuation still refresh the full target. */
	reset(10, 3);
	put(url);
	kscrollup(&top);
	expect(0, 0, url);
	put("z");
	snprintf(text, sizeof(text), "%sz", url);
	expect(0, 0, text);
	expectfresh();

	reset(80, 4);
	put("ordinary output");
	expect(0, 0, NULL);
	put(" updates\033[K");
	expect(0, 0, NULL);
	put("\r\nhttp:/");
	expect(0, 1, NULL);
	put("/example.org/");
	expect(0, 1, "http://example.org/");
	put("\033[3;1Hclock\033[K");
	expect(0, 1, "http://example.org/");
	put("\033[2;8HX");
	expect(0, 1, "http://Xxample.org/");

	reset(80, 4);
	snprintf(text, sizeof(text), "%s %s ", first, second);
	put(text);
	for (i = 0; first[i]; i++)
		expect(i, 0, first);
	expect(i, 0, NULL);
	for (i = 0; second[i]; i++)
		expect(strlen(first) + 1 + i, 0, second);

	/* A scheme as well as its body may span several soft-wrapped rows. */
	reset(7, 12);
	put("xx ");
	put(url);
	put(" ");
	expectwrapped(3, url);
	for (y = 0; y < term.row; y++)
		for (x = 0; x < term.col; x++)
			assert(!(term.line[y][x].mode & ATTR_URL));

	/* Explicit newlines stop URLs, even when the previous row is full. */
	reset(10, 5);
	put("https://ex\r\nample.org");
	expect(0, 0, "https://ex");
	expect(0, 1, NULL);

	/* Surrounding prose is excluded; balanced URL punctuation is retained. */
	reset(80, 4);
	put("(https://example.org/a_(b)). https://[::1]/p, HTTP://host/path! ");
	expect(0, 0, NULL);
	expect(1, 0, "https://example.org/a_(b)");
	expect(26, 0, NULL);
	expect(29, 0, "https://[::1]/p");
	expect(46, 0, "HTTP://host/path");

	/* Unicode preceding a URL must not truncate the rest of the row. */
	reset(80, 4);
	put("\303\251 https://example.org/ ");
	expect(2, 0, "https://example.org/");

	/* A URL can extend above and below the visible scrollback viewport. */
	reset(10, 3);
	put(url);
	put(" ");
	expect(0, 0, url);
	kscrollup(&top);
	expect(0, 0, url);
	expect(9, 2, url);
	/* Switching screens keeps the same history rows in view. */
	tswapscreen();
	expect(0, 0, url);
	kscrolldown(&top);
	assert(renderline(0) == term.line[0]);
	expect(0, 0, NULL);
	kscrollup(&top);
	expect(0, 0, url);
	tswapscreen();
	expect(0, 0, url);

	/* Erasing the prefix also removes highlights on unchanged continuation rows. */
	reset(12, 6);
	put(url);
	put(" ");
	expect(0, 1, url);
	memset(term.dirty, 0, term.row * sizeof(*term.dirty));
	put("\033[H\033[2K");
	expect(0, 1, NULL);
	assert(term.dirty[1]);

	reset(80, 4);
	put(first);
	expect(0, 0, first);
	tswapscreen();
	expect(0, 0, NULL);
	tmoveto(0, 0);
	put(second);
	expect(0, 0, second);
	tswapscreen();
	expect(0, 0, first);

	/* Reflow updates cell hit-testing without inserting newlines in the URL. */
	reset(12, 10);
	put(url);
	put("\r\nafter");
	tresize(20, 10);
	found = 0;
	for (y = 0; y < term.row; y++)
		for (x = 0; x < term.col; x++)
			if (turl(x, y)) {
				expect(x, y, url);
				found++;
			}
	assert(found == strlen(url));

	reset(80, 4);
	put("\033[4:3;58:2::255:0:0mhttps://example.org/\033[0m ");
	expect(0, 0, "https://example.org/");
	assert(USTYLE(term.line[0][0]) == 3 && term.line[0][0].ucolor == TRUECOLOR(255, 0, 0));
	expect(-1, 0, NULL);
	expect(0, term.row, NULL);

	puts("PASS: URLs, wrapping, viewport edges and existing underlines");
	reset(13, 6);
	for (i = 0; i < 4000; i++) {
		random = random * 1664525U + 1013904223U;
		switch ((random >> 16) % 16) {
		case 0: put("http://host/a_(b)?x=1&y=2 "); break;
		case 1: put("https://example.org/\r\n"); break;
		case 2: put("text\r\n"); break;
		case 3: kscrollup(&step); break;
		case 4: kscrolldown(&step); break;
		case 5: put("\033[2;4r\033[4;1H\n\033[r"); break;
		case 6: put("\033[2;1H\033[K"); break;
		case 7: put("\033[1;1H\033[M"); break;
		case 8: put("\033[1;1H\033[L"); break;
		case 9: put("\033[3;2HX\033[P"); break;
		case 10: put("\033[?1049hhttp://alt/ \033[?1049l"); break;
		case 11: put("\033[2S"); break;
		case 12: put("\033[2T"); break;
		case 13: put("\033[H\033M"); break;
		case 14: put("\r\nhttp://tail/ "); break;
		case 15: kscrollup(&top); break;
		}
		if (i < 2000 || i % 7 == 0)
			expectfresh();
	}
	expectfresh();
	puts("PASS: URLs survive viewport/output scrolling and match fresh and whole-line parses");
	/* Work per update is bounded by the view, however long its wrapped line. */
	{
		char *line = xmalloc(4000000), *target;
		Arg inside = { .i = 25000 }, oldest = { .i = 100000 };
		size_t bound, rows;

		for (i = 0; i + 9 <= 4000000; i += 9)
			memcpy(line + i, "http://x/", 9);
		reset(80, 24);
		sb_setlimit(65536);
		twrite(line, 4000000 - 4000000 % 9, 0);
		put("\r\none\r\ntwo\r\nthree");
		kscrollup(&oldest);
		rows = term.row + 2 * ((URLTARGET_MAX + term.col - 1) / term.col) + 1;
		bound = rows * (term.col + 1);
		for (i = 0; i < 2; i++) {
			if (i)
				kscrolldown(&inside);
			urlupdate();
			largest = 0;
			rowvisits = 0;
			watching = 1;
			urls.dirty = 1;
			urlupdate();
			watching = 0;
			assert(largest <= bound && rowvisits <= 8 * rows);
		}
		kscrollup(&oldest);
		target = tgeturl(0, 0);
		assert(target && !strncmp(target, "http://x/", 9) && strlen(target) <= URLTARGET_MAX);
		free(target);
		free(line);
		sb_setlimit(scrollback_lines);
	}
	puts("PASS: URL rows read and memory per update are bounded by the view on a 4 MB line");
	/* Keep the original long seed traces, then cover omitted sizes and limits. */
	{
		int oldlimits[] = { 0, 1, 2, 3, 64, 65536 };
		int limits[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 16, 31, 32, 63, 64, 65, 65536 };
		int sizes[][2] = { {1,1}, {2,3}, {5,2}, {7,4}, {10,3}, {19,8}, {40,12} };
		int cap, seed, size;

		for (cap = 0; cap < LEN(oldlimits); cap++)
			for (seed = 1; seed <= 4; seed++)
				fuzzurls(10, 3, oldlimits[cap], seed, 4000, 0);
		for (size = 0; size < LEN(sizes); size++)
			for (cap = 0; cap < LEN(limits); cap++)
				for (seed = 1; seed <= 32; seed++)
					fuzzurls(sizes[size][0], sizes[size][1], limits[cap], seed, 384, 1);
	}
	for (i = 1; i <= FUZZLONG_RUNS; i++)
		fuzzlong(i, 300);
	fuzzparser(2000);
	sb_setlimit(scrollback_lines);
	puts("PASS: URLs match fresh and whole-line parses across 32 seeds, 18 limits, 7 sizes and resizes");
	puts("PASS: URLs match fresh and whole-line parses with long URLs and far scrolling");
	puts("PASS: the URL parser matches a naive reference parser");
	return 0;
}
