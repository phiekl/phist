#include "terminal.h"

static void
dump_is(const char *expected, const char *request)
{
	FILE *file = tmpfile();
	char text[256] = {0};
	size_t len;

	assert(file);
	iofd = fileno(file);
	if (request)
		put(request);
	else
		tdump();
	rewind(file);
	len = fread(text, 1, sizeof(text) - 1, file);
	assert(len == strlen(expected) && !memcmp(text, expected, len));
	fclose(file);
	iofd = -1;
}

int
main(void)
{
	Arg step = { .i = 1 };

	setlocale(LC_CTYPE, "");
	reset(20, 4);
	put("historylong\r\na\r\nb\r\nc\r\nd");
	dump_is("a\nb\nc\nd\n", NULL);
	kscrollup(&step);
	dump_is("historylong\na\nb\nc\n", NULL);
	/* Without -o, applications cannot print to phist's stdout. */
	dump_is("", "\033[1i\033[0i\033[5ihidden\033[4i");
	assert(!IS_SET(MODE_PRINT));
	mcallowed = 1;
	dump_is("dhidden\n", "\033[1i");
	dump_is("x\033[4i", "\033[5ix\033[4i");
	mcallowed = 0;
	reset(20, 3);
	put("\344\270\255\r\n\r\nx");
	dump_is("\344\270\255\n\nx\n", NULL);
	puts("PASS: print-screen uses displayed history, blank rows and wide UTF-8 glyphs; Media Copy needs -o");
	return 0;
}
