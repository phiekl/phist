#include "terminal.h"

#define WIDE "\344\270\255" /* U+4E2D, two columns */

/* Run put(text) with stderr captured; return what was printed. */
static char *
puterr(const char *text)
{
	static char out[65536];
	FILE *f = tmpfile();
	int saved = dup(2);
	size_t n;

	assert(f && saved >= 0);
	fflush(stderr);
	dup2(fileno(f), 2);
	put(text);
	fflush(stderr);
	dup2(saved, 2);
	close(saved);
	rewind(f);
	n = fread(out, 1, sizeof(out) - 1, f);
	out[n] = '\0';
	fclose(f);
	return out;
}

static int
countlines(const char *s)
{
	int n = 0;

	for (; *s; s++)
		n += *s == '\n';
	return n;
}

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

static void
base64_is(const char *encoded, const char *expected)
{
	char *decoded = base64dec(encoded);

	assert(!strcmp(decoded, expected));
	free(decoded);
}

int
main(void)
{
	Line line;
	int fds[2], loc, m;
	ssize_t n;
	char reply[256];

	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	base64_is("QQ==", "A");
	base64_is("QUI=", "AB");
	base64_is("QUJD", "ABC");
	base64_is("QUJDRA==", "ABCD");
	reset(10, 4);

	/* OSC 7 is metadata shared by both screens, kept across clears and
	 * resize, and removed by RIS. Embedded NUL must not truncate a URI. */
	put("\033]7;file://host/project\a\033[?1049h\033[?1049l\033[2J");
	tresize(12, 5);
	assert(!strcmp(lastcwd, "file://host/project"));
	{
		static const char sequence[] = "\033]7;file://host/wrong\0suffix\a";
		twrite(sequence, sizeof(sequence) - 1, 0);
		assert(!strcmp(lastcwd, "file://host/project"));
	}
	put("\033c");
	assert(!lastcwd[0]);
	reset(10, 4);

	/* Vim's CSI intermediate-byte probe is a silent no-op, not SGR 0. */
	put("\033[1;31mX");
	assert(!*puterr("\033[0%m"));
	assert(term.c.attr.fg == 1 && (term.c.attr.mode & ATTR_BOLD));
	assert(term.c.x == 1 && term.c.y == 0 && term.line[0][0].u == 'X');
	assert(!strcmp(puterr("\033[1%m"), "erresc: unknown csi ESC[1%m\n"));
	assert(!strcmp(puterr("\033[0%z"), "erresc: unknown csi ESC[0%z\n"));
	assert(!strcmp(puterr("\033[?0%m"), "erresc: unknown csi ESC[?0%m\n"));
	assert(!strcmp(puterr("\033[0;0%m"), "erresc: unknown csi ESC[0;0%m\n"));
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
	/* Resetting the whole palette redraws text that still has the old colors. */
	memset(term.dirty, 0, term.row * sizeof(*term.dirty));
	put("\033]104\a");
	assert(term.dirty[0] && term.dirty[term.row - 1]);
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
	/*
	 * Only a few combining marks are kept per character, also in locales
	 * where wcwidth() does not know them.
	 */
	for (loc = 0; loc < 2; loc++) {
		assert(setlocale(LC_CTYPE, loc ? "C" : "C.UTF-8"));
		assert(loc ? wcwidth(0x0336) != 0 : wcwidth(0x0336) == 0);
		put("\033]2;a\314\200\314\201\314\202\314\203\314\204\314\205\314\206b\314\200 "
		    "c\314\200\314\201\314\202\314\203\a");
		assert(!strcmp(lasttitle, "a\314\200\314\201\314\202\314\203b\314\200 "
		    "c\314\200\314\201\314\202\314\203"));
		put("\033]2;\360\237\221\250\342\200\215\360\237\221\251\342\200\215\360\237\221\247"
		    "\342\200\215\360\237\221\246 \340\244\225\340\245\215\340\244\267\340\245\200\a");
		assert(!strcmp(lasttitle, "\360\237\221\250\342\200\215\360\237\221\251\342\200\215\360\237\221\247"
		    "\342\200\215\360\237\221\246 \340\244\225\340\245\215\340\244\267\340\245\200"));
		put("\033]2;");
		for (m = 0; m < 200; m++)
			put("\314\266");
		put("x\a");
		assert(!strcmp(lasttitle, "\314\266\314\266\314\266\314\266x"));
	}
	assert(setlocale(LC_CTYPE, "C.UTF-8") && wcwidth(0x0300) == 0);
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

	/* Diagnostics are rate-limited and dumps are cut short. */
	{
		static char flood[1000 * 6 + 1], longstr[10010], longcsi[ESC_BUF_SIZ];
		char *out;

		for (m = 0; m < 1000; m++)
			memcpy(flood + 6 * m, "\033[999x", 6);
		errlimit.tokens = ERRESC_BURST;
		errlimit.suppressed = 0;
		out = puterr(flood);
		assert(countlines(out) == ERRESC_BURST);
		assert(strstr(out, "erresc: unknown csi ESC[999x\n") == out);
		errlimit.tokens = 1; /* as if time had passed */
		out = puterr("\033[998x");
		assert(!strncmp(out, "erresc: 980 messages suppressed\n"
		                "erresc: unknown csi ESC[998x\n", 999));
		errlimit.tokens = ERRESC_BURST;
		snprintf(longstr, sizeof(longstr), "\033]9");
		memset(longstr + 3, 'x', 10000);
		strcpy(longstr + 10003, "\a");
		out = puterr(longstr);
		assert(countlines(out) == 1 && strstr(out, "...(10001 bytes)\n"));
		assert(strlen(out) < 2 * DUMP_MAX);
		longcsi[0] = '\033';
		longcsi[1] = '[';
		memset(longcsi + 2, '1', sizeof(longcsi) - 4);
		longcsi[sizeof(longcsi) - 2] = 'x';
		out = puterr(longcsi);
		assert(countlines(out) == 1 && strstr(out, "...("));
		assert(strlen(out) < 2 * DUMP_MAX);
		puterr("\033[H\033[2J");
	}

	puts("PASS: charset, escape finals, wide overwrite, CSI parameter bounds, no local echo, opt-in color changes, title sanitising, string length limit and rate-limited diagnostics");

	/* OSC numbers do not wrap around, and must be numbers throughout. */
	{
		const char *badops[] = { "4294967298", "2147483648", "99999999999999999999",
			"2x", "x2", "+2", "-2", " 2" };
		const char *badindexes[] = { "4294967296", "4294967297", "2147483648", "0x",
			"x", "", "+0", "-0", " 0" };
		char text[64];
		int sets, resets, saved = allowcolorchanges;
		size_t i;

		allowcolorchanges = 1;
		reset(20, 4);
		put("\033]2;kept\007");
		for (i = 0; i < LEN(badops); i++) {
			snprintf(text, sizeof(text), "\033]%s;changed\007", badops[i]);
			put(text);
			assert(!strcmp(lasttitle, "kept"));
		}
		put("\033]2147483647;changed\007"); /* the largest number, not a known opcode */
		assert(!strcmp(lasttitle, "kept"));
		put("\033]02;leading zero\007");
		assert(!strcmp(lasttitle, "leading zero"));
		put("\033];empty\007");
		assert(!strcmp(lasttitle, "empty"));

		sets = colorsets;
		resets = colorresets;
		for (i = 0; i < LEN(badindexes); i++) {
			snprintf(text, sizeof(text), "\033]4;%s;red\007", badindexes[i]);
			put(text);
			assert(colorsets == sets);
			snprintf(text, sizeof(text), "\033]104;%s\007", badindexes[i]);
			put(text);
			assert(colorresets == resets);
		}
		put("\033]4;0;red\007\033]4;255;red\007");
		assert(colorsets == sets + 2);
		put("\033]104;7\007");
		assert(colorresets == resets + 1);
		allowcolorchanges = saved;
		/* Permission checks still come first. */
		allowcolorchanges = 0;
		sets = colorsets;
		put("\033]4;0;red\007");
		assert(colorsets == sets);
		allowcolorchanges = saved;
		puts("PASS: OSC opcodes and color indexes are checked whole numbers");
	}
	return 0;
}
