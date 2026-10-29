static unsigned long rowvisits;
#define ROWVISIT() (rowvisits++)
static char *copied; /* what the last copy published */
#define XSETSEL(s) (free(copied), copied = (s))
#include "terminal.h"

/* Press keys by name: every byte is a key, ~x a control key. */
static void
keys(const char *s)
{
	for (; *s; s++) {
		if (*s == '~')
			tvimkey(*++s, 1);
		else
			tvimkey((uchar)*s, 0);
	}
}

/* Row in the view and column of the cursor. */
#define expectat(y, x) expectcursor(__LINE__, y, x)
static void
expectcursor(int source, int y, int x)
{
	if (vimy(vim.pos.row) != y || vim.pos.x != x)
		fprintf(stderr, "line %d: cursor: %d,%d, expected %d,%d\n", source,
		        vimy(vim.pos.row), vim.pos.x, y, x);
	assert(vimy(vim.pos.row) == y && vim.pos.x == x);
	assert(tvimcursor(x, y));
}

#define expectsel(want) expectselection(__LINE__, want)
static void
expectselection(int source, const char *want)
{
	char *text = getsel();

	if (!text || strcmp(text, want))
		fprintf(stderr, "line %d: selection: \"%s\", expected \"%s\"\n", source,
		        text ? text : "(none)", want);
	assert(text && !strcmp(text, want));
	free(text);
}

/* Start at the top left of a screen holding this output. */
static void
screen(int cols, int rows, const char *text)
{
	reset(cols, rows);
	put(text);
	tvimstart();
	keys("gg0");
}

static void
testmotions(void)
{
	screen(40, 5, "  foo.bar baz\r\n\r\nthird line here\r\nlast");
	expectat(0, 0);
	keys("^");
	expectat(0, 2);
	keys("w");
	expectat(0, 5); /* the punctuation is a word of its own */
	keys("w");
	expectat(0, 6);
	keys("w");
	expectat(0, 10);
	keys("w");
	expectat(2, 0); /* past the end and the blank row */
	keys("b");
	expectat(0, 10);
	keys("gg0W");
	expectat(0, 2);
	keys("W");
	expectat(0, 10);
	keys("B");
	expectat(0, 2);
	keys("e");
	expectat(0, 4);
	keys("E");
	expectat(0, 8); /* the rest of foo.bar */
	keys("$");
	expectat(0, 12);
	keys("0");
	expectat(0, 0);
	keys("3l");
	expectat(0, 3);
	keys("2j");
	expectat(2, 3);
	keys("h");
	expectat(2, 2);
	keys("k");
	expectat(1, 2);
	keys("99h");
	expectat(1, 0);
	keys("99l");
	expectat(1, 39);
	keys("G");
	expectat(3, 0); /* the last output, not the blank row below it */
	keys("99k");
	expectat(0, 0);
	keys("3G");
	expectat(2, 0);
	keys("L");
	expectat(4, 0);
	keys("H");
	expectat(0, 2);
	keys("M");
	expectat(2, 0);
	/* w has no word to go to past the last one, and b none before the first. */
	keys("G$w");
	expectat(3, 3);
	keys("ggb");
	expectat(0, 2);
	/* Digits count unless 0 starts them; a stray key drops the count. */
	keys("gg010l");
	expectat(0, 10);
	keys("0" "5~x" "l");
	expectat(0, 1);
	keys("3");
	tvimkey(VIM_ESCAPE, 0);
	assert(tvimactive());
	keys("l");
	expectat(0, 2);
	tvimend();
	assert(!tvimactive());

	/* A hard break separates words even with no blank between them. */
	screen(3, 3, "abc\r\ndef");
	keys("w");
	expectat(1, 0);
	keys("e");
	expectat(1, 2);
	keys("ggl$e");
	expectat(1, 2);
	tvimend();
}

