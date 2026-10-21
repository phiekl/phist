#include "terminal.h"

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

	puts("PASS: history excludes regional scrolling, deletions and explicit SU, and stores trimmed lines");
	return 0;
}
