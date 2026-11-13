#include <regex.h>

static unsigned int regexcalls;

static int
counted_regexec(const regex_t *regex, const char *text, size_t count,
               regmatch_t matches[], int flags)
{
	regexcalls++;
	return regexec(regex, text, count, matches, flags);
}

#define regexec counted_regexec
#include "terminal.h"
#undef regexec

static void
expect(int col, int row, int step, const char *want)
{
	char *got;

	selstart(col, row, SNAP_REGEX + step);
	selextend(col, row, SEL_REGULAR, 1);
	got = getsel();
	if (!got || strcmp(got, want)) {
		fprintf(stderr, "selection at %d,%d step %d: expected <%s>, got <%s>\n",
		        col, row, step, want, got ? got : "(none)");
		abort();
	}
	free(got);
}

static void
expectpath(const char *text, const char *want)
{
	regmatch_t matches[2];

	if (regexec(&selpatterns[1], text, 2, matches, 0) ||
	    matches[1].rm_so < 0 ||
	    (size_t)(matches[1].rm_eo - matches[1].rm_so) != strlen(want) ||
	    strncmp(text + matches[1].rm_so, want, strlen(want))) {
		fprintf(stderr, "path pattern in <%s>: expected <%s>\n", text, want);
		abort();
	}
}

static void
drag(int col, int row, int endcol)
{
	selstart(col, row, 0);
	selextend(endcol, row, SEL_REGULAR, 0);
	selextend(endcol, row, SEL_REGULAR, 1);
}

static void
expectcurrent(const char *want)
{
	char *got = getsel();

	if (!got || strcmp(got, want)) {
		fprintf(stderr, "expected <%s>, got <%s>\n", want, got ? got : "(none)");
		abort();
	}
	free(got);
}