static void
testfind(void)
{
	screen(40, 3, "a,b,c,d");
	keys("f,");
	expectat(0, 1);
	keys(";");
	expectat(0, 3);
	keys(",");
	expectat(0, 1);
	keys("2;");
	expectat(0, 5);
	keys("F,");
	expectat(0, 3);
	keys("fz"); /* nothing there: stays */
	expectat(0, 3);
	keys("0t,");
	expectat(0, 0);
	keys(";");
	expectat(0, 2);
	keys(";");
	expectat(0, 4);
	keys(",");
	expectat(0, 2);
	keys("$T,");
	expectat(0, 6);
	keys("02f,");
	expectat(0, 3);
	keys("02t,");
	expectat(0, 2);
	keys("f"); /* Escape drops a pending key without leaving the mode. */
	tvimkey(VIM_ESCAPE, 0);
	assert(tvimactive());
	keys("h");
	expectat(0, 1);
	tvimend();
}

static void
testvisual(void)
{
	screen(40, 4, "alpha beta\r\ngamma delta\r\nend");
	keys("wviw");
	/* i leaves; nothing is copied or selected any more. */
	assert(!tvimactive());
	assert(sel.ob.x == -1);

	screen(40, 4, "alpha beta\r\ngamma delta\r\nend");
	keys("wve");
	assert(tvimactive() == VIM_VISUAL);
	expectsel("beta");
	keys("j");
	expectsel("beta\ngamma delt");
	keys("o");
	expectat(0, 6);
	keys("h");
	expectsel(" beta\ngamma delt");
	/* Changing mode keeps both ends. */
	keys("V");
	assert(tvimactive() == VIM_LINE);
	expectsel("alpha beta\ngamma delta\n");
	keys("~v");
	assert(tvimactive() == VIM_BLOCK);
	expectsel(" beta\n delt");
	keys("v");
	assert(tvimactive() == VIM_VISUAL);
	/* The same key again leaves the selection. */
	keys("v");
	assert(tvimactive() == VIM_NORMAL && sel.ob.x == -1);
	keys("V");
	expectsel("alpha beta\n");
	tvimkey(VIM_ESCAPE, 0);
	assert(tvimactive() == VIM_NORMAL && sel.ob.x == -1);
	tvimkey(VIM_ESCAPE, 0);
	assert(!tvimactive());

	/* A block spans rows at the same columns. */
	screen(40, 4, "abcdef\r\nghijkl\r\nmnopqr");
	keys("ll~vjl");
	expectsel("cd\nij");
	/* A copy ends the mode, the selection staying like a mouse selection. */
	keys("y");
	assert(!tvimactive() && sel.mode == SEL_IDLE);
	expectsel("cd\nij");

	/* yy, Y and counts copy whole lines, and leave the mode. */
	screen(40, 4, "one\r\ntwo\r\nthree");
	keys("yy");
	assert(!tvimactive());
	expectsel("one\n");
	screen(40, 4, "one\r\ntwo\r\nthree");
	keys("2Y");
	assert(!tvimactive());
	expectsel("one\ntwo\n");
	screen(40, 4, "one\r\ntwo\r\nthree");
	keys("j2yy");
	expectsel("two\nthree\n");
	screen(40, 4, "one\r\ntwo\r\nthree");
	keys("lvY");
	expectsel("one\n");
	/* y alone waits for a second y; another key drops it. */
	screen(40, 4, "one\r\ntwo\r\nthree");
	keys("yl");
	assert(tvimactive() == VIM_NORMAL);
	expectat(0, 0);
	/* Enter copies a selection and does nothing without one. */
	keys("\r");
	assert(tvimactive() == VIM_NORMAL);
	keys("ve\r");
	assert(!tvimactive());
	expectsel("one");
}

