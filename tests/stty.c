#include "terminal.h"

/* Run stty() with its standard output in out and return how much it wrote. */
static size_t
run(const char *cmd, char **args, char *out, size_t size)
{
	FILE *f = tmpfile();
	int saved = dup(1);
	size_t n;

	assert(f && saved >= 0);
	fflush(stdout);
	assert(dup2(fileno(f), 1) == 1);
	stty_args = (char *)cmd;
	stty(args);
	assert(dup2(saved, 1) == 1);
	close(saved);
	rewind(f);
	n = fread(out, 1, size - 1, f);
	out[n] = '\0';
	fclose(f);
	return n;
}

int
main(void)
{
	/* Past _POSIX_ARG_MAX, and the lengths that overflowed its old buffer. */
	static char big[8000], out[20000], want[20000];
	static const int lengths[] = { 4050, 4051, 4052, 4091, sizeof(big) - 1 };
	char *args[4] = { "a b", "$(touch pwned);'\"*", NULL, NULL };
	int i, status, null, saved;
	pid_t child;

	/* Words split at any run of blanks; arguments are not reparsed. */
	run(" printf  \t%s\\n ", args, out, sizeof(out));
	assert(!strcmp(out, "a b\n$(touch pwned);'\"*\n"));
	assert(access("pwned", F_OK) < 0);
	run("printf %s\\n", NULL, out, sizeof(out));
	assert(!strcmp(out, "\n"));

	for (i = 0; i < LEN(lengths); i++) {
		memset(big, 'x', sizeof(big));
		big[lengths[i]] = '\0';
		args[0] = big;
		args[1] = "b";
		args[2] = NULL;
		run("printf %s\\n", args, out, sizeof(out));
		snprintf(want, sizeof(want), "%s\nb\n", big);
		assert(!strcmp(out, want));
	}
	puts("PASS: stty arguments reach it unchanged and at any length");

	/* A command that fails or is missing is reported, not fatal. */
	null = open("/dev/null", O_WRONLY);
	assert(null >= 0);
	fflush(stderr);
	saved = dup(2);
	assert(dup2(null, 2) == 2);
	run("false", NULL, out, sizeof(out));
	run("/nonexistent/stty", NULL, out, sizeof(out));
	/* Without a command there is nothing to run. */
	assert((child = fork()) >= 0);
	if (!child) {
		stty_args = " ";
		stty(NULL);
		_exit(0);
	}
	assert(dup2(saved, 2) == 2);
	close(saved);
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
	close(null);
	puts("PASS: stty failures are reported; an empty command is refused");

	/* A launcher ignoring SIGCHLD doesn't make a stty that worked look failed. */
	{
		int fds[2];
		char said[256];

		assert(pipe(fds) == 0);
		assert((child = fork()) >= 0);
		if (!child) {
			dup2(fds[1], 2);
			close(fds[0]);
			close(fds[1]);
			signal(SIGCHLD, SIG_IGN);
			stty_args = "true";
			ttynew("/dev/null", NULL, NULL, NULL, NULL);
			_exit(0);
		}
		close(fds[1]);
		assert(read(fds[0], said, sizeof(said)) == 0);
		close(fds[0]);
		assert(waitpid(child, &status, 0) == child && status == 0);
	}
	puts("PASS: an inherited ignored SIGCHLD doesn't fail stty");
	return 0;
}
