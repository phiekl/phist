#include "terminal.h"

int
main(void)
{
	setlocale(LC_CTYPE, "");

	/* U+FE0F marks the previous character; following text is unaffected. */
	reset(10, 3);
	put("\342\230\272\357\270\217x\342\230\272");
	assert(term.line[0][0].u == 0x263A && EMOJIPRES(term.line[0][0].mode));
	assert(term.line[0][1].u == 'x' && !EMOJIPRES(term.line[0][1].mode));
	assert(!EMOJIPRES(term.line[0][2].mode));
	assert(term.c.x == 3);

	/* U+FE0E selects text again, and a selector alone at column 0 is ignored. */
	put("\357\270\217\357\270\216");
	assert(!EMOJIPRES(term.line[0][2].mode) && term.c.x == 3);
	put("\r\n\357\270\217");
	assert(term.c.x == 0 && term.line[1][0].u == ' ' && !term.line[1][0].mode);

	/* The last column is found while the cursor waits to wrap. */
	reset(4, 3);
	put("abc\342\230\272\357\270\217");
	assert(term.c.y == 0 && EMOJIPRES(term.line[0][3].mode));

	/* Wide characters default to emoji; U+FE0E asks for text instead. */
	if (wcwidth(0x1F600) == 2) {
		reset(10, 3);
		put("\360\237\230\200");
		assert(EMOJIPRES(term.line[0][0].mode));
		put("\357\270\216");
		assert(!EMOJIPRES(term.line[0][0].mode));
		assert(term.line[0][1].mode == ATTR_WDUMMY);
		put("\357\270\217");
		assert(EMOJIPRES(term.line[0][0].mode));
		assert(!(term.line[0][0].mode & ATTR_ALTPRES));
		put("\357\270\216");
	}

	/* Overwriting the cell clears the presentation. */
	put("\rz");
	assert(!(term.line[0][0].mode & ATTR_ALTPRES));

	puts("PASS: variation selectors choose the presentation of the previous cell");
	return 0;
}