static void
testwrapped(void)
{
	static const char *const yanks[] = { "2yy", "2Y", "j2yy", "j2Y", "4yy", "4Y" };
	int i;

	/* Counts include whole logical lines, even from inside a wrap, and
	 * stop at the end of retained text when fewer lines remain. */
	for (i = 0; i < LEN(yanks); i++) {
		screen(5, 5, "abcdefghijklmno\r\ntwo\r\nend");
		keys(yanks[i]);
		assert(!tvimactive());
		expectsel(i < 4 ? "abcdefghijklmno\ntwo\n" :
		          "abcdefghijklmno\ntwo\nend\n");
	}
	screen(5, 5, "abcdefghijk\r\n\r\ntwo");
	keys("2yy");
	expectsel("abcdefghijk\n\n");

	/* Every way of copying whole lines ends in a newline, also at the full
	 * width and across a wrap that fills its last row. Other copies do not. */
	{
		static const char *const whole[] = { "yy", "Y", "Vy", "V\r", "vY" };

		for (i = 0; i < LEN(whole); i++) {
			screen(5, 5, "abcde\r\nabcd\r\nabcdefghij\r\nx");
			keys(whole[i]);
			assert(copied && !strcmp(copied, "abcde\n"));
			screen(5, 5, "abcde\r\nabcd\r\nabcdefghij\r\nx");
			keys("j");
			keys(whole[i]);
			assert(!strcmp(copied, "abcd\n"));
			screen(5, 5, "abcde\r\nabcd\r\nabcdefghij\r\nx");
			keys("jj");
			keys(whole[i]);
			assert(!strcmp(copied, "abcdefghij\n"));
		}
		screen(5, 5, "abcde\r\nabcd");
		keys("vey");
		assert(!strcmp(copied, "abcde"));
	}

	/* A soft-wrapped line is one line to 0, $, ^, words and V. */
	screen(5, 4, "abcdefgh ij\r\nnext");
	keys("j");
	expectat(1, 0);
	keys("$");
	expectat(2, 0);
	keys("0");
	expectat(0, 0);
	keys("w");
	expectat(1, 4);
	keys("e");
	expectat(2, 0);
	keys("b");
	expectat(1, 4);
	keys("V");
	expectsel("abcdefgh ij\n");
	tvimkey(VIM_ESCAPE, 0);
	/* f and t stay within the logical line, across its wraps. */
	keys("0fi");
	expectat(1, 4);
	keys("fn");
	expectat(1, 4);
	tvimend();

	/* Wide characters take a single step, and the cursor sits on their
	 * first half. */
	screen(10, 3, "a\344\270\255\346\226\207b");
	keys("l");
	expectat(0, 1);
	assert(tvimcursor(1, 0) && !tvimcursor(2, 0));
	keys("l");
	expectat(0, 3);
	keys("l");
	expectat(0, 5);
	keys("h");
	expectat(0, 3);
	keys("v0");
	expectsel("a\344\270\255\346\226\207");
	tvimend();
	assert(sel.ob.x == -1);
}

static void
testhistory(void)
{
	uint64_t top;
	char line[32];
	int i;

	reset(20, 4);
	for (i = 0; i < 20; i++) {
		snprintf(line, sizeof(line), "line %d\r\n", i);
		put(line);
	}
	assert(sb.view_offset == 0);
	tvimstart();
	expectat(term.c.y, term.c.x);
	/* Moving off the view scrolls it. */
	keys("gg");
	assert(sb.view_offset == sb_maxoffset());
	expectat(0, 0);
	keys("vjjjjjj$");
	assert(sb.view_offset == sb_maxoffset() - 3);
	expectsel("line 0\nline 1\nline 2\nline 3\nline 4\nline 5\nline 6");
	tvimkey(VIM_ESCAPE, 0);
	/* Half pages and pages move both the view and the cursor. */
	keys("gg~d");
	assert(sb.view_offset == sb_maxoffset() - 2);
	expectat(0, 0);
	keys("~u");
	assert(sb.view_offset == sb_maxoffset());
	keys("~f");
	assert(sb.view_offset == sb_maxoffset() - 2);
	keys("~b");
	assert(sb.view_offset == sb_maxoffset());
	/* Lines move only the view, until it would leave the cursor out. */
	keys("~e");
	assert(sb.view_offset == sb_maxoffset() - 1);
	expectat(0, 0);
	keys("~y");
	assert(sb.view_offset == sb_maxoffset());
	expectat(1, 0);
	/* The wheel too, its cursor staying in view. */
	keys("gg");
	kscrolldown(&(Arg){ .i = 2 });
	assert(sb.view_offset == sb_maxoffset() - 2);
	expectat(0, 0);
	/* Leaving returns to the live screen it began on. */
	keys("q");
	assert(!tvimactive() && sb.view_offset == 0);

	/* Or to the same place in the history. */
	sb.view_offset = 5;
	top = searchtop();
	tvimstart();
	expectat(term.row - 1, 0);
	keys("gg");
	assert(searchtop() != top);
	tvimend();
	assert(searchtop() == top);
	/* A copy from far away returns too, the selection following its text. */
	sb.view_offset = 0;
	tvimstart();
	keys("ggVy");
	assert(!tvimactive() && sb.view_offset == 0);
	expectsel("line 0\n");
}

