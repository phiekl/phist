static unsigned long linkvisits;
#define LINKVISIT() (linkvisits++)
#include "terminal.h"

static void
openlink(const char *params, const char *uri)
{
	put("\033]8;");
	put(params);
	put(";");
	put(uri);
	put("\033\\");
}

static void
expecturl(int x, int y, const char *want)
{
	char *url = tgeturl(x, y);
	assert(url && !strcmp(url, want) && thyperlink(x, y));
	free(url);
}

/* Every table reference must belong to a real cell, a compressed history
 * run or a cursor. This catches leaks and premature reuse through reflow. */
static void
checkrefs(void)
{
	static uint64_t refs[HLINK_MAX + 1];
	static uchar indexed[HLINK_MAX + 1];
	HistoryLine *row;
	HistoryRun *runs;
	int x, y, i, r, start, bucket, id, used = 0, entries = 0;
	size_t bytes = 0;

	memset(refs, 0, sizeof(refs));
	memset(indexed, 0, sizeof(indexed));
	for (bucket = 0; bucket < HLINK_BUCKETS; bucket++) {
		for (id = hlinks.buckets[bucket]; id; id = hlinks.slots[id].next) {
			assert(BETWEEN(id, 1, hlinks.allocated) && !indexed[id]);
			assert(hlinks.slots[id].uri && hlinks.slots[id].refs);
			assert(hlinks.slots[id].hash == linkhash(hlinks.slots[id].uri,
			                                       hlinks.slots[id].name));
			assert((hlinks.slots[id].hash & (HLINK_BUCKETS - 1)) == bucket);
			indexed[id] = 1;
			entries++;
		}
	}
	refs[term.c.attr.link]++;
	refs[term.mainc.attr.link]++;
	refs[term.saved[0].attr.link]++;
	refs[term.saved[1].attr.link]++;
	for (y = 0; y < term.row; y++)
		for (x = 0; x < term.col; x++) {
			refs[term.line[y][x].link]++;
			refs[term.alt[y][x].link]++;
		}
	for (i = 0; i < sb.len; i++) {
		row = &sb.buf[sb_phys_index(i)];
		if (!row->runs)
			for (x = 0; x < row->cols; x++)
				refs[((Line)row->cells)[x].link]++;
		else {
			runs = row->cells;
			for (r = start = 0; r < row->runs; r++) {
				refs[runs[r].link] += runs[r].end - start;
				start = runs[r].end;
			}
		}
	}
	for (i = 1; i <= HLINK_MAX; i++) {
		assert(hlinks.slots[i].refs == refs[i]);
		assert(!!refs[i] == !!hlinks.slots[i].uri);
		assert(!!refs[i] == indexed[i]);
		if (!refs[i])
			continue;
		used++;
		bytes += strlen(hlinks.slots[i].uri) + 1;
		if (hlinks.slots[i].name)
			bytes += strlen(hlinks.slots[i].name) + 1;
	}
	assert(used == hlinks.used && entries == used &&
	       bytes == hlinks.bytes && bytes <= HLINK_BYTES);
}

static void
testcollisions(void)
{
	static int seen[HLINK_BUCKETS][2];
	char uri[64], targets[3][64];
	int i, bucket, values[3];
	unsigned int ids[3];

	/* Find three distinct keys sharing a bucket, independently of which
	 * hash function is used, then remove the middle, head and tail. */
	for (i = 1; ; i++) {
		snprintf(uri, sizeof(uri), "https://example.com/collision/%d", i);
		bucket = linkhash(uri, NULL) & (HLINK_BUCKETS - 1);
		if (!seen[bucket][0])
			seen[bucket][0] = i;
		else if (!seen[bucket][1])
			seen[bucket][1] = i;
		else {
			values[0] = seen[bucket][0];
			values[1] = seen[bucket][1];
			values[2] = i;
			break;
		}
	}
	reset(20, 3);
	for (i = 0; i < 3; i++) {
		snprintf(targets[i], sizeof(targets[i]), "https://example.com/collision/%d", values[i]);
		openlink("", targets[i]);
		ids[i] = term.c.attr.link;
		assert(ids[i]);
		put("x");
		openlink("", "");
	}
	assert(hlinks.used == 3);
	for (i = 0; i < 3; i++) {
		openlink("", targets[i]);
		assert(term.c.attr.link == ids[i]);
		openlink("", "");
	}
	checkrefs();
	put("\033[1;2H ");
	assert(hlinks.used == 2);
	checkrefs();
	openlink("", targets[1]);
	assert(term.c.attr.link == ids[1] && hlinks.used == 3);
	openlink("", "");
	checkrefs();
	put("\033[1;3H ");
	checkrefs();
	put("\033[1;1H ");
	assert(!hlinks.used);
	checkrefs();
}