int
main(void)
{
	const char *path = "./alpha/beta.txt";
	const char *wrapped = "'./alpha/beta.txt'";
	Arg scroll = { .i = 1 };
	int i;

	/* Past INT_MAX, which wrapped to a buffer too small for the text. */
	assert(selbufsize(80, 24) == 81 * 24 * UTF_SIZ);
	if (sizeof(size_t) > 4) {
		assert(selbufsize(1100, 1000000) == (size_t)4404000000u);
		assert(selbufsize(600, 1000000) == (size_t)2404000000u);
	}

	setlocale(LC_CTYPE, "");
	worddelimiters = L" \t\r\n`~!#$%^&*()=+[{]}\\|;:'\",<>?";
	reset(80, 5);
	put("name: './alpha/beta.txt' done");
	expect(17, 0, 0, "beta.txt");
	expect(17, 0, 1, path);
	expect(17, 0, 2, "name: './alpha/beta.txt' done");
	expect(17, 0, 100, "name: './alpha/beta.txt' done");

	/* The smallest word match includes filename punctuation; an email
	 * address or user@host follows its parts. */
	reset(80, 5);
	put("<name_1-test@example.org> <a> <./aa/bb>");
	expect(7, 0, 0, "name_1-test");
	expect(7, 0, 1, "name_1-test@example.org");
	expect(15, 0, 0, "example.org");
	expect(15, 0, 1, "name_1-test@example.org");
	expect(27, 0, 0, "a");
	expect(36, 0, 0, "bb");
	expect(36, 0, 1, "./aa/bb");

	/* Absolute, relative, home and parent paths include one-character parts. */
	reset(80, 5);
	put("</usr/x/lib>");
	expect(8, 0, 1, "/usr/x/lib");
	reset(80, 5);
	put("<src/st.c>");
	expect(6, 0, 1, "src/st.c");
	reset(80, 5);
	put("<~/a/bb>");
	expect(5, 0, 2, "~/a/bb");
	reset(80, 5);
	put("<../foo/bar>");
	expect(8, 0, 1, "../foo/bar");
	reset(80, 5);
	put("<../../a/.b/c/>");
	expect(12, 0, 1, "../../a/.b/c/");

	/* Dot directory components are valid at the end of absolute/relative paths. */
	expectpath("open '../..' now", "../..");
	expectpath("open 'foo/..' now", "foo/..");
	expectpath("open 'x/y/../' now", "x/y/../");
	expectpath("open '../.' now", "../.");
	expectpath("open 'foo/.' now", "foo/.");
	expectpath("open '/usr/..' now", "/usr/..");
	expectpath("open '~/.config/../.' now", "~/.config/../.");
	reset(80, 5);
	put("<x/y/../>");
	expect(1, 0, 1, "x/y/../");
	reset(80, 5);
	put("</usr/..>");
	expect(2, 0, 1, "/usr/..");

	/* Sentence punctuation is excluded while dots within filenames remain. */
	reset(80, 5);
	put("see /etc/hosts.");
	expect(10, 0, 2, "/etc/hosts");
	reset(80, 5);
	put("see src/st.c.");
	expect(9, 0, 2, "src/st.c");
	reset(80, 5);
	put("see ~/.config/phist.conf...");
	expect(18, 0, 2, "~/.config/phist.conf");
	reset(80, 5);
	put("see /etc/hosts/.");
	expect(10, 0, 1, "/etc/hosts/.");

	/* Longer phrases include spaces, commas, semicolons and final punctuation. */
	reset(80, 5);
	put("<one, two; three.>");
	expect(6, 0, 0, "two");
	expect(6, 0, 1, "one, two; three.");
	expect(6, 0, 2, "<one, two; three.>");

	/* Assignment expansion stays with its own value, including across wraps. */
	{
		const char *env = "COLORTERM=1 LANG=C.UTF-8 LC_ALL=C.UTF-8 TERM=xterm-256color";
		const char *assignment = "LC_ALL=C.UTF-8";
		int widths[] = { 80, 16 };
		int start = strstr(env, "LC_ALL") - env, cols, pos;
		size_t w;

		for (w = 0; w < LEN(widths); w++) {
			cols = widths[w];
			reset(cols, 5);
			put(env);
			for (i = 0; assignment[i]; i++) {
				pos = start + i;
				expect(pos % cols, pos / cols, 0,
				       i < 6 ? "LC_ALL" : i == 6 ? "=" : "C.UTF-8");
				expect(pos % cols, pos / cols, 1, assignment);
				expect(pos % cols, pos / cols, 2, env);
			}
		}
		reset(80, 5);
		put("A=x LONG_NAME=value EMPTY=");
		expect(8, 0, 0, "LONG_NAME");
		expect(8, 0, 1, "LONG_NAME=value");
		expect(22, 0, 0, "EMPTY");
		expect(22, 0, 1, "EMPTY=");
	}

	/* Soft wrapping is transparent to regex matching and copied selections. */
	reset(8, 5);
	put(wrapped);
	expect(2, 1, 0, "beta.txt");
	expect(2, 1, 1, path);
	expect(2, 1, 2, wrapped);
	reset(8, 5);
	put("one two three four");
	expect(1, 1, 1, "one two three four");

	/* Hard line breaks must never join matches. */
	reset(8, 5);
	put("first\r\nsecond");
	expect(2, 1, 100, "second");

	/* Matching and copying include hidden ends of a wrapped logical line. */
	reset(5, 2);
	put(wrapped);
	assert(sb.len > 0);
	expect(0, 0, 100, wrapped);
	kscrollup(&scroll);
	expect(0, 0, 100, wrapped);
	assert(sel.nb.y < 0 && sel.ne.y >= term.row);

	/* History is still selectable after the view has been cleared. */
	reset(80, 5);
	put("'./alpha/beta.txt'\033[H\033[2J");
	kscrollup(&scroll);
	expect(10, 0, 0, "beta.txt");
	expect(10, 0, 1, path);

	/* UTF-8 and wide-cell coordinates must not shift ASCII matches. */
	reset(80, 5);
	put("\303\251 \347\225\214 'alpha' \347\225\214");
	expect(7, 0, 0, "alpha");
	expect(3, 0, 0, "\347\225\214");
	expect(3, 0, 100, "\303\251 \347\225\214 'alpha' \347\225\214");

	/* Empty lines, whitespace and punctuation remain safe to select. */
	reset(20, 3);
	expect(0, 0, 0, "\n");
	put("... !!!");
	expect(5, 0, 0, "!!!");
	expect(10, 0, 100, "... !!!\n");

	/* user@host in a prompt: either name, then both. */
	reset(80, 5);
	put("claude@claude:~/repos/phist (new)$");
	expect(0, 0, 0, "claude");
	expect(0, 0, 1, "claude@claude");
	expect(8, 0, 0, "claude");
	expect(8, 0, 1, "claude@claude");
	/* A unit file's value: a part, the value, then the line. */
	reset(80, 5);
	put("Requires=systemd-fsck@dev-xvdb.service");
	expect(2, 0, 0, "Requires");
	expect(2, 0, 1, "Requires=systemd-fsck@dev-xvdb.service");
	expect(12, 0, 0, "systemd-fsck");
	expect(12, 0, 1, "systemd-fsck@dev-xvdb.service");
	expect(12, 0, 2, "Requires=systemd-fsck@dev-xvdb.service");
	expect(25, 0, 0, "dev-xvdb.service");
	expect(25, 0, 1, "systemd-fsck@dev-xvdb.service");
	expect(25, 0, 2, "Requires=systemd-fsck@dev-xvdb.service");
	/* Blank-separated items of a value running to the end of the line. */
	reset(80, 5);
	put("Documentation=man:fstab(5) man:systemd-fstab-generator(8)");
	expect(35, 0, 0, "systemd-fstab-generator");
	expect(35, 0, 1, "man:systemd-fstab-generator(8)");
	expect(35, 0, 2, "man:fstab(5) man:systemd-fstab-generator(8)");
	expect(35, 0, 3, "Documentation=man:fstab(5) man:systemd-fstab-generator(8)");
	expect(19, 0, 1, "man:fstab(5)");
	/* But not where it holds further assignments, nor URLs cut at '='. */
	reset(80, 5);
	put("LC_ALL=C.UTF-8 make CC=cc -j4 all");
	expect(9, 0, 0, "C.UTF-8");
	expect(9, 0, 1, "LC_ALL=C.UTF-8");
	expect(9, 0, 2, "LC_ALL=C.UTF-8 make CC=cc -j4 all");
	reset(80, 5);
	put("see http://example.com:8080/path?q=1, ok");
	expect(28, 0, 2, "http://example.com:8080/path?q=1");

	/* A part of an IPv6 or MAC address, then all of it, then with an IPv6
	 * prefix length, whichever part is clicked; the prefix length alone,
	 * then the address with it. */
	reset(80, 5);
	put("inet6 fe80::1/64 scope link");
	expect(6, 0, 0, "fe80");
	expect(6, 0, 1, "fe80::1");
	expect(6, 0, 2, "fe80::1/64");
	expect(6, 0, 3, "inet6 fe80::1/64 scope link");
	expect(10, 0, 0, "::");
	expect(10, 0, 1, "fe80::1");
	expect(12, 0, 0, "1");
	expect(12, 0, 1, "fe80::1");
	expect(12, 0, 2, "fe80::1/64");
	expect(13, 0, 0, "/64");
	expect(13, 0, 1, "fe80::1/64");
	expect(14, 0, 0, "64");
	expect(14, 0, 1, "fe80::1/64");
	expect(14, 0, 2, "inet6 fe80::1/64 scope link");
	expect(17, 0, 0, "scope");
	expect(17, 0, 1, "inet6 fe80::1/64 scope link");
	reset(80, 5);
	put("ether aa:bb:cc:dd:ee:ff brd ff:ff:ff:ff:ff:ff");
	expect(9, 0, 0, "bb");
	expect(9, 0, 1, "aa:bb:cc:dd:ee:ff");
	expect(9, 0, 2, "ether aa:bb:cc:dd:ee:ff brd ff:ff:ff:ff:ff:ff");
	expect(8, 0, 0, ":");
	expect(8, 0, 1, "aa:bb:cc:dd:ee:ff");
	expect(43, 0, 1, "ff:ff:ff:ff:ff:ff");
	reset(80, 5);
	put("route 2001:DB8::/32 via [2001:db8::1]:443, ::ffff:192.0.2.1.");
	expect(11, 0, 0, "DB8");
	expect(11, 0, 1, "2001:DB8::");
	expect(11, 0, 2, "2001:DB8::/32");
	expect(17, 0, 0, "32");
	expect(17, 0, 1, "2001:DB8::/32");
	expect(30, 0, 0, "db8");
	expect(30, 0, 1, "2001:db8::1");
	expect(51, 0, 0, "192.0.2.1");
	expect(51, 0, 1, "::ffff:192.0.2.1");

	/* Embedded IPv4 addresses in every compressed form are whole. */
	reset(80, 5);
	put("2001:db8::abcd:192.0.2.1 1::2:3:4:5:0.0.0.0 ::1:2:3:4:5:1.2.3.4");
	expect(0, 0, 1, "2001:db8::abcd:192.0.2.1");
	expect(16, 0, 0, "192.0.2.1");
	expect(16, 0, 1, "2001:db8::abcd:192.0.2.1");
	expect(25, 0, 1, "1::2:3:4:5:0.0.0.0");
	expect(44, 0, 1, "::1:2:3:4:5:1.2.3.4");
	/* Invalid dotted suffixes leave no address, but a final period does. */
	reset(80, 5);
	put("::ffff:192.0.2.999 ::ffff:192.0.2.1extra ::ffff:1.2.3.4.");
	expect(2, 0, 1, "ffff:");
	expect(2, 0, 2, "::ffff:192.0.2.999");
	expect(21, 0, 1, "ffff:");
	expect(21, 0, 2, "::ffff:192.0.2.1extra");
	expect(43, 0, 1, "::ffff:1.2.3.4");

	/* Wrapped addresses are found across rows. */
	reset(10, 5);
	put("ab fe80::1/64 x");
	expect(3, 0, 1, "fe80::1");
	expect(3, 0, 2, "fe80::1/64");
	expect(1, 1, 0, "64");
	expect(1, 1, 1, "fe80::1/64");

	/* Addresses stand alone, not within words, and must be valid: these
	 * offer no address, only their blank-separated item. */
	reset(80, 5);
	put("std::vector deadbeefcafe::1 fe80::1zz 1:2:3:4:5:6:7:8:9 12:34:56");
	expect(2, 0, 1, "std:");
	expect(2, 0, 2, "std::vector");
	expect(12, 0, 1, "deadbeefcafe::1");
	expect(28, 0, 1, "1 fe80:");
	expect(28, 0, 2, "fe80::1zz");
	expect(38, 0, 2, "1:2:3:4:5:6:7:8:9");
	expect(56, 0, 2, "12:34:56");
	/* Seven groups are no MAC address, and too long a prefix no prefix. */
	reset(80, 5);
	put("aa:bb:cc:dd:ee:ff:00 fe80::1/1280");
	expect(0, 0, 1, "aa:");
	expect(0, 0, 2, "aa:bb:cc:dd:ee:ff:00");
	expect(24, 0, 1, "fe80::1");
	expect(24, 0, 2, "fe80::1/1280");
	expect(30, 0, 1, "/1280");

	/* Dragging expands by the same match size in either direction. */
	reset(80, 5);
	put("'alpha' 'beta' 'gamma'");
	selstart(2, 0, SNAP_REGEX);
	selextend(17, 0, SEL_REGULAR, 1);
	expectcurrent("alpha' 'beta' 'gamma");
	selstart(17, 0, SNAP_REGEX);
	selextend(2, 0, SEL_REGULAR, 1);
	expectcurrent("alpha' 'beta' 'gamma");
	selstart(2, 0, 0);
	selextend(4, 0, SEL_REGULAR, 0);
	selextend(4, 0, SEL_REGULAR, 1);
	expectcurrent("lph");

	/* Alternate-screen matches cannot reach into main-screen scrollback. */
	reset(8, 3);
	put("main contents wrap into history");
	tswapscreen();
	tclearregion(0, 0, term.col - 1, term.row - 1);
	tmoveto(0, 0);
	put("alt");
	expect(1, 0, 100, "alt");
	tswapscreen();

	/* Reflow and a full history ring retain valid selection coordinates. */
	reset(8, 3);
	for (i = 0; i < 100; i++)
		put("old\r\n");
	put(wrapped);
	expect(2, 1, 100, wrapped);
	put("\033[H\033[2J");
	tresize(10, 4);
	scroll.i = 2;
	kscrollup(&scroll);
	expect(2, 3, 100, wrapped);

	/* Selections follow their text into history and back into view. */
	{
		Arg two = { .i = 2 };

		reset(10, 3);
		put("aaa\r\nbbb\r\nccc");
		drag(0, 0, 2);
		put("\r\nddd\r\neee");
		assert(sel.nb.y == -2 && !selected(0, 0));
		expectcurrent("aaa");
		kscrollup(&two);
		assert(sel.nb.y == 0 && selected(0, 0));
		kscrolldown(&two);
		expectcurrent("aaa");
		/* Typing returns to the live screen without dropping it. */
		int fds[2];

		kscrollup(&two);
		assert(pipe(fds) == 0);
		cmdfd = fds[1];
		ttyinput("x", 1, 0);
		close(fds[0]);
		close(fds[1]);
		assert(sb.view_offset == 0 && sel.nb.y == -2);
		expectcurrent("aaa");
		/* Clearing the screen moves selected text to history. */
		drag(0, 2, 2);
		put("\033[H\033[2J");
		assert(sel.nb.y < 0);
		expectcurrent("eee");
		/* Text leaving the history ends its selection. */
		for (i = 0; i < 100; i++)
			put("\r\nmore");
		assert(sel.ob.x == -1);
	}

	/* Inserting or deleting characters ends a selection on the cells they
	 * move, which would select other text, but not one before them. */
	{
		static const char *const edits[] = {
			"\r\033[P",  /* DCH before the selection */
			"\r\033[@",  /* ICH before the selection */
			"\r\033[3C\033[P", /* DCH inside it */
			"\r\033[4h\033[2Cx\033[4l", /* IRM typing before it */
		};

		for (i = 0; i < (int)LEN(edits); i++) {
			reset(10, 3);
			put("ABCDE");
			drag(2, 0, 3);
			expectcurrent("CD");
			put(edits[i]);
			assert(sel.ob.x == -1 && !selected(2, 0));
		}
		/* Edits after the selection leave it on its text. */
		reset(10, 3);
		put("ABCDE");
		drag(1, 0, 2);
		put("\r\033[4C\033[P\033[@\033[4hx\033[4l");
		expectcurrent("BC");
	}

	/* Without history, scrolled-off rows are gone and others move up. */
	reset(10, 3);
	sb_setlimit(0);
	put("aaa\r\nbbb\r\nccc");
	drag(0, 1, 2);
	put("\r\n");
	assert(sel.nb.y == 0);
	expectcurrent("bbb");
	put("\r\n");
	assert(sel.ob.x == -1);
	sb_setlimit(scrollback_lines);

	/* Text evicted from a full history ends its selection in a fixed view. */
	{
		Arg far = { .i = 1000 }, one = { .i = 1 };

		reset(10, 3);
		for (i = 0; i < scrollback_lines + 10; i++)
			put("line\r\n");
		kscrollup(&far);
		drag(0, 0, 3);
		kscrolldown(&one);
		assert(sel.nb.y == -1 && sb.len == sb.limit);
		put("x\r\n");
		assert(sel.ob.x == -1);
	}

	/* Regional scrolling moves a selection while history is in view. */
	{
		Arg one = { .i = 1 };

		reset(10, 5);
		for (i = 0; i < 6; i++)
			put("old\r\n");
		put("\033[H\033[2Jtop\r\nr1\r\nr2\r\nr3\r\nstatus\033[2;4r");
		kscrollup(&one);
		drag(0, 4, 1); /* screen row 3: "r3" */
		expectcurrent("r3");
		put("\033[4;1H\n");
		assert(sel.nb.y == 3);
		expectcurrent("r3");
		put("\033[4;1H\n\n");
		assert(sel.ob.x == -1); /* scrolled out of the region */
	}

	/* Regional scrolling shifts snapped bounds without repeating regex work. */
	reset(30, 6);
	put("top\r\n'./alpha/beta.txt'\r\nmiddle\r\nbottom\r\ntail\r\nlast\033[2;5r");
	expect(12, 1, 1, "./alpha/beta.txt");
	regexcalls = 0;
	put("\033[2;1H\033[L");
	assert(sel.nb.y == 2 && sel.ne.y == 2 && regexcalls == 0);
	expectcurrent("./alpha/beta.txt");
	put("\033[2;1H\033[M");
	assert(sel.nb.y == 1 && sel.ne.y == 1 && regexcalls == 0);
	expectcurrent("./alpha/beta.txt");
	put("\033[2;1H\033[M");
	assert(sel.ob.x == -1 && regexcalls == 0);

	/* Scrolling inside a selection spanning both margins changes its text. */
	reset(30, 6);
	put("a\r\nb\r\nc\r\nd\r\ne\r\nf\033[2;5r");
	selstart(0, 0, 0);
	selextend(0, 5, SEL_REGULAR, 1);
	put("\033[2;1H\033[L");
	assert(sel.ob.x == -1);

	/* Selections keep their rows across screen switches. */
	reset(10, 3);
	put("main1\r\nmain2");
	drag(0, 0, 4);
	put("\033[?1049h");
	assert(sel.nb.y == -term.row && !selected(0, 0));
	expectcurrent("main1");
	put("alt\033[?1049l");
	assert(sel.nb.y == 0 && selected(0, 0));
	expectcurrent("main1");
	put("\033[?1049h\033[Halt");
	drag(0, 0, 2);
	put("\033[?1049l");
	assert(sel.ob.x == -1);

	/* Patterns only see SELCONTEXT cells around a click in a long line. */
	{
		char *line = xmalloc(5001 + 5);

		memset(line, 'x', 5000);
		strcpy(line + 5000, " tail");
		reset(80, 5);
		put(line);
		expect(term.c.x - 2, term.c.y, 0, "tail");
		/* A word reaching beyond the context is still selected whole. */
		line[5000] = '\0';
		expect(2, term.c.y, 0, line);
		/* Matches cut by the context grow to the whole logical line. */
		line[5000] = ' ';
		expect(2, term.c.y, 1, line);
		expect(2, term.c.y, 100, line);
		expect(term.c.x - 2, term.c.y, 1, line);
		free(line);
	}

	/* Coordinates outside the screen select from its nearest cell. */
	{
		int snaps[] = { 0, SNAP_WORD, SNAP_LINE, SNAP_REGEX };
		char *got;
		size_t i;

		reset(10, 3);
		put("0123456789abc");
		for (i = 0; i < LEN(snaps); i++) {
			selstart(10, 3, snaps[i]);
			assert(sel.ob.x == 9 && sel.ob.y == 2);
			selstart(-1, -1, snaps[i]);
			assert(sel.ob.x == 0 && sel.ob.y == 0);
			selextend(99, 99, SEL_REGULAR, 0);
			selextend(99, 99, SEL_REGULAR, 1);
			assert(sel.oe.x == 9 && sel.oe.y == 2);
			got = getsel();
			assert(got && !strncmp(got, "0123456789abc", 13));
			free(got);
		}
	}

	/* Releasing Alt before the button ends a block drag as lines, with
	 * the bounds of lines rather than of the block. */
	reset(80, 5);
	put("abcd\r\nefgh");
	selstart(2, 0, 0);
	selextend(1, 1, SEL_RECTANGULAR, 0);
	expectcurrent("bc\nfg");
	selextend(1, 1, SEL_REGULAR, 1);
	expectcurrent("cd\nef");
	/* And the other way round. */
	selstart(2, 0, 0);
	selextend(1, 1, SEL_REGULAR, 0);
	selextend(1, 1, SEL_RECTANGULAR, 1);
	expectcurrent("bc\nfg");

	puts("PASS: regex selection expansion, paths, assignments, phrases, UTF-8, wrapping, history, dragging, alternate screen, reflow, long lines, coordinates off the screen");
	return 0;
}