static void
testlargepages(void)
{
	/* The repeat cap alone does not bound count times page height. Large
	 * forward/backward jumps must reach the edge without reversing. */
	reset(1, 30000);
	put("\033[30000;1H\n\n\n\n");
	assert(sb.len == 4);
	tvimstart();
	keys("gg99999~f");
	assert(searchtop() == sb.base + sb_maxoffset());
	expectat(term.row - 1, 0);
	keys("99999");
	tvimkey(VIM_PAGEUP, 0);
	assert(searchtop() == sb.base);
	expectat(0, 0);
	tvimend();
}

static void
testoutput(void)
{
	char line[32];
	int i;

	/* Output scrolling past keeps the cursor on its text, and pins the
	 * view once the text leaves it. */
	reset(20, 4);
	put("first\r\nsecond\r\nthird\r\nfourth");
	tvimstart();
	keys("gg");
	expectat(0, 0);
	put("\r\nmore");
	vimvalidate();
	assert(sb.len == 1 && sb.view_offset == 1);
	expectat(0, 0);
	keys("ve");
	expectsel("first");
	put("\r\nand\r\nmore");
	vimvalidate();
	assert(tvimactive() == VIM_VISUAL);
	expectat(0, 4);
	expectsel("first");
	keys("y");
	expectsel("first");

	/* Rows leaving the history take the selection with them, and the
	 * cursor stays on the oldest row left. */
	reset(20, 4);
	put("anchor\r\n");
	tvimstart();
	keys("ggvl");
	for (i = 0; i < 200; i++) {
		snprintf(line, sizeof(line), "line %d\r\n", i);
		put(line);
	}
	vimvalidate();
	assert(tvimactive() == VIM_NORMAL && sel.ob.x == -1);
	assert(vim.pos.row >= sb.base);
	keys("gg");
	expectat(0, 0);
	tvimend();

	/* Overwriting selected text ends the selection, not the mode. */
	reset(20, 4);
	put("selected");
	tvimstart();
	keys("ggve");
	put("\033[Hover");
	vimvalidate();
	assert(tvimactive() == VIM_NORMAL && sel.ob.x == -1);
	keys("v");
	assert(tvimactive() == VIM_VISUAL);
	tvimend();
}