/* Targets sharing a bucket under one seed must scatter under another, including
 * seeds that agree in their low bits, which an unmixed hash would ignore. */
static void
testseed(void)
{
	static int counts[HLINK_BUCKETS], values[64];
	static const uint64_t others[] = { 2, UINT64_C(1) << 32 | 1, UINT64_C(1) << 63 | 1 };
	char uri[64];
	uint64_t saved, first;
	int i, n, bucket, most, want;

	reset(20, 3);
	assert(!hlinks.used);
	linkhash("https://example.com/", NULL);
	assert(hlinks.seeded);
	saved = hlinks.seed;
	linkseed();
	first = hlinks.seed;
	linkseed();
	assert(hlinks.seed != first);
	hlinks.seed = 1;
	want = linkhash("https://example.com/seed/0", NULL) & (HLINK_BUCKETS - 1);
	for (i = n = 0; n < LEN(values); i++) {
		snprintf(uri, sizeof(uri), "https://example.com/seed/%d", i);
		if ((int)(linkhash(uri, NULL) & (HLINK_BUCKETS - 1)) == want)
			values[n++] = i;
	}
	for (i = 0; i < LEN(others); i++) {
		hlinks.seed = others[i];
		memset(counts, 0, sizeof(counts));
		most = 0;
		for (n = 0; n < LEN(values); n++) {
			snprintf(uri, sizeof(uri), "https://example.com/seed/%d", values[n]);
			bucket = linkhash(uri, NULL) & (HLINK_BUCKETS - 1);
			most = MAX(most, ++counts[bucket]);
		}
		assert(most <= 4);
	}
	hlinks.seed = saved;
}

static void
testlookupbounds(void)
{
	char uri[128];
	int i;

	reset(HLINK_MAX + 2, 1);
	for (i = 0; i < HLINK_MAX; i++) {
		snprintf(uri, sizeof(uri), "https://example.com/retained/%d", i);
		openlink("", uri);
		put("x");
	}
	openlink("", "");
	assert(hlinks.used == HLINK_MAX);
	/* A saturated table must still find retained targets, without scanning
	 * every slot for either hits or rejected new targets. Count work rather
	 * than elapsed time so this remains a stable regression check. */
	linkvisits = 0;
	for (i = 0; i < 10000; i++) {
		snprintf(uri, sizeof(uri), "https://example.com/retained/%d", i % 50);
		openlink("", uri);
		assert(term.c.attr.link == term.line[0][i % 50].link);
	}
	assert(linkvisits < 16 * 10000UL);
	openlink("", "");
	linkvisits = 0;
	for (i = 0; i < 10000; i++) {
		snprintf(uri, sizeof(uri), "https://example.com/rejected/%d", i);
		openlink("", uri);
		assert(!term.c.attr.link);
	}
	assert(linkvisits < 16 * 10000UL && hlinks.used == HLINK_MAX);
	checkrefs();
}

