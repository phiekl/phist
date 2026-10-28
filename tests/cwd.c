#include <assert.h>
#include <fcntl.h>
#include <locale.h>
#include <sys/socket.h>
#define VERSION "test"
#define main st_main
#include "../x.c"
#undef main

void testmouse_init(int, int, int);
void testmouse_put(const char *);

static int updates, deletes;
static char published[20000];

int
XChangeProperty(Display *d, Window w, Atom property, Atom type, int format,
                int mode, const unsigned char *data, int count)
{
	assert(w == xw.win && property == xw.cwduri && type == xw.utf8string);
	assert(format == 8 && mode == PropModeReplace);
	assert(count > 0 && (size_t)count < sizeof(published));
	memcpy(published, data, count);
	published[count] = '\0';
	updates++;
	return Success;
}

int
XDeleteProperty(Display *d, Window w, Atom property)
{
	assert(w == xw.win && property == xw.cwduri);
	published[0] = '\0';
	deletes++;
	return Success;
}

static void
report(const char *uri)
{
	testmouse_put("\033]7;");
	testmouse_put(uri);
	testmouse_put("\033\\");
}

int
main(void)
{
	static const char *valid[] = {
		"file:///", "file://localhost/tmp", "file://remote.example/home/user",
		"file://host/a;b;c;d;e;f;g;h;i;j;k;l;m;n;o;p;q;r;s;t",
		"file://[2001:db8::1]/tmp", "FILE://host/a%20b/%E7%95%8C/%23%3F",
		"file://my%2Dhost/tmp",
		"file://host/a:@!$&'()*+,;=-._~b",
	};
	static const char *invalid[] = {
		"/tmp", "https://host/tmp", "file:/tmp", "file://host",
		"file://user@host/tmp", "file://host:22/tmp", "file://[nope]/tmp",
		"file://[::1/tmp", "file://[]/tmp", "file://host/a b",
		"file://host/a?query", "file://host/a#fragment", "file://host/a\\b",
		"file://host/%", "file://host/%2", "file://host/%GG",
		"file://host/%00", "file://host/%1b", "file://host/%0A",
		"file://host/%7f", "file://host/a\nb", "file://host/\u00e9",
	};
	char before[4096], after[4096], large[16386], reply;
	size_t i;
	int fds[2], count;

	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	assert(fcntl(fds[1], F_SETFL, O_NONBLOCK) == 0);
	testmouse_init(fds[0], 20, 4);
	xw.win = 42;
	xw.cwduri = 43;
	xw.utf8string = 44;
	assert(getcwd(before, sizeof(before)));
	xflushcwd();
	assert(!updates && !deletes); /* absent before the first report */

	/* Both terminators work, including a sequence split across reads. */
	testmouse_put("\033]7;file://host/");
	xflushcwd();
	assert(!updates);
	testmouse_put("project\a");
	assert(!updates);
	xflushcwd();
	assert(updates == 1 && !strcmp(published, "file://host/project"));
	for (i = 0; i < LEN(valid); i++) {
		report(valid[i]);
		xflushcwd();
		assert(!strcmp(published, valid[i]));
	}
	count = updates;
	for (i = 0; i < LEN(invalid); i++) {
		report(invalid[i]);
		xflushcwd();
		assert(updates == count && !deletes);
		assert(!strcmp(published, valid[LEN(valid) - 1]));
	}

	/* An exact-limit URI is preserved; an oversized one is ignored. */
	memcpy(large, "file:///", 8);
	memset(large + 8, 'x', 16384 - 8);
	large[16384] = '\0';
	report(large);
	xflushcwd();
	assert(strlen(published) == 16384 && !strcmp(published, large));
	count = updates;
	large[16384] = 'x';
	large[16385] = '\0';
	report(large);
	xflushcwd();
	assert(updates == count);

	/* Reports coalesce and flush while hidden, and duplicates cause no
	 * extra property notifications. Clearing removes the property. */
	win.mode = 0;
	for (i = 0; i < 1000; i++)
		report(i % 2 ? "file://host/last" : "file://host/first");
	assert(updates == count);
	xflushcwd();
	assert(updates == count + 1 && !strcmp(published, "file://host/last"));
	report("file://host/last");
	xflushcwd();
	assert(updates == count + 1);
	report("");
	assert(!deletes);
	xflushcwd();
	assert(deletes == 1 && !published[0]);
	report("");
	xflushcwd();
	assert(deletes == 1);

	assert(getcwd(after, sizeof(after)) && !strcmp(before, after));
	assert(read(fds[1], &reply, 1) == -1 && errno == EAGAIN);
	assert(!pendingtitle && !pendingicontitle);
	close(fds[0]);
	close(fds[1]);
	puts("PASS: OSC 7 URI validation, preserved hosts/encoding, X11 metadata, batching, deduplication and clearing");
	return 0;
}
