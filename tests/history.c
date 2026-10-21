#include "terminal.h"

static void
expectsel(int col, int row, const char *want)
{
	char *got;

	selstart(col, row, SNAP_LINE);
	selextend(col, row, SEL_REGULAR, 1);
	got = getsel();
	assert(got && !strcmp(got, want));
	free(got);
	selclear();
}

int
main(void)
{
	int i;

	reset(20, 5);
	put("A\r\nB\r\nC\r\nD\r\nE\033[2;4r\033[4;1H\n");
	assert(sb.len == 0 && term.line[0][0].u == 'A');
	assert(term.line[1][0].u == 'C' && term.line[4][0].u == 'E');
	reset(20, 5);
	put("A\r\nB\r\nC\r\nD\r\nE\033[1;1H\033[M");
	assert(sb.len == 0 && term.line[0][0].u == 'B');
	put("\033[S");
	assert(sb.len == 0 && term.line[0][0].u == 'C');
	reset(20, 3);
	put("A\r\nB\r\nC\r\nD");
	assert(sb.len == 1 && sb_get(0)[0].u == 'A');
	put("\033D");
	assert(sb.len == 2 && sb_get(1)[0].u == 'B');
	put("\033[?1049h1\r\n2\r\n3\r\n4");
	assert(sb.len == 2);
	/* History stores lines without trailing blanks and pads them on access. */
	reset(80, 3);
	put("short\033[41m \033[0m\r\n");
	put("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz0123wrapped");
	put("\r\n\r\n\r\n\r\n");
	assert(sb.len >= 3);
	assert(sb.buf[sb_phys_index(0)].cols == 6); /* colored blank is kept */
	assert(sb.buf[sb_phys_index(1)].cols == 80);
	assert(sb.buf[sb_phys_index(2)].cols == 9);
	tresize(40, 3);
	for (i = 0; i < sb.len; i++)
		assert(sb.buf[sb_phys_index(i)].cols <= 40);
	assert(sb.buf[sb_phys_index(0)].cols == 6);
	assert(sb_get(0)[39].u == ' ' && sb_get(0)[39].bg == defaultbg);
	assert(sb_get(0)[5].bg == 1 && sb.buf[sb_phys_index(0)].cols == 6);
	/* Foreground-only erase padding has no visible content to retain. */
	reset(80, 3);
	put("\033[31mfoo\033[K\033[m\r\n\r\n\r\n");
	assert(sb.len == 1 && sb.buf[sb.head].cols == 3 && sb.max_width == 3);
	assert(sb_get(0)[0].fg == 1 && sb_get(0)[79].fg == defaultfg);
	/* Viewing and selecting history leaves its storage trimmed. */
	{
		Arg step = { .i = 1 };
		char *got;

		reset(80, 3);
		for (i = 0; i < 40; i++)
			put("row\r\n");
		for (i = 0; i < sb.len; i++) {
			kscrollup(&step);
			tattrset(ATTR_BLINK); /* reads every displayed row */
		}
		selstart(0, 0, SNAP_LINE);
		selextend(0, term.row - 1, SEL_REGULAR, 1);
		got = getsel();
		assert(got && !strcmp(got, "row\nrow\nrow\n"));
		free(got);
		for (i = 0; i < sb.len; i++)
			assert(sb.buf[sb_phys_index(i)].cols == 3);
		selclear();
	}

	/* On the alternate screen, scrolling reveals the main screen, then history. */
	{
		Arg up = { .i = 1 }, far = { .i = 1000 };

		reset(8, 3);
		put("h1\r\nh2\r\nm1\r\nm2\r\nwrapping");
		assert(sb.len == 2);
		put("\033[?1049h\033[Ha1\r\na2\r\na3");
		assert(renderline(0) == term.line[0]);
		kscrollup(&up);
		assert(renderline(0)[0].u == 'w' && renderline(1)[1].u == '1');
		assert(renderline(2)[1].u == '2');
		kscrollup(&far);
		assert(sb.view_offset == sb.len + term.row);
		assert(renderline(0)[1].u == '1' && renderline(2)[0].u == 'm');
		/* Main-screen rows never wrap onto the alternate screen. */
		term.alt[term.row - 1][term.col - 1].mode |= ATTR_WRAP;
		sb.view_offset = 1;
		assert(viewline(0)[0].u == 'w' && !(viewline(0)[term.col - 1].mode & ATTR_WRAP));
		assert(term.alt[term.row - 1][term.col - 1].mode & ATTR_WRAP);
		expectsel(1, 1, "a1\n");
		/* Output keeps the view; typing returns to the live screen. */
		put("x");
		assert(sb.view_offset == 1);
		ttyinput("", 0, 0);
		assert(sb.view_offset == 1);
		kscrolldown(&far);
		assert(sb.view_offset == 0 && renderline(0) == term.line[0]);
		/* Leaving the alternate screen keeps the same history rows in view. */
		kscrollup(&far);
		put("\033[?1049l");
		assert(sb.view_offset == sb.len && renderline(0)[1].u == '1');
		put("\033[?1049h");
		assert(sb.view_offset == sb.len + term.row);
		sb.view_offset = 2;
		put("\033[?1049l");
		assert(sb.view_offset == 0);
	}

	puts("PASS: history excludes regional scrolling, deletions and explicit SU, stores trimmed lines and shows beneath the alternate screen");
	return 0;
}