static void
testanonymous(const char *target)
{
	int i, x, y, right, bottom;

	/* Repeated anonymous targets share storage, but adjacent labels keep
	 * their own bounds, including across wide glyphs, wraps and reflow. */
	reset(4, 4);
	openlink("", target);
	put("ab");
	openlink("", "");
	openlink("", target);
	put("c\u754cx");
	openlink("", "");
	assert(hlinks.used == 1 && term.line[0][0].link == term.line[0][2].link);
	x = 1; y = 0;
	thyperlinkbounds(&x, &y, &right, &bottom);
	assert(x == 0 && y == 0 && right == 1 && bottom == 0);
	x = 1; y = 1;
	thyperlinkbounds(&x, &y, &right, &bottom);
	assert(x == 0 && y == 0 && right == 2 && bottom == 1);
	tresize(3, 4);
	checkrefs();
	x = 0; y = 0;
	thyperlinkbounds(&x, &y, &right, &bottom);
	assert(x == 0 && y == 0 && right == 1 && bottom == 0);
	x = 1; y = 1;
	thyperlinkbounds(&x, &y, &right, &bottom);
	assert(x == 0 && y == 0 && right == 2 && bottom == 1);

	/* A new label may begin exactly at a soft wrap. */
	reset(3, 4);
	openlink("", target);
	put("abc");
	openlink("", target);
	put("def");
	openlink("", "");
	x = 1; y = 0;
	thyperlinkbounds(&x, &y, &right, &bottom);
	assert(x == 0 && y == 0 && right == 2 && bottom == 0);
	x = 1; y = 1;
	thyperlinkbounds(&x, &y, &right, &bottom);
	assert(x == 0 && y == 1 && right == 2 && bottom == 1);
	checkrefs();

	/* Deleting the first glyph leaves the adjacent labels distinct. */
	reset(20, 3);
	openlink("", target);
	put("ab");
	openlink("", target);
	put("cdef");
	openlink("", "");
	put("\033[1;3H\033[P"); /* Remove the second label's first glyph. */
	x = 3; y = 0;
	thyperlinkbounds(&x, &y, &right, &bottom);
	assert(x == 2 && right == 4 && y == 0 && bottom == 0);
	put("\033[1;2H\033[2P"); /* Cut across both labels, including the new start. */
	x = 2; y = 0;
	thyperlinkbounds(&x, &y, &right, &bottom);
	assert(x == 1 && right == 2 && y == 0 && bottom == 0);
	checkrefs();

	/* More retained occurrences than slots must leave room for new targets. */
	reset(20, 3);
	sb_setlimit(HLINK_MAX + 3);
	for (i = 0; i < HLINK_MAX + 10; i++) {
		openlink("", target);
		put("x");
		openlink("", "");
		put("\r\n");
	}
	assert(hlinks.used == 1 && hlinks.bytes == strlen(target) + 1);
	openlink("", "https://other.example/");
	assert(term.c.attr.link && hlinks.used == 2);
	checkrefs();
	sb_setlimit(scrollback_lines);
}