static void
testregionalscroll(void)
{
	static const char text[] = "one\r\ntwo\r\nthree\r\nfour\r\nfive";
	int mode, alt;

	/* Copy immediately after output, before the next frame validates it. */
	screen(20, 5, text);
	keys("jve");
	put("\033[H\033[L");
	keys("y");
	expectsel("two");

	/* Insert/delete lines move both ends, including a reversed selection.
	 * Yanking must copy the text that remained highlighted. */
	for (mode = VIM_VISUAL; mode <= VIM_BLOCK; mode++) {
		for (alt = 0; alt <= 1; alt++) {
			reset(20, 5);
			if (alt)
				put("\033[?1049h\033[H");
			put(text);
			tvimstart();
			keys("Hj0");
			tvimkey(mode == VIM_LINE ? 'V' : 'v',
			        mode == VIM_BLOCK);
			keys("ll");
			put("\033[H\033[L");
			vimvalidate();
			expectat(2, 2);
			expectsel(mode == VIM_LINE ? "two\n" : "two");
			keys("o");
			put("\033[H\033[M");
			vimvalidate();
			expectat(1, 0);
			expectsel(mode == VIM_LINE ? "two\n" : "two");
			keys("y");
			assert(!tvimactive());
			expectsel(mode == VIM_LINE ? "two\n" : "two");
		}
	}

	/* The normal cursor follows regional scrolling too, stopping at its
	 * margins when its text is lost. Rows outside the region stay put. */
	screen(20, 5, text);
	keys("j");
	put("\033[2;4r\033[2;1H\033[L");
	vimvalidate();
	expectat(2, 0);
	put("\033[M");
	vimvalidate();
	expectat(1, 0);
	put("\033[9M");
	vimvalidate();
	expectat(1, 0);
	keys("H");
	put("\033[2;1H\033[L");
	vimvalidate();
	expectat(0, 0);
	tvimend();

	/* A top-origin region adds history, but rows below it stay on screen.
	 * Their absolute positions change with the history's live edge. */
	screen(20, 5, text);
	keys("Lve");
	put("\033[1;3r\033[3;1H\n");
	vimvalidate();
	expectat(4, 3);
	expectsel("five");
	keys("y");
	expectsel("five");

	/* A selection crossing a region edge still ends if scrolling splits it. */
	screen(20, 5, text);
	keys("Vj");
	put("\033[2;4r\033[2;1H\033[L");
	vimvalidate();
	assert(tvimactive() == VIM_NORMAL && sel.ob.x == -1);
	tvimend();
}

static void
testscreens(void)
{
	/* Switching screens ends the mode, as rows mean other text there. */
	reset(20, 4);
	put("main");
	tvimstart();
	keys("0v");
	put("\033[?1049h");
	assert(!tvimactive() && sel.ob.x == -1);
	/* Beneath a full-screen program the mode reaches main text and history. */
	put("\033[Halt");
	assert(IS_SET(MODE_ALTSCREEN));
	tvimstart();
	expectat(0, 3);
	keys("0ve");
	expectsel("alt");
	tvimkey(VIM_ESCAPE, 0);
	keys("gg");
	assert(sb.view_offset == sb_maxoffset());
	keys("ve");
	expectsel("main");
	tvimend();
	assert(sb.view_offset == 0);
	put("\033[?1049l");
	assert(!tvimactive());

	/* A switch leaves the mode for the view it started from, live or not. */
	reset(20, 4);
	put("1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9\r\n10");
	tvimstart();
	keys("gg");
	assert(sb.view_offset == sb_maxoffset() && sb.view_offset > 2);
	put("\033[?1049h");
	assert(!tvimactive() && sb.view_offset == 0);
	put("\033[?1049l");
	assert(sb.view_offset == 0);
	{
		Arg scroll = { .i = 2 };

		kscroll(&scroll);
	}
	assert(sb.view_offset == 2);
	tvimstart();
	keys("gg");
	/* The same history rows stay in view as the main screen moves on. */
	put("\033[?1049h");
	assert(!tvimactive() && sb.view_offset == 2 + term.row);
	put("\033[?1049l");
	assert(sb.view_offset == 2);

	/* Reset ends it. */
	reset(20, 4);
	put("text");
	tvimstart();
	put("\033c");
	assert(!tvimactive());

	/* Resizing keeps the cursor at its place on screen, ending the
	 * selection, as reflow moves the text. */
	reset(20, 4);
	put("one\r\ntwo\r\nthree");
	tvimstart();
	keys("gg" "j" "lll" "v");
	tresize(10, 3);
	assert(tvimactive() == VIM_NORMAL && sel.ob.x == -1);
	expectat(1, 3);
	keys("99l");
	expectat(1, 9);
	tresize(5, 2);
	expectat(1, 4);
	tresize(1, 1);
	expectat(0, 0);
	tresize(20, 4);
	keys("q");
	assert(!tvimactive());
}

