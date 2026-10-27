#include "terminal.h"

#define WIDE "\344\270\255" /* U+4E2D, two columns */

static void
putrepeat(int c, size_t n)
{
	char chunk[4096];
	size_t len;

	memset(chunk, c, sizeof(chunk));
	for (; n > 0; n -= len) {
		len = MIN(n, sizeof(chunk));
		twrite(chunk, len, 0);
	}
}

int
main(void)
{
	Line line;
	int fds[2];
	ssize_t n;
	char reply[256];

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

	/* Applications cannot turn on local echo (SRM reset). */
	put("\033[H\033[2J\033[12l");
	assert(!IS_SET(MODE_ECHO));
	assert((cmdfd = open("/dev/null", O_WRONLY)) >= 0);
	ttywrite("typed", 5, 1);
	close(cmdfd);
	assert(term.c.x == 0 && term.line[0][0].u == ' ');

	/* Color changes need allowcolorchanges; queries and resets always work. */
	assert(pipe(fds) == 0 && fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
	cmdfd = fds[1];
	put("\033]4;1;#ff0000\a\033]10;#000000\a\033]11;#000000\033\\\033]12;red\a");
	assert(colorsets == 0);
	put("\033]4;1;?\a\033]11;?\a");
	n = read(fds[0], reply, sizeof(reply) - 1);
	reply[MAX(n, 0)] = '\0';
	assert(strstr(reply, "\033]4;1;rgb:") && strstr(reply, "\033]11;rgb:"));
	put("\033]104;1\a\033]110\a\033]111\a\033]112\a");
	assert(colorsets == 0 && colorresets == 4);
	allowcolorchanges = 1;
	put("\033]4;1;#ff0000\a\033]10;#000000\a\033]11;#000000\a\033]12;red\a");
	assert(colorsets == 4);
	allowcolorchanges = 0;
	close(fds[0]);
	close(fds[1]);

	/* Titles stay on one line and are cut at a UTF-8 boundary. */
	put("\033]0;evil\r\nroot@host: ~\t$\a");
	assert(!strcmp(lasttitle, "evil  root@host: ~ $"));
	assert(!strcmp(lasticontitle, lasttitle));
	put("\033]2;");
	putrepeat('x', TITLE_MAX - 1);
	put(WIDE "tail\a");
	assert(strlen(lasttitle) == TITLE_MAX - 1 && lasttitle[TITLE_MAX - 2] == 'x');
	put("\033]1;");
	putrepeat('i', 4 * TITLE_MAX);
	put("\a");
	assert(strlen(lasticontitle) == TITLE_MAX);
	put("\033kold\bstyle\033\\");
	assert(!strcmp(lasttitle, "old style"));
	put("\033k\033\\");
	assert(!strcmp(lasttitle, ""));
	/* Unicode line and paragraph separators break lines as well. */
	put("\033]0;first\342\200\250second\342\200\251third\a");
	assert(!strcmp(lasttitle, "first second third"));
	assert(!strcmp(lasticontitle, lasttitle));
	put("\033]1;a\342\200\250b\a");
	assert(!strcmp(lasticontitle, "a b"));
	put("\033]2;a\342\200\251b\a");
	assert(!strcmp(lasttitle, "a b"));
	put("\033ka\342\200\250b\033\\");
	assert(!strcmp(lasttitle, "a b"));
	/* Other characters in the same block are untouched. */
	put("\033]2;\342\200\246\342\200\242\a");
	assert(!strcmp(lasttitle, "\342\200\246\342\200\242"));
	/* Explicit bidi controls are removed from titles. */
	put("\033]2;invoice\342\200\256fdp.exe\a");
	assert(!strcmp(lasttitle, "invoicefdp.exe"));
	put("\033]0;\342\200\252a\342\200\253b\342\200\254c\342\200\255d\342\200\216e"
	    "\342\200\217f\330\234g\342\201\246h\342\201\247i\342\201\250j\342\201\251\a");
	assert(!strcmp(lasttitle, "abcdefghij") && !strcmp(lasticontitle, lasttitle));
	put("\033ka\342\200\256b\033\\");
	assert(!strcmp(lasttitle, "ab"));
	/* Invisible characters are removed, but not joiners or variation selectors. */
	put("\033]2;a\342\200\213b\342\201\240c\357\273\277d\302\255e\315\217f"
	    "\341\205\237g\343\205\244h\342\201\242i\341\240\216j\363\240\201\201k\a");
	assert(!strcmp(lasttitle, "abcdefghijk"));
	put("\033]0;\360\237\221\250\342\200\215\360\237\221\251 \342\235\244\357\270\217 "
	    "\330\252\342\200\214\330\250 \350\276\273\363\240\204\200\a");
	assert(!strcmp(lasttitle, "\360\237\221\250\342\200\215\360\237\221\251 \342\235\244\357\270\217 "
	    "\330\252\342\200\214\330\250 \350\276\273\363\240\204\200"));
	assert(!strcmp(lasticontitle, lasttitle));
	put("\033ka\342\200\213b\033\\");
	assert(!strcmp(lasttitle, "ab"));
	/* Right-to-left text itself is kept. */
	put("\033]2;\327\251\327\234\327\225\327\235 \330\263\331\204\330\247\331\205\a");
	assert(!strcmp(lasttitle, "\327\251\327\234\327\225\327\235 \330\263\331\204\330\247\331\205"));
	/* The parser has already replaced invalid and truncated UTF-8. */
	put("\033]2;a\377b\342\200\a");
	assert(!strcmp(lasttitle, "a\357\277\275b\357\277\275"));

	/* The byte limit applies after replacement, so a title of separators is kept. */
	put("\033]2;");
	for (n = 0; n < TITLE_MAX; n++)
		put("\342\200\250");
	put("\a");
	assert(strlen(lasttitle) == TITLE_MAX && strspn(lasttitle, " ") == TITLE_MAX);

	/* Strings up to the limit are kept; longer ones stop growing and are dropped. */
	put("\033[H\033[2J\033]2;");
	putrepeat('t', STR_MAX_SIZ - 16);
	assert(!strescseq.overflow && strescseq.len == STR_MAX_SIZ - 14);
	put("\033\\");
	assert(!(term.esc & ESC_STR));
	put("\033P");
	putrepeat('d', 8 * STR_MAX_SIZ);
	assert(strescseq.overflow && strescseq.siz == STR_MAX_SIZ);
	assert(term.c.x == 0 && term.line[0][0].u == ' ');
	put("\033\\ok");
	assert(!(term.esc & ESC_STR) && term.line[0][0].u == 'o');
	put("\033]0;title\a");
	assert(!strescseq.overflow && strescseq.siz == STR_BUF_SIZ);

	puts("PASS: charset, escape finals, wide overwrite, CSI parameter bounds, no local echo, opt-in color changes, title sanitising and string length limit");
	return 0;
}