int
main(void)
{
	static const char target[] = "https://example.com/a;b?q=x#part";
	char large[URLTARGET_MAX + 2], params[32];
	char *text;
	unsigned int id;
	int i, j, x, y, right, bottom;

	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	reset(20, 4);
	openlink("id=one", target);
	put("A\u754cB");
	openlink("", "");
	for (i = 0; i < 4; i++)
		expecturl(i, 0, target);
	assert(!turl(4, 0));
	checkrefs();
	x = 3;
	y = 0;
	thyperlinkbounds(&x, &y, &right, &bottom);
	assert(x == 0 && y == 0 && right == 3 && bottom == 0);
	id = term.line[0][0].link;
	openlink("ignored=value:id=one", target);
	put(" C");
	assert(term.line[0][4].link == id && term.line[0][5].link == id);
	openlink("id=other", target);
	put("D");
	assert(term.line[0][6].link != id);
	put("\033[0mE\0337"); /* SGR leaves links alone; save owns its attribute. */
	openlink("", "");
	put("x\0338F");
	expecturl(8, 0, target);
	checkrefs();

	/* Normal selection copies the label; only clicking resolves a target. */
	selstart(0, 0, 0);
	selextend(3, 0, SEL_REGULAR, 0);
	selextend(3, 0, SEL_REGULAR, 1);
	text = getsel();
	assert(text && !strcmp(text, "A\u754cB"));
	free(text);

	/* Cell moves, wide overwrites and clears release their former owners. */
	put("\033[H\033[2P\033[2@\033[4hZZ\033[4l");
	checkrefs();
	put("\033[H\u754cx\033[1;2Hz");
	checkrefs();
	put("\033[2J");
	checkrefs();

	/* Locate the start through soft wraps and an unused wide margin. */
	reset(4, 4);
	put("xx");
	openlink("id=wrap", target);
	put("a\u754cZ");
	x = 2;
	y = 1;
	thyperlinkbounds(&x, &y, &right, &bottom);
	assert(x == 0 && y == 0 && right == 2 && bottom == 1);
	put("\r\nx");
	x = 0;
	y = 2;
	thyperlinkbounds(&x, &y, &right, &bottom);
	assert(x == 0 && y == 2 && right == 0 && bottom == 2); /* hard break */
	checkrefs();

	/* Linked spaces are content. Targets survive compression and reflow. */
	reset(4, 3);
	openlink("", target);
	put("ab  cdefghijkl");
	openlink("", "");
	checkrefs();
	assert(sb.len > 0);
	tresize(2, 6);
	checkrefs();
	kscrollup(&(Arg){ .i = 1000 });
	expecturl(0, 0, target);
	expecturl(0, 1, target); /* the linked trailing spaces */
	tresize(9, 4);
	checkrefs();
	put("\033[?1049h");
	openlink("", "file://remote/tmp/report.txt");
	put("report");
	checkrefs();
	put("\033[4;1Hlast");
	tresize(3, 2);
	checkrefs();
	put("\033[?1049l\033[2J");
	checkrefs();
	put("\033c");
	checkrefs(); /* RIS keeps retained history links, clears cursor state. */

	/* Malformed/oversized opens close the old link rather than applying
	 * its target to the label of an invalid one. URI semicolons are kept. */
	reset(80, 3);
	openlink("", target);
	openlink("", "https://example.com/a b");
	put("plain");
	assert(!term.line[0][0].link && !term.c.attr.link);
	openlink("", "https://example.com/%");
	assert(!term.c.attr.link);
	openlink("", target);
	put("\033]8\a");
	assert(!term.c.attr.link);
	memset(large, 'x', sizeof(large));
	memcpy(large, "https://", 8);
	large[sizeof(large) - 1] = '\0';
	openlink("", large);
	assert(!term.c.attr.link);
	large[URLTARGET_MAX] = '\0';
	openlink("", large);
	put("z");
	expecturl(5, 0, large);
	checkrefs();
	for (i = 1; i <= 4; i++) {
		reset(4, 4);
		openlink("", target);
		put("ab\u754c c\u754c");
		openlink("", "");
		tresize(i, 6);
		checkrefs();
		tresize(8, 4);
		checkrefs();
	}
	reset(HLINK_BYTES / (URLTARGET_MAX + 1) + 2, 1);
	for (i = 0; i <= HLINK_BYTES / (URLTARGET_MAX + 1); i++) {
		snprintf(params, sizeof(params), "id=%d", i);
		openlink(params, large);
		put("x");
	}
	assert(!term.c.attr.link && hlinks.bytes <= HLINK_BYTES);
	checkrefs();

	/* Saturation preserves retained targets; releasing cells makes their
	 * slots available immediately, without scanning all scrollback. */
	reset(HLINK_MAX + 2, 1);
	for (i = 0; i < HLINK_MAX; i++) {
		snprintf(params, sizeof(params), "id=%d", i);
		openlink(params, target);
		put("x");
	}
	assert(hlinks.used == HLINK_MAX);
	openlink("", "https://other.example/");
	assert(!term.c.attr.link);
	expecturl(0, 0, target);
	checkrefs();
	put("\033[2K");
	assert(!hlinks.used);
	openlink("", "https://new.example/");
	put("y");
	checkrefs();

	/* Repeated edits exercise ownership across raw and packed rows. */
	reset(12, 4);
	for (i = 0; i < 150; i++) {
		openlink(i % 2 ? "id=shared" : "", target);
		put("abc \u754c defghijk\r\n");
		openlink("", "");
		if (i % 3 == 0)
			put("\033[H\033[2@\033[1P");
		if (i % 5 == 0)
			tresize(3 + i % 17, 3 + i % 5);
		if (i % 7 == 0)
			put("\033[?1049h\033[?1049l");
		checkrefs();
	}
	reset(20, 3);
	checkrefs();
	assert(!hlinks.used && !hlinks.bytes);
	for (j = 0; j < 2; j++) {
		openlink("", target);
		put("x");
		reset(20, 3);
		checkrefs();
		assert(!hlinks.used);
	}
	testanonymous(target);
	testcollisions();
	testseed();
	testlookupbounds();
	reset(20, 3);
	checkrefs();
	assert(!hlinks.used);
	puts("PASS: OSC 8 labels, shared anonymous targets, IDs, wide cells, selection, history, reflow, edits, bounds and reference reclamation");
	return 0;
}