static void
testsearch(void)
{
	reset(20, 3);
	put("needle one\r\nother\r\nneedle two\r\nthree\r\nneedle end\r\nfin");
	tvimstart();
	keys("/");
	/* The window opens the bar; keys then go to it. */
	tsearchstart();
	tsearchset("needle");
	assert(tsearchactive());
	tsearchend(0);
	assert(tvimactive() == VIM_NORMAL && !tsearchactive());
	assert(vim.pos.row == 4 && vim.pos.x == 0);
	keys("N");
	assert(vim.pos.row == 2 && vim.pos.x == 0);
	keys("N");
	assert(vim.pos.row == 0);
	keys("n");
	assert(vim.pos.row == 2);
	keys("2n");
	assert(vim.pos.row == 0); /* around the end */
	expectat(0, 0);
	/* A selection extends across a search. */
	keys("v");
	keys("n");
	expectsel("needle one\nother\nn");
	tvimkey(VIM_ESCAPE, 0);
	/* ? reverses the meaning of n. */
	assert(tvimkey('?', 0));
	tsearchstart();
	tsearchset("needle");
	tsearchend(0);
	assert(vim.pos.row == 2);
	keys("n");
	assert(vim.pos.row == 0);
	/* Cancelling leaves the cursor and nothing for n to repeat. */
	keys("/");
	tsearchstart();
	tsearchset("three");
	tsearchend(1);
	assert(vim.pos.row == 0 && tvimactive());
	keys("n");
	assert(vim.pos.row == 0);
	/* The cursor is not drawn while the bar is open. */
	tsearchstart();
	assert(!tvimcursor(vim.pos.x, vimy(vim.pos.row)));
	tsearchend(1);
	assert(tvimcursor(vim.pos.x, vimy(vim.pos.row)));
	tvimend();
	assert(sb.view_offset == 0);

	/* A query that finds nothing leaves the cursor. */
	reset(20, 3);
	put("text");
	tvimstart();
	tsearchstart();
	tsearchset("absent");
	tsearchend(0);
	expectat(0, 4);
	keys("n");
	expectat(0, 4);
	tvimend();
}

/* A selection output ended stays ended, even before the next frame or key. */
static void
testendedselection(void)
{
	Arg scroll = { .i = 1 };
	int cancel;

	/* The wheel moves the view, keeping the cursor, not the selection. */
	screen(20, 3, "1\r\n2\r\n3\r\n4\r\nalpha");
	keys("G0vl");
	expectsel("al");
	put("\033[3;1HX");
	assert(sel.ob.x == -1);
	kscroll(&scroll);
	assert(tvimactive() == VIM_NORMAL && sel.ob.x == -1);
	assert(sb.view_offset == 1);
	tvimend();

	/* So does closing a search, kept or cancelled. */
	for (cancel = 0; cancel <= 1; cancel++) {
		screen(20, 3, "needle\r\n2\r\nalpha");
		keys("G0vl");
		expectsel("al");
		tsearchstart();
		tsearchset("needle");
		put("\033[3;1HX");
		assert(sel.ob.x == -1);
		tsearchend(cancel);
		assert(tvimactive() == VIM_NORMAL && sel.ob.x == -1);
		tvimend();
	}
}

/* A search closed before the mode started: n repeats it only if it was kept. */
static void
testearliersearch(void)
{
	reset(20, 3);
	put("needle\r\nother\r\nmore");
	tsearchstart();
	tsearchset("needle");
	tsearchend(1);
	tvimstart();
	expectat(2, 4);
	keys("n");
	expectat(2, 4);
	tvimend();
	tsearchstart();
	tsearchset("needle");
	tsearchend(0);
	tvimstart();
	expectat(2, 4);
	keys("n");
	expectat(0, 0);
	tvimend();
}

