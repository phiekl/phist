#include "terminal.h"

static void
line_is(Line line, const char *text)
{
	int x;

	for (x = 0; text[x]; x++)
		assert(x < term.col && line[x].u == (unsigned char)text[x]);
	for (; x < term.col; x++)
		assert(line[x].u == ' ');
}

static void
wide_pairs(Line line)
{
	int x;

	for (x = 0; x < term.col; x++) {
		if (line[x].mode & ATTR_WDUMMY)
			assert(x > 0 && line[x - 1].mode & ATTR_WIDE);
		if (term.col > 1 && line[x].mode & ATTR_WIDE)
			assert(x + 1 < term.col && line[x + 1].mode & ATTR_WDUMMY);
	}
}

int
main(int argc, char **argv)
{
	int i, j, cap;
	char text[32];
	char *copied;
	Line oldest = NULL;
	HistoryLine *index;

	assert(argc == 2);
	setlocale(LC_CTYPE, "");
	cap = scrollback_lines = atoi(argv[1]);
	reset(20, 4);
	put("A\r\nB\r\nC\033[4;1H");
	tresize(21, 4);
	line_is(term.line[0], "A");
	line_is(term.line[1], "B");
	line_is(term.line[2], "C");
	line_is(term.line[3], "");
	assert(sb.len == 0 && sb.limit == cap);

	/* Erasing with a text color must not create a second, invisible row. */
	reset(80, 4);
	put("\033[31mfoo\033[K\033[m\r\n");
	tresize(40, 4);
	assert(term.c.y == 1 && term.c.x == 0 && sb.len == 0);
	line_is(term.line[0], "foo");
	assert(sb.max_width == 3);

	/* Other long text may require reflow; colored padding still disappears. */
	reset(80, 6);
	put("\033[31mfoo\033[K\033[m\r\n");
	for (i = 0; i < 81; i++)
		put("x");
	put("\r\n");
	tresize(40, 6);
	assert(term.c.y == 4 && term.c.x == 0 && sb.len == 0);
	line_is(term.line[0], "foo");
	line_is(term.line[3], "x");

	/* Colored blank rows below the cursor do not keep history off screen. */
	reset(80, 6);
	for (i = 0; i < 6; i++)
		put("old\r\n");
	put("\033[H\033[2Jfoo\033[31m\033[2;1H\033[J\033[m\033[1;4H");
	tresize(80, 3);
	assert(term.c.y == MIN(cap, 2) && term.c.x == 3);
	line_is(term.line[term.c.y], "foo");
	assert(sb.max_width == 3);

	if (wcwidth(0x754C) == 2) {
		/* A colored but invisible margin cell is omitted when copying a wrap. */
		reset(5, 8);
		put("\033[31m\033[Kabcd\344\270\255ef\033[m");
		assert(term.line[0][4].mode == (ATTR_WRAP | ATTR_WPAD));
		selstart(0, 1, SNAP_REGEX + 100);
		selextend(0, 1, SEL_REGULAR, 1);
		copied = getsel();
		assert(copied && !strcmp(copied, "abcd\344\270\255ef"));
		free(copied);

		/* Output wrapping a wide glyph is soft wrapping too. */
		reset(5, 8);
		put("abcd\344\270\255ef");
		assert(term.line[0][4].mode == (ATTR_WRAP | ATTR_WPAD));
		selstart(0, 1, SNAP_REGEX + 100);
		selextend(0, 1, SEL_REGULAR, 1);
		copied = getsel();
		assert(copied && !strcmp(copied, "abcd\344\270\255ef"));
		free(copied);
		tresize(10, 8);
		assert(term.line[0][4].u == 0x4E2D && term.line[0][6].u == 'e');
		assert(term.line[0][7].u == 'f' && term.c.y == 0 && term.c.x == 8);

		/* Wrapping past existing text must not turn that text into padding. */
		reset(5, 8);
		put("abc\344\270\255\033[1;5H\344\270\255");
		assert(term.line[0][3].u == 0x4E2D);
		assert(term.line[0][4].mode == (ATTR_WDUMMY | ATTR_WRAP));
		assert(term.line[1][0].u == 0x4E2D);
		reset(5, 8);
		put("abcde\033[1;5H\344\270\255");
		assert(term.line[0][4].u == 'e' && term.line[0][4].mode == ATTR_WRAP);

		/* An odd boundary moves the entire wide glyph to the next row. */
		reset(10, 8);
		put("abcd\347\225\214X\0337");
		tresize(5, 8);
		assert(term.line[0][4].mode == (ATTR_WRAP | ATTR_WPAD));
		assert(term.line[1][0].u == 0x754C && term.line[1][0].mode & ATTR_WIDE);
		assert(term.line[1][1].mode == ATTR_WDUMMY && term.line[1][2].u == 'X');
		assert(term.c.y == 1 && term.c.x == 3);
		assert(term.saved[0].y == 1 && term.saved[0].x == 3);
		selstart(0, 0, SNAP_LINE);
		selextend(0, 0, SEL_REGULAR, 1);
		copied = getsel();
		assert(copied && !strcmp(copied, "abcd\347\225\214X\n"));
		free(copied);
		selstart(0, 0, SNAP_WORD);
		selextend(0, 0, SEL_REGULAR, 1);
		copied = getsel();
		assert(copied && !strcmp(copied, "abcd\347\225\214X"));
		free(copied);
		tresize(10, 8);
		assert(term.line[0][4].u == 0x754C && term.line[0][6].u == 'X');
		assert(term.c.y == 0 && term.c.x == 7);

		/* One column clips the glyph without orphaning a dummy on another row. */
		tresize(1, 8);
		assert(term.line[4][0].u == 0x754C && term.line[4][0].mode & ATTR_WIDE);
		assert(term.line[5][0].u == 'X' && !(term.line[5][0].mode & ATTR_WDUMMY));
		tresize(10, 8);
		assert(term.line[0][4].u == 0x754C && term.line[0][5].mode == ATTR_WDUMMY);
		assert(term.line[0][6].u == 'X' && term.c.y == 0 && term.c.x == 7);

		/* Both cursors follow a wide glyph moved past the padding cell. */
		reset(10, 8);
		put("abcd\347\225\214X\033[1;5H\0337");
		tresize(5, 8);
		assert(term.c.y == 1 && term.c.x == 0);
		assert(term.saved[0].y == 1 && term.saved[0].x == 0);

		/* A pair ending at the right edge keeps its wrapped dummy intact. */
		reset(10, 8);
		put("abc\347\225\214XY");
		tresize(5, 8);
		assert(term.line[0][3].mode & ATTR_WIDE);
		assert(term.line[0][4].mode == (ATTR_WDUMMY | ATTR_WRAP));
		line_is(term.line[1], "XY");

		reset(4, 8);
		put("ab\347\225\214");
		tresize(3, 8);
		assert(term.c.y == 1 && term.c.x == 2 && !(term.c.state & CURSOR_WRAPNEXT));
		assert(term.line[1][0].u == 0x754C && term.line[1][1].mode == ATTR_WDUMMY);

		/* Ring eviction and restoring the live screen preserve the same invariant. */
		reset(10, 3);
		for (i = 0; i < 20; i++)
			put("abcd\347\225\214X\r\n");
		tresize(5, 3);
		for (i = 0; i < sb.len; i++)
			wide_pairs(sb_get(i));
		for (i = 0; i < term.row; i++)
			wide_pairs(term.line[i]);
	}

	/* Visible blank cells survive trimming and count when deciding to reflow. */
	reset(10, 4);
	put("A\033[7m   \033[0m\r\nB\033[4:3;58:5:2m   \033[0m\r\nC\033[9m   \033[0m");
	tresize(3, 8);
	assert(term.line[0][1].mode & ATTR_REVERSE);
	assert(term.line[1][0].mode & ATTR_REVERSE);
	assert(USTYLE(term.line[2][1]) == 3 && term.line[2][1].ucolor == 2);
	assert(USTYLE(term.line[3][0]) == 3 && term.line[3][0].ucolor == 2);
	assert(term.line[4][1].mode & ATTR_STRUCK);
	assert(term.line[5][0].mode & ATTR_STRUCK);
	tresize(10, 8);
	assert(term.line[0][3].mode & ATTR_REVERSE);
	assert(USTYLE(term.line[1][3]) == 3 && term.line[1][3].ucolor == 2);
	assert(term.line[2][3].mode & ATTR_STRUCK);

	/* A styled blank row below the cursor is content too. */
	reset(10, 4);
	put("prompt\033[3;1H\033[7m    \033[0m\033[H");
	tresize(5, 6);
	assert(term.line[3][3].mode & ATTR_REVERSE);

	/* Shrinking keeps the cursor's text, even with occupied rows below it. */
	reset(10, 4);
	put("A\r\nB\r\nC\r\nD\033[H\0337");
	tresize(10, 3);
	assert(term.c.y == 0 && term.saved[0].y == 0);
	line_is(term.line[0], "A");
	line_is(term.line[2], "C");
	put("X");
	line_is(term.line[0], "X");

	/* Reflow must not evict the cursor before choosing the restored rows. */
	reset(10, 4);
	put("abcdefghij\r\nABCDEFGHIJ\r\n0123456789\r\nklmnopqrst\033[1;3H");
	tresize(5, 3);
	assert(term.c.y == 0 && term.c.x == 2);
	line_is(term.line[0], "abcde");
	line_is(term.line[1], "fghij");
	line_is(term.line[2], "ABCDE");
	assert(sb.len == 0 && sb.limit == cap);

	reset(20, 3);
	for (i = 0; i < cap + 3; i++) {
		snprintf(text, sizeof(text), "%sline%d", i ? "\r\n" : "", i);
		put(text);
	}
	assert(sb.len == cap);
	index = sb.buf;
	if (cap)
		oldest = sb.buf[sb.head].cells;
	tresize(20, 3);
	assert(sb.len == cap);
	tresize(21, 3);
	assert(sb.len == cap && sb.limit == cap && sb.buf == index);
	if (cap) {
		assert(sb.buf[sb.head].cells == oldest && sb.buf[sb.head].cols == 5);
		assert(sb_get(0)[20].u == ' ' && sb.buf[sb.head].cols == 5);
	}
	for (i = 0; i < cap; i++) {
		snprintf(text, sizeof(text), "line%d", i);
		line_is(sb_get(i), text);
	}
	for (i = 0; i < 3; i++) {
		snprintf(text, sizeof(text), "line%d", cap + i);
		line_is(term.line[i], text);
	}
	tresize(20, 3);
	assert(sb.len == cap && sb.buf == index);

	reset(20, 4);
	put("A\r\nB\r\nC\r\nD\r\nE\r\nF\r\nPROMPT");
	i = MIN(2, MIN(cap, 3));
	put("\033[?1049hvim");
	tresize(20, 6);
	put("\033[?1049l");
	assert(term.c.y == 3 + i);
	line_is(term.line[term.c.y], "PROMPT");
	put("\033[?1049hvim");
	tresize(20, 3);
	put("\033[?1049l");
	assert(term.c.y == 2);
	line_is(term.line[2], "PROMPT");
	reset(20, 6);
	put("old\033[6;1H\033[?1049hvim\033[?1049l\033[H\033[2JPROMPT");
	tresize(19, 3);
	line_is(term.line[term.c.y], "PROMPT");
	tresize(21, 3);
	line_is(term.line[term.c.y], "PROMPT");
	reset(20, 6);
	put("\033[6;1H\0337\033[H\033[2JPROMPT");
	tresize(5, 3);
	assert(term.line[term.c.y][term.c.x - 1].u == 'T');
	/* An excluded blank cursor follows content without retaining blank rows. */
	for (j = 0; j < 3; j++) {
		reset(10, 6);
		put("abcdefghij\r\n1\r\n2\r\n3\r\n4\r\n5\r\n6");
		put("\033[H\033[2JPROMPT\033[4;1H\0337\033[H");
		tresize(j == 2 ? 5 : j ? 20 : 10, 16);
		for (i = term.row - 1; i > 0 && !tlinelen(term.line[i]); i--)
			;
		put("\0338");
		assert(term.c.x == 0 && term.c.y == i + 3);
	}
	for (i = 0; i < 2; i++) {
		reset(20, 6);
		put("\033[6;1H\0337\033[HPROMPT");
		snprintf(text, sizeof(text), "\033[?%dh\033[6;1Hvim", i ? 1047 : 47);
		put(text);
		tresize(19, 3);
		line_is(term.alt[term.mainc.y], "PROMPT");
		snprintf(text, sizeof(text), "\033[?%dl", i ? 1047 : 47);
		put(text);
		line_is(term.line[0], "PROMPT");
		assert(term.c.x == 3 && term.c.y == 2);

		/* Modes 47/1047 share the active cursor; DECRC is explicit. */
		reset(20, 6);
		put("PROMPT\0337");
		snprintf(text, sizeof(text), "\033[?%dh\033[4;3H\033[31m", i ? 1047 : 47);
		put(text);
		snprintf(text, sizeof(text), "\033[?%dl", i ? 1047 : 47);
		put(text);
		assert(term.c.x == 2 && term.c.y == 3 && term.c.attr.fg == 1);
		put("\0338");
		assert(term.c.x == 6 && term.c.y == 0 && term.c.attr.fg == defaultfg);

		/* Both resize paths map an explicitly saved main cursor. */
		for (j = 0; j < 2; j++) {
			reset(20, 4);
			put("A\r\nB\r\nC\r\nD\r\nE\r\nF\r\nPROMPT\0337");
			snprintf(text, sizeof(text), "\033[?%dh\033[Halternate", i ? 1047 : 47);
			put(text);
			tresize(j ? 5 : 20, 6);
			assert(term.mainc.x == term.saved[0].x && term.mainc.y == term.saved[0].y);
			snprintf(text, sizeof(text), "\033[?%dl\0338", i ? 1047 : 47);
			put(text);
			assert(term.c.x > 0 && term.line[term.c.y][term.c.x - 1].u == 'T');
			put("X");
			assert(term.line[term.c.y][term.c.x - 1].u == 'X');
		}
	}

	/* Alternate-screen contents survive, keeping the cursor on screen. */
	reset(10, 6);
	put("main\033[?1049h\033[Htop\r\n\r\n\r\n\r\nfifth\033[6;1Hlast");
	tresize(8, 6);
	line_is(term.line[0], "top");
	line_is(term.line[4], "fifth");
	tresize(12, 3);
	line_is(term.line[1], "fifth");
	line_is(term.line[2], "last");
	assert(term.c.y == 2 && term.c.x == 4);
	put("\033[?1049l");
	line_is(term.line[0], "main");

	/* A resize ends the selection rather than leaving it on other text. */
	reset(20, 4);
	put("first line\r\nsecond line\r\nthird line");
	selstart(0, 1, 0);
	selextend(5, 1, SEL_REGULAR, 0);
	selextend(5, 1, SEL_REGULAR, 1);
	tresize(5, 4);
	assert(!getsel());

	reset(10, 6);
	put("abcdefghijABCDEFGHIJklmnopqr\033[1;3H");
	tresize(5, 6);
	assert(term.c.y == 0 && term.c.x == 2);
	assert(term.line[term.c.y][term.c.x].u == 'c');
	tresize(10, 6);
	assert(term.c.y == 0 && term.c.x == 2);
	line_is(term.line[0], "abcdefghij");

	/* Resizing keeps stops and continues the last one's spacing into new columns. */
	tabspaces = 4;
	reset(32, 4);
	assert(term.tabs[4] && term.tabs[8] && term.tabs[28]);
	tresize(32, 5);
	assert(term.tabs[4] && term.tabs[8] && term.tabs[28]);
	put("\033[3g\033[1;6H\033H");
	tresize(40, 6);
	for (i = 0; i < term.col; i++)
		assert(!!term.tabs[i] == (i == 5 || i == 33 || i == 37));
	put("\033[H\t\t");
	assert(term.c.x == 33);
	tabspaces = 8;
	/* Stops beyond a narrowed window are forgotten; widening adds defaults again. */
	reset(80, 4);
	put("\033[1;60H\033H");
	tresize(6, 4);
	tresize(80, 4);
	for (i = 0; i < term.col; i++)
		assert(!!term.tabs[i] == (i > 0 && i % tabspaces == 0));
	reset(16, 4);
	put("\033[3g");
	tresize(40, 4);
	for (i = 0; i < term.col; i++)
		assert(!!term.tabs[i] == (i == 16 || i == 24 || i == 32));
	/* Zero spacing must be finite on reset and growth. */
	tabspaces = 0;
	reset(4, 3);
	assert(!term.tabs[0] && term.tabs[1] && term.tabs[3]);
	put("\033[H\t");
	assert(term.c.x == 1);
	tresize(8, 3);
	for (i = 1; i < term.col; i++)
		assert(term.tabs[i]);
	tabspaces = 8;

	reset(10, 6);
	put("abcdefghijABCDEFGHIJklmnopqr\033[2;4H\0337\033[3;2H");
	tresize(5, 6);
	assert(term.line[term.c.y][term.c.x].u == 'l');
	put("\0338");
	assert(term.line[term.c.y][term.c.x].u == 'D');

	reset(10, 6);
	put("abcdefghij");
	tresize(5, 6);
	assert(term.c.y == 1 && term.c.x == 4 && term.c.state & CURSOR_WRAPNEXT);
	put("K");
	assert(term.line[2][0].u == 'K');
	tresize(20, 6);
	assert(term.c.y == 0 && term.c.x == 11);
	line_is(term.line[0], "abcdefghijK");
	reset(10, 6);
	put("abcdefghij");
	tresize(20, 6);
	assert(term.c.y == 0 && term.c.x == 10 && !(term.c.state & CURSOR_WRAPNEXT));
	line_is(term.line[0], "abcdefghij");

	/* A final blank still leaves the cursor logically after the old margin. */
	reset(10, 4);
	put("123456789 \0337");
	tresize(20, 4);
	assert(term.c.x == 10 && !(term.c.state & CURSOR_WRAPNEXT));
	assert(term.saved[0].x == 10 && !(term.saved[0].state & CURSOR_WRAPNEXT));
	put("X");
	assert(term.c.y == 0 && term.line[0][10].u == 'X');
	put("\0338Y");
	assert(term.c.y == 0 && term.line[0][10].u == 'Y');

	/* Mapping is also needed when only the saved cursor is wrap-pending. */
	reset(10, 4);
	put("123456789 \0337\r\nnext");
	tresize(20, 4);
	put("\0338X");
	assert(term.c.y == 0 && term.line[0][10].u == 'X');

	/* Copied alternate screens clear a wrap that belonged to the old width. */
	reset(10, 4);
	put("main\033[?1049h\033[H123456789 \0337");
	tresize(20, 4);
	assert(!(term.c.state & CURSOR_WRAPNEXT));
	assert(!(term.saved[1].state & CURSOR_WRAPNEXT));

	printf("PASS: resize preserves full history and screen with capacity %d\n", cap);
	return 0;
}
