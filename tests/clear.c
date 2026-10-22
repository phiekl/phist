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
screen_is_blank(void)
{
	int x, y;

	for (y = 0; y < term.row; y++)
		for (x = 0; x < term.col; x++)
			assert(term.line[y][x].u == ' ' && term.line[y][x].mode == 0);
}

int
main(void)
{
	const char *lines[] = { "one", "two", "three", "four", "five", "six" };
	const char *clears[] = { "\033[H\033[J", "\033[H\033[2J", "\033[1f\033[0J" };
	const char *url = "https://example.org/long/path?q=one&next=two#anchor";
	Arg top = { .i = 1000 };
	char text[32], *copied;
	int i, n, x, y;

	reset(20, 5);
	put("one\r\ntwo\r\nthree\r\nfour\r\nfive\r\nsix");
	assert(sb.len == 1);
	x = term.c.x;
	y = term.c.y;
	put("\033[2J");
	assert(sb.len == 6 && term.c.x == x && term.c.y == y);
	screen_is_blank();
	for (i = 0; i < 6; i++)
		line_is(sb_get(i), lines[i]);
	kscrollup(&top);
	line_is(renderline(0), "one");
	/* Clearing while viewing history must not duplicate the historical view. */
	put("\033[2J");
	assert(sb.len == 6 && sb.view_offset == 0);
	screen_is_blank();

	for (i = 0; i < LEN(clears); i++) {
		reset(20, 6);
		put("one\r\ntwo\r\nprompt> ");
		put(clears[i]);
		assert(sb.len == 3 && term.c.x == 0 && term.c.y == 0);
		screen_is_blank();
		line_is(sb_get(0), "one");
		line_is(sb_get(1), "two");
		line_is(sb_get(2), "prompt> ");
		put("new prompt");
		line_is(term.line[0], "new prompt");
	}

	reset(20, 5);
	put("top\r\nmiddle\r\nbottom\033[2;3H\033[J");
	assert(sb.len == 0 && term.c.x == 2 && term.c.y == 1);
	line_is(term.line[0], "top");
	line_is(term.line[1], "mi");
	line_is(term.line[2], "");
	reset(20, 5);
	put("top\r\nmiddle\r\nbottom\033[2;3H\033[1J");
	assert(sb.len == 0);
	line_is(term.line[0], "");
	line_is(term.line[1], "   dle");
	line_is(term.line[2], "bottom");

	reset(20, 5);
	put("one\r\ntwo\r\nthree\r\nfour\r\nfive\r\nsix");
	n = sb.len;
	put("\033[?1049halternate\033[2J");
	assert(sb.len == n && IS_SET(MODE_ALTSCREEN));
	screen_is_blank();
	put("alternate\033[H\033[J");
	assert(sb.len == n);
	put("\033[?1049l");
	assert(!IS_SET(MODE_ALTSCREEN) && sb.len == n);
	line_is(term.line[0], "two");
	line_is(term.line[4], "six");

	reset(20, 5);
	put("old\033[H\033[2Jlive");
	assert(sb.len == 1);
	put("\033[3J");
	assert(sb.len == 0);
	line_is(term.line[0], "live");
	put("\033[2J\033c");
	assert(sb.len == 0);
	screen_is_blank();

	reset(10, 8);
	put(url);
	put(" \r\nprompt");
	tflashurl(3, 1, url);
	assert(turlflashed(3, 1));
	put("\033[H\033[2Jnew");
	assert(!urls.flashtext);
	assert(sb_get(0)[term.col - 1].mode & ATTR_WRAP);
	assert(!(sb_get(sb.len - 1)[term.col - 1].mode & ATTR_WRAP));
	kscrollup(&top);
	copied = tgeturl(0, 0);
	assert(copied && !strcmp(copied, url));
	free(copied);

	reset(20, 5);
	put("\033[1;31;4:3;58:2::1:2:3mcolored\033[0m\033[2J");
	assert(sb.len == 1);
	assert((sb_get(0)[0].mode & (ATTR_BOLD | ATTR_UNDERLINE)) == (ATTR_BOLD | ATTR_UNDERLINE));
	assert(sb_get(0)[0].fg == 1 && USTYLE(sb_get(0)[0]) == 3);
	assert(sb_get(0)[0].ucolor == TRUECOLOR(1, 2, 3));

	reset(10, 3);
	put("abcdefghij \033[2J");
	assert(sb.len == 1);
	assert(!(sb_get(0)[9].mode & ATTR_WRAP));
	reset(20, 5);
	put("\r\n\r\nlast\033[2J");
	assert(sb.len == 3);
	line_is(sb_get(0), "");
	line_is(sb_get(2), "last");
	put("\033[H\033[J\033[2J");
	assert(sb.len == 3);

	reset(20, 3);
	for (i = 0; i < 70; i++) {
		put("\033[H");
		snprintf(text, sizeof(text), "screen %d", i);
		put(text);
		put("\033[2J");
	}
	assert(sb.len == 64);
	line_is(sb_get(0), "screen 6");
	line_is(sb_get(63), "screen 69");

	reset(20, 6);
	put("one\r\ntwo\r\nthree\r\nfour\r\nfive\033[2;4r\033[3;4H\033[2J");
	assert(sb.len == 5 && term.top == 1 && term.bot == 3);
	assert(term.c.x == 3 && term.c.y == 2);
	screen_is_blank();
	reset(10, 1);
	put("one\033[2J");
	assert(sb.len == 1);
	line_is(sb_get(0), "one");
	screen_is_blank();
	puts("PASS: clear/Ctrl+L scrollback, cursor and margins, partial erases, alternate screen, explicit history erase, reset, wrapped URLs, colors, blank rows, ring eviction");
	return 0;
}