/* Search repeats honor counts above 100 without repeating the scan. */
static void
testsearchcounts(void)
{
	static const int counts[] = { 1, 2, 99, 100, 101, 299, 300, 301, 9999, 99999 };
	char command[32];
	int i, direction, n;

	reset(20, 3);
	sb_setlimit(512);
	for (i = 0; i < 300; i++)
		put("needle\r\n");
	tvimstart();
	tsearchstart();
	tsearchset("needle");
	tsearchend(0);
	for (direction = -1; direction <= 1; direction += 2) {
		for (i = 0; i < LEN(counts); i++) {
			n = counts[i];
			vim.pos = (SearchPos){40, 0};
			vimupdate();
			snprintf(command, sizeof(command), "%d%c", n, direction > 0 ? 'n' : 'N');
			rowvisits = 0;
			keys(command);
			assert(vim.pos.row == (40 + direction * (n % 300) + 300) % 300);
			assert(vim.pos.x == 0);
			/* Count work, not time: even 99999n scans history just once. */
			assert(rowvisits < 3 * (sb_maxoffset() + term.row) + 64);
		}
	}
	tvimend();
	sb_setlimit(scrollback_lines);
}

/* Whatever happens, the cursor stays on a row that exists, and leaving
 * always returns to a valid view. */
static void
teststress(void)
{
	static const char keyset[] = "hjklwbeWBE0^$GHMLggfxtxFyT;,nNvVoOyY123 q/?\r";
	static const char ctrls[] = "bfudyev";
	static const char *output[] = {
		"word ", "\r\n", "\033[2J", "\033[H", "\033[K", "\033[3S", "\033[2T",
		"\033[?1049h", "\033[?1049l", "\033c", "\344\270\255\346\226\207",
		"xxxxxxxxxxxxxxxxxxxxxxxxx", "\033[2;3r", "\033[r", "\033]8;;x:y\033\\",
		"\033]8;;\033\\", "needle",
	};
	static const int sizes[][2] = { {20, 4}, {1, 1}, {2, 3}, {7, 2}, {33, 9} };
	int seed, i, k, op;
	char *text;

	for (seed = 1; seed <= 4; seed++) {
		srand(seed);
		reset(20, 4);
		for (i = 0; i < 20000; i++) {
			op = rand() % 16;
			if (!tvimactive() && op < 10) {
				tvimstart();
			} else if (op < 10) {
				k = rand() % (sizeof(keyset) - 1);
				if (rand() % 12 == 0) {
					tvimkey(ctrls[rand() % (sizeof(ctrls) - 1)], 1);
				} else if (keyset[k] == '/' || keyset[k] == '?') {
					if (tvimkey(keyset[k], 0)) {
						tsearchstart();
						tsearchset(rand() % 2 ? "needle" : "absent");
						tsearchend(rand() % 2);
					}
				} else {
					tvimkey(keyset[k], 0);
				}
			} else if (op < 14) {
				put(output[rand() % LEN(output)]);
			} else if (op == 14 && rand() % 16 == 0) {
				k = rand() % LEN(sizes);
				tresize(sizes[k][0], sizes[k][1]);
			} else if (op == 15 && rand() % 4 == 0) {
				kscrollup(&(Arg){ .i = rand() % 7 - 3 });
			}
			vimvalidate();
			if (tvimactive()) {
				assert(vim.pos.row >= sb.base && vim.pos.row <= vimlast());
				assert(vim.pos.row >= searchtop());
				assert(vim.pos.row < searchtop() + term.row);
				assert(BETWEEN(vim.pos.x, 0, term.col - 1));
				assert(!(viewline(vimy(vim.pos.row))[vim.pos.x].mode & ATTR_WDUMMY) ||
				       !vim.pos.x);
				assert(tvimactive() <= VIM_BLOCK);
				if (tvimactive() > VIM_NORMAL && sel.ob.x != -1) {
					text = getsel();
					assert(text);
					free(text);
				}
			}
			assert(sb.view_offset >= 0 && sb.view_offset <= sb_maxoffset());
		}
		tvimend();
	}
}

int
main(void)
{
	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	testmotions();
	testfind();
	testvisual();
	testwrapped();
	testhistory();
	testlargepages();
	testoutput();
	testregionalscroll();
	testscreens();
	testsearch();
	testendedselection();
	testearliersearch();
	testsearchcounts();
	teststress();
	puts("PASS: keyboard selection motions, counts, visual modes, wrapped and wide text, history, live output, screens, resizing, search and random keys");
	return 0;
}
