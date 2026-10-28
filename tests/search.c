#include "terminal.h"

static void
find(const char *text)
{
	if (!tsearchactive())
		tsearchstart();
	tsearchset(text);
}

/* The current match is index of count, counting from the top. */
static void
expectcount(int count, int index)
{
	int i;

	assert(tsearchcount(&i) == count && i == index);
}

static void
expectmatch(uint64_t row, int x, uint64_t endrow, int endx)
{
	int y = (int)(row - sb.base) - (sb_maxoffset() - sb.view_offset);

	assert(tsearchactive() && tsearchfound());
	if (search.start.row != row || search.start.x != x ||
	    search.end.row != endrow || search.end.x != endx)
		fprintf(stderr, "match: %llu:%d..%llu:%d, expected %llu:%d..%llu:%d\n",
		        (unsigned long long)search.start.row, search.start.x,
		        (unsigned long long)search.end.row, search.end.x,
		        (unsigned long long)row, x, (unsigned long long)endrow, endx);
	assert(search.start.row == row && search.start.x == x);
	assert(search.end.row == endrow && search.end.x == endx);
	assert(tsearchmatch(x, y) == SEARCH_CURRENT);
	assert(tsearchmatch(x - 1, y) != SEARCH_CURRENT);
}

int
main(void)
{
	Arg up = { .i = 3 };
	uint64_t origin, matchrow;
	char *copy;
	int i, cols, count;

	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	reset(20, 3);
	put("old needle\r\nother\r\nnew needle\r\nlive");
	assert(sb.len == 1);
	find("needle");
	expectmatch(2, 4, 2, 9);
	expectcount(2, 2);
	tsearchnext(-1);
	expectmatch(0, 4, 0, 9);
	expectcount(2, 1);
	tsearchnext(-1); /* Wrap to the newest match. */
	expectmatch(2, 4, 2, 9);
	expectcount(2, 2);
	tsearchnext(1);
	expectmatch(0, 4, 0, 9);
	expectcount(2, 1);
	tsearchnext(1);
	expectmatch(2, 4, 2, 9);
	expectcount(2, 2);
	/* The first and last match, from wherever the current one is. */
	tsearchedge(0);
	expectmatch(0, 4, 0, 9);
	expectcount(2, 1);
	tsearchedge(0);
	expectcount(2, 1);
	tsearchedge(1);
	expectmatch(2, 4, 2, 9);
	expectcount(2, 2);
	find("absent");
	assert(!tsearchfound() && sb.view_offset == 0);
	expectcount(0, 0);
	tsearchedge(0);
	assert(!tsearchfound());
	find("");
	assert(!tsearchfound());
	tsearchend(1);
	assert(!tsearchactive() && !sb.view_offset && !tsearchmatch(4, 1));

	/* Every match in view is marked, overlapping ones too, besides the
	 * current one. Cells between them are not. */
	reset(20, 3);
	put("ababa x\r\naba");
	find("aba");
	expectmatch(1, 0, 1, 2);
	searchmarks();
	for (i = 0; i <= 4; i++)
		assert(tsearchmatch(i, 0) == SEARCH_OTHER);
	assert(!tsearchmatch(5, 0) && !tsearchmatch(6, 0) && !tsearchmatch(3, 1));
	tsearchend(1);
	searchmarks();
	assert(!tsearchmatch(0, 0));

	/* Matches crossing the top or bottom of the view are marked in it;
	 * here soft-wrapped across rows 0-1 and 4-5 of the history. */
	reset(4, 3);
	put("xxneedlexxxxxxxxxxneedle");
	find("needle");
	expectcount(2, 2);
	tsearchedge(0);
	expectmatch(0, 2, 1, 3);
	kscrolldown(&(Arg){ .i = 2 }); /* rows 2-4 */
	searchmarks();
	assert(!tsearchmatch(1, 2));
	assert(tsearchmatch(2, 2) == SEARCH_OTHER && tsearchmatch(3, 2) == SEARCH_OTHER);
	tsearchedge(1);
	kscrollup(&(Arg){ .i = 2 }); /* rows 1-3 */
	searchmarks();
	for (i = 0; i < 4; i++)
		assert(tsearchmatch(i, 0) == SEARCH_OTHER && !tsearchmatch(i, 1));
	tsearchend(1);

	/* Both cells of a wide character are marked. */
	reset(20, 3);
	put("a\347\225\214z \347\225\214z");
	find("\347\225\214z");
	expectmatch(0, 5, 0, 7);
	searchmarks();
	for (i = 1; i <= 3; i++)
		assert(tsearchmatch(i, 0) == SEARCH_OTHER);
	assert(!tsearchmatch(0, 0) && !tsearchmatch(4, 0));
	tsearchend(1);

	/* Output completing a match below a row redraws that row too. */
	reset(4, 3);
	put("xxneedxx");
	find("needle");
	assert(!tsearchfound());
	searchmarks();
	memset(term.dirty, 0, term.row * sizeof(*term.dirty));
	put("\033[2;3Hle");
	assert(!term.dirty[0] && term.dirty[1]);
	searchmarks();
	assert(term.dirty[0] && tsearchmatch(2, 0) == SEARCH_OTHER);
	tsearchend(1);

	/* Page Up and Page Down find the nearest match beyond the rows in
	 * view, however many that skips. On the first or last page, they go
	 * to the first or last match, and then stay there. */
	reset(20, 3);
	for (i = 0; i < 30; i++)
		put(i == 0 || i == 1 || i == 10 || i == 11 || i == 20 || i == 21 ?
		    "needle\r\n" : "x\r\n");
	find("needle");
	expectmatch(21, 0, 21, 5);
	expectcount(6, 6);
	assert(searchtop() == 20);
	tsearchpage(-1);
	expectmatch(11, 0, 11, 5); /* Row 20 is in view, so it is skipped. */
	expectcount(6, 4);
	assert(searchtop() == 10);
	tsearchpage(-1);
	expectmatch(1, 0, 1, 5);
	expectcount(6, 2);
	assert(searchtop() == 0);
	tsearchpage(-1); /* Nothing is above: the first match. */
	expectmatch(0, 0, 0, 5);
	expectcount(6, 1);
	assert(searchtop() == 0);
	tsearchpage(-1);
	expectmatch(0, 0, 0, 5);
	tsearchpage(1);
	expectmatch(10, 0, 10, 5);
	expectcount(6, 3);
	assert(searchtop() == 9);
	tsearchpage(1); /* Row 11 is in view. */
	expectmatch(20, 0, 20, 5);
	expectcount(6, 5);
	assert(searchtop() == 19);
	tsearchpage(1); /* Nothing is below: the last match. */
	expectmatch(21, 0, 21, 5);
	expectcount(6, 6);
	tsearchpage(1);
	expectmatch(21, 0, 21, 5);
	tsearchend(1);

	/* Start at the view being read, restore it on cancel, follow output. */
	reset(20, 3);
	for (i = 0; i < 8; i++)
		put("needle\r\n");
	kscrollup(&up);
	origin = searchtop();
	find("needle");
	expectmatch(origin + 2, 0, origin + 2, 5);
	put("later\r\n");
	searchvalidate();
	assert(tsearchfound());
	tsearchend(1);
	assert(searchtop() == origin && sb.view_offset == 4);
	find("needle");
	origin = searchtop();
	tsearchend(0);
	assert(searchtop() == origin);

	/* Opening search at the live edge follows output before the first
	 * query, and after an overwritten match leaves navigation unanchored. */
	reset(20, 3);
	put("needle\r\n");
	tsearchstart();
	for (i = 0; i < 10; i++)
		put("later\r\n");
	put("needle");
	tsearchset("needle");
	expectmatch(11, 0, 11, 5);
	expectcount(2, 2);
	assert(!sb.view_offset);
	put("\rxxxxxx\r\nneedle");
	searchvalidate();
	assert(!tsearchfound());
	tsearchnext(-1);
	expectmatch(12, 0, 12, 5);
	put("\rxxxxxx\r\nneedle");
	searchvalidate();
	assert(!tsearchfound());
	tsearchnext(1); /* From the live edge, wrap to the oldest match. */
	expectmatch(0, 0, 0, 5);
	expectcount(2, 1);

	/* When a scrolled-back origin and match are evicted, restart from
	 * the retained view instead of wrapping from an obsolete row. */
	reset(20, 3);
	for (i = 0; i < 8; i++)
		put("needle\r\n");
	kscrollup(&up);
	find("needle");
	for (i = 0; i < 80; i++)
		put("needle\r\n");
	searchvalidate();
	assert(!tsearchfound() && search.origin < sb.base);
	origin = searchtop();
	tsearchnext(-1);
	expectmatch(origin + 2, 0, origin + 2, 5);

	/* Overlapping patterns exercise KMP fallback in both directions. */
	reset(20, 3);
	put("ababa aaaa");
	find("aba");
	expectmatch(0, 2, 0, 4);
	expectcount(2, 2);
	tsearchnext(-1);
	expectmatch(0, 0, 0, 2);
	expectcount(2, 1);
	find("aaa");
	expectmatch(0, 7, 0, 9);
	expectcount(2, 2);
	tsearchnext(-1);
	expectmatch(0, 6, 0, 8);
	expectcount(2, 1);
	find("ABa");
	assert(!tsearchfound()); /* Literal, case-sensitive text. */

	/* A match can start in compressed history and finish on screen. */
	reset(4, 3);
	put("abcdefghijklm");
	assert(sb.len == 1);
	find("defgh");
	expectmatch(0, 3, 1, 3);
	find("abcdefghijkl");
	expectmatch(0, 0, 2, 3);
	reset(4, 3);
	put("abcd\r\nefgh");
	find("def");
	assert(!tsearchfound());

	/* Wide spacing cells and the unused margin before a wide wrap are
	 * omitted from text, including in a one-column terminal. */
	for (cols = 1; cols <= 4; cols++) {
		reset(4, 4);
		put("abc\347\225\214z");
		tresize(cols, 6);
		find("c\347\225\214z");
		assert(tsearchfound());
		searchvalidate();
		assert(tsearchfound());
	}
	reset(20, 3);
	put("a\347\225\214z caf\303\251 e\314\201");
	find("\347\225\214z");
	expectmatch(0, 1, 0, 3);
	find("cafe\314\201");
	expectmatch(0, 5, 0, 8);
	find("\303\251");
	expectmatch(0, 10, 0, 10);
	find("e\314\201");
	expectmatch(0, 10, 0, 10);

	/* Spacing characters stay in separate cells even when Unicode has a
	 * canonical composition for the pair (Bengali vowel signs E + AA). */
	reset(20, 3);
	put("\u09c7\u09be");
	assert(term.line[0][0].u == 0x09c7 && term.line[0][1].u == 0x09be);
	find("\u09c7\u09be");
	expectmatch(0, 0, 0, 1);
	searchvalidate();
	assert(tsearchfound());
	find("\u09cb");
	assert(!tsearchfound());

	/* Zero-width marks still compose with a wide base, while marks with
	 * no composition are dropped, just as when writing terminal cells. */
	reset(20, 3);
	put("\u1100\u1161\u11a8 q\u0301x");
	find("\u1100\u1161\u11a8");
	expectmatch(0, 0, 0, 1);
	find("\uac01");
	expectmatch(0, 0, 0, 1);
	find("q\u0301x");
	expectmatch(0, 3, 0, 4);

	/* Unknown-width runes occupy one cell in terminal output, so they
	 * must remain in literal queries rather than making a false match. */
	reset(20, 3);
	assert(wcwidth(0x0378) == -1);
	put("a\u0378b ab");
	find("a\u0378b");
	expectmatch(0, 0, 0, 2);
	expectcount(1, 1);
	searchvalidate();
	assert(tsearchfound());
	reset(20, 3);
	put("ab");
	find("a\u0378b");
	assert(!tsearchfound());

	/* Searching keeps an existing selection with its text. */
	reset(20, 3);
	put("keep\r\nneedle\r\na\r\nb\r\nc");
	kscrollup(&(Arg){ .i = 100 });
	selstart(0, 0, SNAP_LINE);
	selextend(0, 0, SEL_REGULAR, 1);
	find("needle");
	copy = getsel();
	assert(copy && !strcmp(copy, "keep\n"));
	free(copy);
	tsearchend(1);
	copy = getsel();
	assert(copy && !strcmp(copy, "keep\n"));
	free(copy);

	/* Alternate-screen contents are searched after main and history. */
	reset(10, 3);
	put("hist\r\nmain\r\nlast\r\nend");
	put("\033[?1049h\033[Halt");
	find("alt");
	expectmatch(4, 0, 4, 2);
	find("main");
	expectmatch(1, 0, 1, 3);
	find("hist");
	expectmatch(0, 0, 0, 3);
	find("endalt");
	assert(!tsearchfound());
	put("\033[?1049l");
	searchvalidate();
	find("main");
	assert(tsearchfound());

	/* Output retains a match as it scrolls; eviction or overwriting drops
	 * its highlight without scanning every history row on each redraw. */
	reset(20, 3);
	put("needle");
	find("needle");
	matchrow = search.start.row;
	put("\r\na\r\nb\r\nc");
	searchvalidate();
	assert(tsearchfound() && search.start.row == matchrow);
	for (i = 0; i < 70; i++)
		put("\r\nx");
	searchvalidate();
	assert(!tsearchfound());
	tsearchend(1);
	assert(!sb.view_offset); /* Origin was the live screen. */
	reset(20, 3);
	put("needle");
	find("needle");
	put("\033[Hxxxxxx");
	searchvalidate();
	assert(!tsearchfound());
	expectcount(-1, 0); /* Output invalidated the last scan. */
	tsearchnext(-1);
	assert(!tsearchfound());
	expectcount(0, 0); /* A fresh scan really found no matches. */

	reset(20, 3);
	put("needle\r\nneedle");
	find("needle");
	put("\rxxxxxx");
	searchvalidate();
	searchmarks();
	expectcount(-1, 0);
	assert(tsearchmatch(0, 0) == SEARCH_OTHER);
	tsearchnext(-1);
	expectcount(1, 1);
	find("absent");
	expectcount(0, 0);
	put("\033[3;1Habsent");
	searchmarks();
	expectcount(-1, 0); /* New matches also make "No match" obsolete. */

	/* Reflow refreshes the current query and recomputes the cell range. */
	reset(8, 3);
	put("abcdefneedle tail");
	find("needle");
	assert(tsearchfound());
	tresize(5, 4);
	searchvalidate();
	assert(tsearchfound());
	searchvalidate();
	assert(tsearchfound());

	/* Search and navigation do not expand compressed history storage. */
	reset(80, 3);
	for (i = 0; i < 80; i++)
		put("short needle\r\n");
	find("needle");
	count = tsearchcount(&i); /* the newest of what the ring retains */
	assert(count > 3 && i == count);
	for (i = 0; i < 80; i++)
		tsearchnext(-1);
	expectcount(count, 80 % count ? count - 80 % count : count);
	tsearchedge(0); /* The oldest retained row, scrolled to the top. */
	expectcount(count, 1);
	assert(search.start.row == sb.base && sb.view_offset == sb_maxoffset());
	tsearchedge(1);
	expectcount(count, count);
	for (i = 0; i < sb.len; i++)
		assert(sb.buf[sb_phys_index(i)].cols == 12);

	puts("PASS: incremental history search, navigation, wraps, Unicode, alternate screens, selection, eviction and reflow");
	return 0;
}
