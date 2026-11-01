#include "terminal.h"
#include <sys/resource.h>
#include <sys/socket.h>

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
		/* The writer fails first, and then the printing stops, and only that. */
		alarm(20);
		while (iofd != -1) {
			tprinter("x", 1);
			ttyflushprint();
			usleep(1000);
		}
		alarm(0);
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

/* A reader that stays but takes nothing holds output back, not the terminal.
 * alarm() ends the test if anything blocks. */
static void
testslowreader(void)
{
	static char block[64 * 1024], got[256 * 1024];
	int fds[2], i, saved = iofd;
	size_t total = 0, want = 256 * 1024;
	ssize_t n;

	assert(pipe(fds) == 0);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	iofd = fds[1];
	alarm(20);
	printwriter();
	memset(block, 'x', sizeof(block));
	for (i = 0; i < 4; i++) {
		tprinter(block, sizeof(block));
		ttyflushprint();
	}
	/* The pipes hold 128 KiB at most, and the rest waits, without blocking. */
	assert(iofd != -1 && ttyprintfd() == iofd);
	/* The reader takes it all, in order, once it reads again. */
	while (total < want) {
		n = read(fds[0], got + total, want - total);
		if (n < 0) {
			assert(errno == EAGAIN);
			usleep(1000);
		} else {
			assert(n > 0);
			total += n;
		}
		ttyflushprint();
	}
	assert(ttyprintfd() == -1 && !memchr(got, 0, want));
	printstop();
	close(fds[0]);
	alarm(0);
	iofd = saved;
}

/* Output nobody takes never blocks, even with another writer filling the pipe
 * after any check for room, and the output's own flags stay as they were. A full
 * queue ends printing, and the reader then sees the end, so the writer is gone. */
static void
teststalled(int rd, int wr, int fill)
{
	static char block[64 * 1024], text[4096];
	int i, other = -1, shared, saved = iofd;
	char path[64];
	ssize_t n;

	alarm(20);
	shared = dup(wr);
	if (fill) {
		snprintf(path, sizeof(path), "/proc/self/fd/%d", wr);
		other = open(path, O_WRONLY | O_NONBLOCK);
		assert(other >= 0);
		while (write(other, block, sizeof(block)) > 0 || write(other, block, 1) > 0)
			;
		assert(errno == EAGAIN);
	}
	iofd = wr;
	printwriter();
	assert(iofd != -1 && iofd != wr && (fcntl(iofd, F_GETFL) & O_NONBLOCK));
	for (i = 0; iofd != -1 && i < 2 * PRINT_MAX / (int)sizeof(block); i++) {
		tprinter(block, sizeof(block));
		ttyflushprint();
	}
	assert(iofd == -1 && ttyprintfd() == -1);
	tprinter("x", 1);
	assert(!(fcntl(shared, F_GETFL) & O_NONBLOCK));
	close(shared);
	if (other >= 0)
		close(other);
	while ((n = read(rd, text, sizeof(text))) > 0)
		;
	assert(n == 0);
	close(rd);
	alarm(0);
	iofd = saved;
}

/* -o - leaves stdout's flags alone, and the reader's end of file doesn't wait
 * for phist to exit. */
static void
testsharedstdout(void)
{
	char text[16];
	int fds[2], status;
	size_t len = 0;
	ssize_t n;
	pid_t child;

	child = fork();
	assert(child >= 0);
	if (!child) {
		alarm(20);
		assert(pipe(fds) == 0);
		assert(dup2(fds[1], STDOUT_FILENO) == STDOUT_FILENO);
		close(fds[1]);
		iofd = STDOUT_FILENO;
		printwriter();
		assert(iofd != STDOUT_FILENO);
		assert(!(fcntl(STDOUT_FILENO, F_GETFL) & O_NONBLOCK));
		tprinter("ok", 2);
		ttyflushprint();
		printstop();
		while ((n = read(fds[0], text + len, sizeof(text) - len)) > 0)
			len += n;
		assert(n == 0 && len == 2 && !memcmp(text, "ok", 2));
		_exit(7);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 7);
}

/* The writer keeps nothing it inherited, whatever its number and however low the
 * limits went after opening it: this one would otherwise hold a pipe open. A
 * system that can't tell what is open gets no writer, and so no printing. */
