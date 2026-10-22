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

	setlocale(LC_CTYPE, "");
	worddelimiters = L" \t\r\n`~!#$%^&*()=+[{]}\\|;:'\",<>?";
	reset(80, 5);
	put("name: './alpha/beta.txt' done");
	expect(17, 0, 0, "beta.txt");
	expect(17, 0, 1, path);
	expect(17, 0, 2, "name: './alpha/beta.txt' done");
	expect(17, 0, 100, "name: './alpha/beta.txt' done");

	/* The smallest word match includes filename and email punctuation. */
	reset(80, 5);
	put("<name_1-test@example.org> <a> <./aa/bb>");
	expect(7, 0, 0, "name_1-test@example.org");
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

	puts("PASS: regex selection expansion, paths, phrases, UTF-8, wrapping, history, dragging, alternate screen, reflow, long lines");
	return 0;
}
