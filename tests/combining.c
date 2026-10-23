#include "terminal.h"

static int
row(int y, const Rune *want, int n)
{
	int x;

	for (x = 0; x < n; x++)
		if (term.line[y][x].u != want[x])
			return 0;
	return 1;
}

int
main(void)
{
	if (!setlocale(LC_CTYPE, "C.UTF-8") || wcwidth(0x301) != 0) {
		puts("SKIP: no UTF-8 locale with zero width combining marks");
		return 0;
	}

	/* Marks merge into the previous cell instead of taking the next one. */
	reset(20, 3);
	put("e\314\201 a\314\210 o\314\202");
	assert(row(0, (Rune[]){ 0xE9, ' ', 0xE4, ' ', 0xF4, ' ' }, 6));
	assert(term.c.x == 5);

	/* Marks apply one after another: e, circumflex, acute. */
	put(" e\314\202\314\201");
	assert(term.line[0][6].u == 0x1EBF && term.c.x == 7);

	/* A mark without a precomposed form is dropped, keeping the base. */
	put("q\314\201x");
	assert(term.line[0][7].u == 'q' && term.line[0][8].u == 'x');
	assert(term.c.x == 9);

	/* Zero width joiners and marks at column 0 leave the line alone. */
	put("\342\200\215\r\n\314\201");
	assert(term.c.x == 0 && term.line[1][0].u == ' ');
	assert(term.line[0][9].u == ' ');

	/* The last column is found while the cursor waits to wrap. */
	reset(3, 3);
	put("abe\314\201");
	assert(term.c.y == 0 && term.line[0][2].u == 0xE9);
	assert(term.c.state & CURSOR_WRAPNEXT);
	put("z");
	assert(term.c.y == 1 && term.line[1][0].u == 'z');

	/* Wide bases keep their width; Hangul jamo compose into syllables. */
	reset(10, 3);
	put("\341\204\200\341\205\241\341\206\250");
	assert(term.line[0][0].u == 0xAC01);
	assert(term.line[0][0].mode & ATTR_WIDE);
	assert(term.line[0][1].mode & ATTR_WDUMMY && term.c.x == 2);

	/* REP repeats the composed character. */
	reset(10, 3);
	put("e\314\201\033[2b");
	assert(row(0, (Rune[]){ 0xE9, 0xE9, 0xE9 }, 3) && term.c.x == 3);

	/* Composed text is what gets selected. */
	reset(10, 3);
	put("a\314\210b");
	selstart(0, 0, 0);
	selextend(1, 0, SEL_REGULAR, 0);
	selextend(1, 0, SEL_REGULAR, 1);
	{
		char *s = getsel();
		assert(s && !strcmp(s, "\303\244b"));
		free(s);
	}

	/* Merging into a selected cell clears the selection, as overwriting does. */
	reset(10, 3);
	put("ae");
	selstart(1, 0, 0);
	selextend(1, 0, SEL_REGULAR, 0);
	selextend(1, 0, SEL_REGULAR, 1);
	assert(selected(1, 0));
	put("\314\201");
	assert(term.line[0][1].u == 0xE9 && !selected(1, 0));

	/* A selection elsewhere on the line survives. */
	selstart(0, 0, 0);
	selextend(0, 0, SEL_REGULAR, 0);
	selextend(0, 0, SEL_REGULAR, 1);
	put("o\314\201");
	assert(term.line[0][2].u == 0xF3 && selected(0, 0));

	puts("PASS: combining characters merge into the previous cell");
	return 0;
}
