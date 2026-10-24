#include "terminal.h"

#define WIDE "\344\270\255" /* U+4E2D, two columns */

int
main(void)
{
	Line line;

	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	reset(10, 4);

	/* Charset designators wider than a byte are rejected, not truncated. */
	put("\033(0");
	assert(term.trantbl[0] == CS_GRAPHIC0);
	put("\033(\304\200"); /* U+0100 truncates to NUL */
	assert(term.trantbl[0] == CS_GRAPHIC0);
	put("\033(B\033(\304\260"); /* U+0130 truncates to '0' */
	assert(term.trantbl[0] == CS_USA);

	/* Other escape finals and CSI bytes are not truncated either. */
	put("\033[H\033[2Jab\033\305\243"); /* U+0163 truncates to 'c' (RIS) */
	assert(term.c.x == 2 && term.line[0][0].u == 'a');
	put("\033#\304\270"); /* U+0138 truncates to '8' (DECALN) */
	assert(term.line[1][0].u != 'E');
	put("\033%\305\200"); /* U+0140 truncates to '@' */
	assert(IS_SET(MODE_UTF8));
	put("\033[\304\262J"); /* U+0132 truncates to '2' (ED 2) */
	assert(term.line[0][0].u == 'a');

	/* A wide glyph drops the dummy of a styled wide glyph it covers. */
	put("\033[H\033[2Ja\033[7m" WIDE "\033[m\r" WIDE);
	line = term.line[0];
	assert((line[0].mode & ATTR_WIDE) && (line[1].mode & ATTR_WDUMMY));
	assert(!(line[2].mode & ATTR_WDUMMY) && line[2].u == ' ');

	/* Out-of-range parameters clamp instead of overflowing. */
	put("\033[3;3H\033[-2147483648A");
	assert(term.c.x == 2 && term.c.y == 1);
	put("\033[-1B");
	assert(term.c.y == 2);
	put("\033[99999999999B\033[2147483647C");
	assert(term.c.x == 9 && term.c.y == 3);
	put("\033[2147483647;2147483647H\033[2147483647D");
	assert(term.c.x == 0 && term.c.y == 3);

	puts("PASS: charset, escape finals, wide overwrite and CSI parameter bounds");
	return 0;
}