static void
testinheritedhighfd(int listed)
{
	enum { HIGH = 70000 };
	struct rlimit limit;
	char text[16];
	int marker[2], out[2], status;
	pid_t child;

	assert(!getrlimit(RLIMIT_NOFILE, &limit));
	if (limit.rlim_max != RLIM_INFINITY && limit.rlim_max <= HIGH) {
		puts("SKIP: descriptor limit too low for the high descriptor test");
		return;
	}
	child = fork();
	assert(child >= 0);
	if (!child) {
		alarm(5);
		signal(SIGPIPE, SIG_IGN);
		assert(pipe(marker) == 0);
		limit.rlim_cur = HIGH + 1;
		assert(!setrlimit(RLIMIT_NOFILE, &limit));
		assert(dup2(marker[1], HIGH) == HIGH);
		close(marker[1]);
		limit.rlim_cur = limit.rlim_max = 1024;
		assert(!setrlimit(RLIMIT_NOFILE, &limit));
		assert(pipe(out) == 0);
		iofd = out[1];
		if (!listed)
			printfdlist = "/nonexistent";
		printwriter();
		close(HIGH);
		/* With or without the writer still running, this is the end of file. */
		assert(read(marker[0], text, sizeof(text)) == 0);
		while (!listed && iofd != -1) {
			tprinter("x", 1);
			ttyflushprint();
			usleep(1000);
		}
		assert(!listed || iofd != -1);
		printstop();
		assert(read(out[0], text, sizeof(text)) == 0);
		_exit(7);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 7);
}

/* The writer's own fork failing, in a child, leaves no errno here to report. */
pid_t __fork(void);
static pid_t forkparent;

pid_t
fork(void)
{
	if (forkparent && getpid() != forkparent) {
		errno = EAGAIN;
		return -1;
	}
	return __fork();
}

static void
testwriterforkfails(void)
{
	int out[2], err[2], status;
	char said[256] = {0};
	ssize_t n;
	pid_t child;

	assert(pipe(out) == 0 && pipe(err) == 0);
	assert((child = fork()) >= 0);
	if (!child) {
		dup2(out[1], STDOUT_FILENO);
		dup2(err[1], STDERR_FILENO);
		close(out[0]);
		close(out[1]);
		close(err[0]);
		close(err[1]);
		stty_args = "true";
		forkparent = getpid();
		errno = ENOTTY;
		ttynew("/dev/null", NULL, "-", NULL, NULL);
		_exit(iofd != -1);
	}
	close(out[1]);
	close(err[1]);
	n = read(err[0], said, sizeof(said) - 1);
	assert(n > 0 && !strcmp(said, "Error starting output writer\n"));
	assert(waitpid(child, &status, 0) == child && status == 0);
	close(out[0]);
	close(err[0]);
}

/* A launcher ignoring SIGCHLD doesn't make a writer that started look failed. */
static void
testignoredsigchld(void)
{
	int out[2], err[2], status;
	char said[256];
	pid_t child;

	assert(pipe(out) == 0 && pipe(err) == 0);
	assert((child = fork()) >= 0);
	if (!child) {
		dup2(out[1], STDOUT_FILENO);
		dup2(err[1], STDERR_FILENO);
		close(out[0]);
		close(out[1]);
		close(err[0]);
		close(err[1]);
		signal(SIGCHLD, SIG_IGN);
		stty_args = "true";
		ttynew("/dev/null", NULL, "-", NULL, NULL);
		_exit(iofd == -1);
	}
	close(out[1]);
	close(err[1]);
	assert(read(err[0], said, sizeof(said)) == 0);
	assert(waitpid(child, &status, 0) == child && status == 0);
	close(out[0]);
	close(err[0]);
}

int
main(void)
{
	Arg step = { .i = 1 };
	int pair[2];

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
	testslowreader();
	assert(pipe(pair) == 0);
	teststalled(pair[0], pair[1], 1);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
	teststalled(pair[0], pair[1], 0);
	testsharedstdout();
	testignoredsigchld();
	testwriterforkfails();
	testinheritedhighfd(1);
	testinheritedhighfd(0);
	reset(20, 3);
	put("\344\270\255\r\n\r\nx");
	dump_is("\344\270\255\n\nx\n", NULL);
	puts("PASS: screen dumps use displayed history, blank rows and wide UTF-8 glyphs; Media Copy needs -o");
	return 0;
}
