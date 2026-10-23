/* Headless parser/history benchmark: includes URL scanning, excludes X rendering. */
#define TEST_DRAWING
#include "terminal.h"
#include <sys/resource.h>
#include <time.h>

static volatile unsigned long checksum;

void
xdrawline(Line line, int start, int y, int end)
{
	unsigned long sum = 0;
	int x;

	for (x = start; x < end; x++)
		sum += line[x].u + line[x].mode + line[x].fg + line[x].bg + line[x].ucolor;
	checksum += sum;
}

int xstartdraw(void) { return 1; }

static double
seconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
	return now.tv_sec + now.tv_nsec / 1e9;
}

int
main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "plain";
	char text[4096];
	int x, i, length = 0;
	double start, load, scroll, resize;
	struct rusage usage;

	assert(!strcmp(mode, "plain") || !strcmp(mode, "colored") || !strcmp(mode, "varied"));
	setlocale(LC_CTYPE, "C.UTF-8");
	scrollback_lines = 65536;
	reset(160, 40);
	for (x = 0; x < 150; x++) {
		if (!strcmp(mode, "varied") || (!strcmp(mode, "colored") && x % 25 == 0))
			length += snprintf(text + length, sizeof(text) - length,
				"\033[38;2;%d;%d;%dm", x, 255 - x, x);
		text[length++] = x % 17 == 0 ? ' ' : 'a' + x % 26;
	}
	text[length++] = '\r';
	text[length++] = '\n';
	text[length] = '\0';
	start = seconds();
	for (i = 0; i < 3 * 65536 + 40; i++)
		put(text);
	load = seconds() - start;
	assert(sb.len == 65536);
	getrusage(RUSAGE_SELF, &usage);
	printf("%s: load %.4fs, peak RSS after load %ld KiB; ", mode, load, usage.ru_maxrss);
	fflush(stdout);

	sb.view_offset = sb.len;
	start = seconds();
	for (i = 0; i < 10000; i++)
		kscrolldown(&(Arg){ .i = 1 });
	scroll = seconds() - start;
	start = seconds();
	tresize(100, 40);
	tresize(160, 40);
	resize = seconds() - start;
	getrusage(RUSAGE_SELF, &usage);
	printf("10k scrolls %.4fs, resize pair %.4fs, overall peak RSS %ld KiB, checksum %lu\n",
	       scroll, resize, usage.ru_maxrss, checksum);
	return 0;
}
