#include "terminal.h"
#include <poll.h>
#include <sys/syscall.h>
#include <time.h>

enum {
	NORMAL,
	LAUNCHERBLOCK, /* phist is started with SIGCHLD blocked */
	KEYATEXIT,     /* input is pending when the shell's end has closed */
	MAINSTUCK,     /* the main loop never gets to finish up */
	STUCKBLOCKED,  /* the same, with phist started with SIGALRM blocked */
};

static const char *output;
static int stalled, failttywrites;

/* Linux takes writes after the shell's end closed, so have it refuse them. */
long syscall(long, ...);

ssize_t
write(int fd, const void *s, size_t n)
{
	if (failttywrites && fd == cmdfd) {
		errno = EIO;
		return -1;
	}
	return syscall(SYS_write, fd, s, n);
}

/* Run cmd as phist's shell and return phist's status, with what it said. */
static int
runwith(int mode, const char *cmd, char *said, size_t size)
{
	sigset_t set;
	char *args[] = { "/bin/sh", "-c", (char *)cmd, NULL };
	int fds[2], status;
	struct pollfd p[2] = { { .events = POLLIN }, { .events = POLLIN } };
	ssize_t n;
	size_t len = 0;
	pid_t child;

	assert(pipe(fds) == 0);
	child = fork();
	assert(child >= 0);
	if (!child) {
		dup2(fds[1], STDERR_FILENO);
		close(fds[0]);
		close(fds[1]);
		alarm(20);
		termname = "dumb";
		sigemptyset(&set);
		if (mode == LAUNCHERBLOCK)
			sigaddset(&set, SIGCHLD);
		if (mode == STUCKBLOCKED)
			sigaddset(&set, SIGALRM);
		sigprocmask(SIG_SETMASK, &set, NULL);
		p[0].fd = ttynew(NULL, "/bin/sh", output, args, NULL);
		p[1].fd = ttywakefd();
		while (mode == MAINSTUCK || mode == STUCKBLOCKED)
			sleep(60);
		/* What the main loop does, with parsing the only slow part. */
		for (;;) {
			ttycheckshell();
			if (poll(p, 2, -1) <= 0)
				continue;
			if (mode == KEYATEXIT && (p[0].revents & POLLHUP)) {
				failttywrites = 1;
				ttywrite("x", 1, 0);
			}
			if (p[0].revents & (POLLIN | POLLHUP))
				ttyread();
		}
	}
	close(fds[1]);
	/* A stalled writer keeps stderr open, so don't wait for that to close. */
	while (!stalled && len < size - 1 &&
	       (n = read(fds[0], said + len, size - 1 - len)) > 0)
		len += n;
	said[len] = '\0';
	close(fds[0]);
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status));
	return WEXITSTATUS(status);
}

static int
run(const char *cmd, char *said, size_t size)
{
	return runwith(NORMAL, cmd, said, size);
}

static double
elapsed(const struct timespec *start)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - start->tv_sec) + (now.tv_nsec - start->tv_nsec) / 1e9;
}

static size_t
countlines(const char *path)
{
	FILE *f = fopen(path, "r");
	size_t n = 0;
	int c;

	assert(f);
	while ((c = getc(f)) != EOF)
		n += c == '\n';
	fclose(f);
	return n;
}

