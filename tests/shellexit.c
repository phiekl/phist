#include "terminal.h"
#include <poll.h>

enum {
	NORMAL,
	SIGNALLOST,    /* the shell exits with nothing to catch its signal */
	LAUNCHERBLOCK, /* phist is started with SIGCHLD blocked */
};

/* Run cmd as phist's shell and return phist's status, with what it said. */
static int
runwith(int mode, const char *cmd, char *said, size_t size)
{
	sigset_t set;
	char *args[] = { "/bin/sh", "-c", (char *)cmd, NULL };
	int fds[2], status, fd;
	struct pollfd p = { .events = POLLIN };
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
		sigprocmask(SIG_SETMASK, &set, NULL);
		fd = ttynew(NULL, "/bin/sh", NULL, args, NULL);
		if (mode == SIGNALLOST)
			signal(SIGCHLD, SIG_DFL);
		p.fd = fd;
		for (;;) {
			poll(&p, 1, -1);
			ttyread();
		}
	}
	close(fds[1]);
	while (len < size - 1 && (n = read(fds[0], said + len, size - 1 - len)) > 0)
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

int
main(void)
{
	char said[256];
	int i;

	setlocale(LC_CTYPE, "");
	reset(20, 4);
	for (i = 0; i < 100; i++) {
		assert(run("exit 0", said, sizeof(said)) == 0 && !*said);
		assert(run("exit 3", said, sizeof(said)) == 1 && !*said);
		assert(run("echo text", said, sizeof(said)) == 0 && !*said);
	}
	assert(run("kill -9 $$", said, sizeof(said)) == 1 && !*said);
	for (i = 0; i < 20; i++) {
		assert(runwith(SIGNALLOST, "exit 0", said, sizeof(said)) == 0 && !*said);
		assert(runwith(SIGNALLOST, "exit 3", said, sizeof(said)) == 1 && !*said);
		assert(runwith(LAUNCHERBLOCK, "exit 0", said, sizeof(said)) == 0 && !*said);
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
