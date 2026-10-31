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

/* A -o pipe losing its reader ends the printing only, and the child still
 * dies of SIGPIPE like any other program: "yes" would complain otherwise. */
static void
testclosedpipe(void)
{
	char out[64], text[512], *args[] = { "/bin/sh", "-c",
		"sleep 0.1; yes | head -n 1 >/dev/null; echo finished", NULL };
	int fds[2], fd, status;
	size_t len = 0;
	ssize_t n;
	pid_t child;

	child = fork();
	assert(child >= 0);
	if (!child) {
		termname = "dumb";
		assert(pipe(fds) == 0);
		close(fds[0]);
		snprintf(out, sizeof(out), "/proc/self/fd/%d", fds[1]);
		fd = ttynew(NULL, "/bin/sh", out, args, NULL);
		signal(SIGCHLD, SIG_DFL);
		assert(iofd >= 0);
		close(fds[1]);
		tprinter("x", 1);
		/* The printing stopped here, and only that. */
		assert(iofd == -1);
		while (len < sizeof(text) - 1 &&
		       (n = read(fd, text + len, sizeof(text) - 1 - len)) > 0)
			len += n;
		text[len] = '\0';
		assert(strstr(text, "finished") && !strstr(text, "yes:"));
		_exit(7);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 7);
}

/* Without -o too, as stderr can be a pipe whose reader goes away. */
static void
testsigpipeignored(void)
{
	struct sigaction sa;
	int status;
	pid_t child;

	assert((child = fork()) >= 0);
	if (!child) {
		stty_args = "true";
		ttynew("/dev/null", NULL, NULL, NULL, NULL);
		sigaction(SIGPIPE, NULL, &sa);
		_exit(sa.sa_handler != SIG_IGN);
	}
	assert(waitpid(child, &status, 0) == child && status == 0);
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
	testsigpipeignored();
	testclosedpipe();
	reset(20, 3);
	put("\344\270\255\r\n\r\nx");
	dump_is("\344\270\255\n\nx\n", NULL);
	puts("PASS: screen dumps use displayed history, blank rows and wide UTF-8 glyphs; Media Copy needs -o");
	return 0;
}