/* The shell's last output reaches -o, and nothing keeps phist long after it. */
static void
testlastoutput(void)
{
	char dir[32], path[64], fifo[64], said[256], block[4096];
	struct timespec start;
	int i, j, fd, fds[2];
	size_t lines;
	ssize_t n;

	snprintf(dir, sizeof(dir), "/tmp/phist-exit%d", (int)getpid());
	assert(mkdir(dir, 0700) == 0);
	snprintf(path, sizeof(path), "%s/out", dir);
	snprintf(fifo, sizeof(fifo), "%s/fifo", dir);
	assert(mkfifo(fifo, 0600) == 0);

	for (i = 0; i < 10; i++) {
		unlink(path);
		output = path;
		assert(run("seq 1 3000", said, sizeof(said)) == 0 && !*said);
		assert(countlines(path) == 3000);

		/* Through the writer: it all fits in the FIFO, read afterwards. */
		fd = open(fifo, O_RDONLY | O_NONBLOCK);
		assert(fd >= 0);
		output = fifo;
		assert(run("seq 1 3000", said, sizeof(said)) == 0 && !*said);
		fcntl(fd, F_SETFL, 0);
		lines = 0;
		while ((n = read(fd, block, sizeof(block))) > 0)
			for (j = 0; j < n; j++)
				lines += block[j] == '\n';
		assert(n == 0 && lines == 3000);
		close(fd);
	}

	/* A query among the last output can't be answered, which is no error. */
	for (i = 0; i < 20; i++) {
		unlink(path);
		output = path;
		assert(run("seq 1 3000; printf '\\033[5n'; exit 3", said, sizeof(said)) == 1 && !*said);
		assert(countlines(path) == 3000);
	}

	/* A job that outlives the shell and keeps writing is cut off. */
	output = path;
	clock_gettime(CLOCK_MONOTONIC, &start);
	assert(run("(trap '' HUP; while echo x; do :; done) & sleep 0.1; exit 0",
	           said, sizeof(said)) == 0 && !*said);
	assert(elapsed(&start) < 4);

	/* A silent one leaves the pty open, so only the signal tells. */
	output = NULL;
	clock_gettime(CLOCK_MONOTONIC, &start);
	assert(run("(trap '' HUP; sleep 3) & exit 3", said, sizeof(said)) == 1 && !*said);
	assert(elapsed(&start) < 2.5);

	/* A reader that takes nothing can't hold phist up for long either. */
	assert(pipe(fds) == 0);
	snprintf(path, sizeof(path), "/proc/%d/fd/%d", (int)getpid(), fds[1]);
	output = path;
	stalled = 1;
	clock_gettime(CLOCK_MONOTONIC, &start);
	assert(run("seq 1 100000; exit 0", said, sizeof(said)) == 0);
	stalled = 0;
	assert(elapsed(&start) < 5);
	close(fds[0]);
	close(fds[1]);
	output = NULL;

	snprintf(path, sizeof(path), "%s/out", dir);
	unlink(path);
	unlink(fifo);
	rmdir(dir);
	puts("PASS: a shell's last output reaches -o, without holding phist up");
}

int
main(void)
{
	char said[256];
	int i;

	setlocale(LC_CTYPE, "");
	reset(20, 4);
	alarm(120);
	testlastoutput();
	for (i = 0; i < 100; i++) {
		assert(run("exit 0", said, sizeof(said)) == 0 && !*said);
		assert(run("exit 3", said, sizeof(said)) == 1 && !*said);
		assert(run("echo text", said, sizeof(said)) == 0 && !*said);
	}
	assert(run("kill -9 $$", said, sizeof(said)) == 1 && !*said);
	for (i = 0; i < 20; i++) {
		assert(runwith(KEYATEXIT, "exit 0", said, sizeof(said)) == 0 && !*said);
		assert(runwith(KEYATEXIT, "exit 3", said, sizeof(said)) == 1 && !*said);
		assert(run("printf '\\033[5n'", said, sizeof(said)) == 0 && !*said);
		assert(runwith(LAUNCHERBLOCK, "exit 0", said, sizeof(said)) == 0 && !*said);
	}
	/* A main loop that can't finish up doesn't keep phist after its shell. */
	{
		struct timespec start;

		clock_gettime(CLOCK_MONOTONIC, &start);
		assert(runwith(MAINSTUCK, "exit 0", said, sizeof(said)) == 0 && !*said);
		assert(runwith(MAINSTUCK, "exit 3", said, sizeof(said)) == 1 && !*said);
		assert(runwith(STUCKBLOCKED, "exit 3", said, sizeof(said)) == 1 && !*said);
		assert(elapsed(&start) < 13);
	}
	/* What the shell runs doesn't start with SIGCHLD held back. Not the shell's
	 * own /proc entry: it can block everything for a moment around a fork. */
	assert(run("grep -q '^SigBlk:[[:space:]]*0*$' /proc/self/status",
	           said, sizeof(said)) == 0 && !*said);
	/* Closing the terminal isn't exiting. */
	assert(run("exec <&- >&- 2>&-; sleep 5", said, sizeof(said)) == 1);
	assert(strstr(said, "couldn't read from shell: Input/output error"));
	puts("PASS: phist exits with its shell's status and without an error");
	return 0;
}
