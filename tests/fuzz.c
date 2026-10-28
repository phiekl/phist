/*
 * Headless terminal-core fuzzer using the real parser through terminal.h.
 * Reproduce a run with: fuzz SEED [EPISODES [OPS [SECONDS]]]
 * SECONDS bounds the whole seed, including all episodes.
 *
 * Exercises malformed escapes and UTF-8, resizes, history, selections and URLs.
 * Check state bounds after each operation and wide/dummy pairs after each
 * episode. Pairing mismatches remain warnings because erase and insert
 * operations can split wide cells.
 *
 * SIGALRM reports the current step and a stack trace before _exit().
 * Sanitizer findings, assertion failures and hangs fail the run.
 */
/* Keep the invariant checks enabled even with release CFLAGS. */
#undef NDEBUG
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__GLIBC__)
#include <execinfo.h>
#endif

#include "terminal.h"

static uint64_t rngstate;

static uint64_t
rnd(void)
{
	rngstate ^= rngstate << 13;
	rngstate ^= rngstate >> 7;
	rngstate ^= rngstate << 17;
	return rngstate;
}

static uint32_t
rndmod(uint32_t n)
{
	return n ? rnd() % n : 0;
}

/* ---- payload generators: biased towards parser edge cases ---- */

static void
gen_utf8(char *out, size_t *len)
{
	/* A random code point as (possibly broken) UTF-8. */
	static const uint32_t pool[] = {
		0x41, 0x7E, 0xA0, 0xFF, 0x100, 0x300, 0x301, 0x35C, /* combining */
		0x200B, 0x200D, 0xFE0E, 0xFE0F, 0x1100, 0x1161, 0x11A8, /* jamo */
		0xAC00, 0xAC01, 0xD7A3, 0x2603, 0x1F600, 0x1F9E0,   /* emoji */
		0x4E00, 0x9FFF, 0xFF21, 0x10FFFF,                  /* wide, boundary */
		0xD800, 0xDFFF, 0xFDD0, 0xFFFF, 0x1F1E6,
	};
	static const char *frag[] = {
		"\xC0", "\xC0\x80", "\xE0\x80", "\xED\xA0\x80",
		"\xF0\x9F", "\xF4\x90\x80\x80", "\xF8\x88\x80\x80",
		"\xC2", "\xE2\x98", "\x80\x80", "\xFE", "\xFF",
	};
	uint32_t u = pool[rndmod(LEN(pool))];
	size_t n;

	if (rnd() & 1) {
		const char *s = frag[rndmod(LEN(frag))];

		/* Raw fragments: truncated, overlong and stray bytes. */
		n = 1 + rndmod(strlen(s));
		memcpy(out, s, n);
	} else {
		char tmp[8];
		size_t k = 0;

		if (u < 0x80) tmp[k++] = u;
		else if (u < 0x800) { tmp[k++] = 0xC0 | (u >> 6); tmp[k++] = 0x80 | (u & 0x3F); }
		else if (u < 0x10000) { tmp[k++] = 0xE0 | (u >> 12); tmp[k++] = 0x80 | ((u >> 6) & 0x3F); tmp[k++] = 0x80 | (u & 0x3F); }
		else if (u < 0x200000) { tmp[k++] = 0xF0 | (u >> 18); tmp[k++] = 0x80 | ((u >> 12) & 0x3F); tmp[k++] = 0x80 | ((u >> 6) & 0x3F); tmp[k++] = 0x80 | (u & 0x3F); }
		else tmp[k++] = '?';
		n = k;
		if (rnd() % 8 == 0 && n > 1)
			n--; /* writepayload() carries unfinished UTF-8 across calls. */
		memcpy(out, tmp, n);
	}
	*len = n;
}

static void
num(char **p, int digits)
{
	/* A random CSI parameter: empty, huge, negative or a colon chain. */
	switch (rnd() % 10) {
	case 0:
		*(*p)++ = ';';
		return;
	case 1:
		return;
	case 2:
		*(*p)++ = '0' + rndmod(10);
		return;
	case 3: {
		static const char *big[] = {
			"2147483647", "2147483648", "9999999999", "-1",
			"-2147483648", "00000000000001", "65536", "65535",
			"4294967295",
		};
		const char *s = big[rndmod(LEN(big))];

		while (*s)
			*(*p)++ = *s++;
		return;
	}
	case 4: {
		int j, cnt = 1 + rndmod(6);

		for (j = 0; j < cnt; j++) {
			if (j)
				*(*p)++ = ':';
			if (rnd() % 4)
				*(*p)++ = '0' + rndmod(10);
		}
		return;
	}
	default: {
		int j, cnt = digits ? 1 + rndmod(digits) : 1;

		for (j = 0; j < cnt; j++)
			*(*p)++ = '0' + rndmod(10);
		return;
	}
	}
}

static void
gen_csi(char *out, size_t *len)
{
	static const char finals[] = "@ABCDEFGHIJKLMNPSTUXZ`abcdefghilmnprstux";
	static const char intermed[] = " !\"$#%'";
	char *p = out;
	int nargs, i;

	*p++ = '\033';
	*p++ = '[';
	if (rnd() % 8 == 0)
		*p++ = '?';
	else if (rnd() % 16 == 0)
		*p++ = '>';
	else if (rnd() % 16 == 0)
		*p++ = '<';
	else if (rnd() % 16 == 0)
		*p++ = '=';
	nargs = rndmod(5);
	for (i = 0; i < nargs; i++) {
		num(&p, 9);
		if (i + 1 < nargs || rnd() % 3 == 0)
			*p++ = ';';
	}
	if (rnd() % 10 == 0)
		*p++ = intermed[rndmod(sizeof(intermed) - 1)];
	*p++ = finals[rndmod(sizeof(finals) - 1)];
	*len = p - out;
}

static void
gen_osc(char *out, size_t *len)
{
	static const int codes[] = {0, 1, 2, 4, 7, 8, 10, 11, 12, 52, 104, 110, 111, 112};
	static const char b64[] = "ABCabc012+/\n\r\t\x01\xff=";
	char *p = out;
	int code = codes[rndmod(LEN(codes))], i;

	*p++ = '\033';
	*p++ = ']';
	p += sprintf(p, "%d;", code);
	switch (code) {
	case 8:
		p += sprintf(p, "id=%u;", (unsigned)rndmod(8));
		if (rnd() % 3)
			p += sprintf(p, "https://example.com/%u;path?q=x#part", (unsigned)rndmod(16));
		break;
	case 52:
		/* OSC 52 is enabled below to also exercise base64dec(). */
		*p++ = 's';
		*p++ = ';';
		for (i = 0; i < 40; i++) {
			if (rnd() % 20 == 0)
				break;
			*p++ = b64[rndmod(sizeof(b64) - 1)];
		}
		break;
	case 4:
		p += sprintf(p, "%d;rgb:%02x/%02x/%02x", (int)rndmod(512),
		             (unsigned)rndmod(256), (unsigned)rndmod(256),
		             (unsigned)rndmod(256));
		break;
	case 10:
	case 11:
	case 12:
		if (rnd() & 1)
			*p++ = '?';
		else
			p += sprintf(p, "#%06x", (int)rndmod(0x1000000));
		break;
	case 7:
		p += sprintf(p, "file://host/");
		/* FALLTHROUGH */
	default:
		for (i = 0; i < 20; i++)
			*p++ = 0x20 + rndmod(0x60);
		break;
	}
	/* BEL, ESC \\, a raw C1 ST byte (invalid UTF-8), or no terminator. */
	switch (rnd() % 4) {
	case 0: *p++ = '\a'; break;
	case 1: *p++ = '\033'; *p++ = '\\'; break;
	case 2: *p++ = 0x9c; break;
	}
	*len = p - out;
}

static void
gen_esc(char *out, size_t *len)
{
	static const char fins[] = "78=>DMEHcZ%#()*+-./0B";
	static const char cs[] = "0B01A%";
	char *p = out;

	*p++ = '\033';
	*p++ = fins[rndmod(sizeof(fins) - 1)];
	if (rnd() % 3 == 0)
		*p++ = cs[rndmod(sizeof(cs) - 1)];
	*len = p - out;
}

static void
gen_text(char *out, size_t *len)
{
	static const char words[][24] = {
		"http://a.b/c?d=", "https://x/", "(((", ")))", "[]]",
		"a/b/c/.././~", "foo@example.com", "\t\t\t", "\r\n", "\n",
		"--------", "\\\\\\", "0123456789", "AAAAAAAAAAAA",
	};
	const char *s = words[rndmod(LEN(words))];
	size_t n = strlen(s);

	memcpy(out, s, n);
	while (n && rnd() % 2)
		n--;
	*len = n;
}

static void
payload(char *buf, size_t *len, size_t cap)
{
	size_t n = 0, plen;
	char *p = buf;

	/* CSI needs at most 65 bytes; reserve room for every generator. */
	while (cap - n >= 128) {
		switch (rnd() % 8) {
		case 0: case 1: case 2: gen_csi(p, &plen); break;
		case 3: gen_osc(p, &plen); break;
		case 4: gen_esc(p, &plen); break;
		case 5: gen_text(p, &plen); break;
		default: gen_utf8(p, &plen); break;
		}
		p += plen;
		n += plen;
	}
	*len = n;
}

/* Match ttyread(): retain any incomplete UTF-8 until more input arrives. */
static void
writepayload(char *buf, size_t cap, size_t *pending)
{
	size_t len;
	int written;

	assert(*pending < UTF_SIZ && *pending <= cap);
	payload(buf + *pending, &len, cap - *pending);
	len += *pending;
	written = twrite(buf, len, 0);
	assert(written >= 0 && (size_t)written <= len);
	*pending = len - written;
	assert(*pending < UTF_SIZ);
	memmove(buf, buf + written, *pending);
}

/* ---- operations ---- */

static void
do_resize(void)
{
	static const int dims[] = {1, 1, 2, 3, 5, 20, 21, 79, 80, 81, 200, 1};
	int col = dims[rndmod(LEN(dims))], row = dims[rndmod(LEN(dims))];

	if (rnd() % 3 == 0)
		col = 1 + rndmod(50);
	if (rnd() % 3 == 0)
		row = 1 + rndmod(24);
	tresize(col, row);
}

static void
do_scroll(void)
{
	static const int vals[] = {-3, -1, 0, 1, 2, 5, 100, -100, 65536};
	Arg a;

	a.i = vals[rndmod(LEN(vals))];
	if (rnd() & 1)
		kscrollup(&a);
	else
		kscrolldown(&a);
}

/* x.c clamps click coordinates through evcol()/evrow(); do the same. */
static void
do_selection(void)
{
	static const int snaps[] = {0, SNAP_WORD, SNAP_LINE, SNAP_REGEX,
	                            SNAP_REGEX + 1, SNAP_REGEX + 2, SNAP_REGEX + 3};
	int x = rndmod(term.col), y = rndmod(term.row);
	int x2 = rndmod(term.col), y2 = rndmod(term.row);
	int snap = snaps[rndmod(LEN(snaps))];
	int type = rnd() & 1 ? SEL_REGULAR : SEL_RECTANGULAR;
	char *sel;

	selstart(x, y, rnd() & 1 ? snap : 0);
	if (rnd() % 3)
		selextend(x2, y2, type, rnd() & 1);
	if (rnd() % 3 == 0) {
		sel = getsel();
		free(sel);
	}
	if (rnd() % 4 == 0)
		selclear();
}

static void
do_urls(void)
{
	int x = rndmod(term.col), y = rndmod(term.row);
	char *u = tgeturl(x, y);

	free(u);
	if (rnd() % 8 == 0) {
		tflashurl(x, y, NULL);
		if (rnd() & 1)
			tclearurlflash();
	}
}

/* ---- invariants ---- */

static volatile sig_atomic_t step;
static int pairwarnings;

static void
pairwarn(const char *what, int x, int y)
{
	if (pairwarnings++ < 10)
		fprintf(stderr, "warning: step %d: %s at %d,%d "
		        "(size %dx%d, history %d, view %d)\n",
		        step, what, x, y, term.col, term.row,
		        sb.len, sb.view_offset);
}

/*
 * Structural bounds must hold unconditionally (assert).  Wide/dummy
 * pairing is only warned about: upstream erase and insert operations
 * leave half of a wide glyph behind when they cover only the other half.
 */
static void
checkinvariants(int fullview)
{
	Line line;
	int x, y, first = fullview ? sb.view_offset - sb_maxoffset() : 0;
	int end = fullview ? sb.view_offset + term.row : term.row;

	assert(term.col >= 1 && term.row >= 1);
	assert(term.c.x >= 0 && term.c.x < term.col);
	assert(term.c.y >= 0 && term.c.y < term.row);
	assert(sb.len >= 0 && sb.len <= sb.limit && sb.limit <= sb.cap);
	assert(sb.head >= 0 && sb.head < sb.cap);
	assert(sb.view_offset >= 0 && sb.view_offset <= sb_maxoffset());
	for (x = 0; x < 2; x++) {
		assert(term.saved[x].x >= 0 && term.saved[x].x < term.col);
		assert(term.saved[x].y >= 0 && term.saved[x].y < term.row);
	}
	for (y = first; y < end; y++) {
		if (!(line = viewline(y)))
			continue;
		for (x = 0; x < term.col; x++) {
			if ((line[x].mode & ATTR_WIDE) && x + 1 < term.col &&
			    !(line[x + 1].mode & ATTR_WDUMMY))
				pairwarn("wide glyph without dummy cell", x, y);
			if ((line[x].mode & ATTR_WDUMMY) &&
			    !(x > 0 && (line[x - 1].mode & ATTR_WIDE)))
				pairwarn("dummy cell without wide glyph", x, y);
		}
	}
}

/* ---- driver ---- */

static void
hangbackstop(int sig)
{
	char digits[32], *p = digits + sizeof(digits);
	unsigned int n = step;
#if defined(__GLIBC__)
	void *bt[64];
	int frames;
#endif

	(void)sig;
	do {
		*--p = '0' + n % 10;
		n /= 10;
	} while (n);
	/* Avoid stdio locks in the signal handler; print before unwinding. */
	write(2, "fuzz: time budget exceeded at step ",
	      sizeof("fuzz: time budget exceeded at step ") - 1);
	write(2, p, digits + sizeof(digits) - p);
	write(2, " (SIGALRM backstop)\n", sizeof(" (SIGALRM backstop)\n") - 1);
#if defined(__GLIBC__)
	frames = backtrace(bt, LEN(bt));
	backtrace_symbols_fd(bt, frames, 2);
#endif
	_exit(99);
}

int
main(int argc, char **argv)
{
	uint64_t seed = argc > 1 ? strtoull(argv[1], NULL, 0) : 1;
	int episodes = argc > 2 ? atoi(argv[2]) : 6;
	int ops = argc > 3 ? atoi(argv[3]) : 300;
	int seconds = argc > 4 ? atoi(argv[4]) : 120;
	char drain[4096], buf[1 << 12];
	int fds[2], e, o;
	size_t pending;
#if defined(__GLIBC__)
	void *bt[1];

	/* Load the unwinder before a signal could interrupt malloc(). */
	backtrace(bt, LEN(bt));
#endif
	signal(SIGALRM, hangbackstop);
	alarm(seconds);

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
		die("socketpair: %s\n", strerror(errno));
	cmdfd = fds[0];
	if (!setlocale(LC_CTYPE, "C.UTF-8"))
		fprintf(stderr, "fuzz: no UTF-8 locale; wide cells untested\n");
	vtiden = "\033[?6c"; /* DA replies need it */
	iofd = -1;           /* never print */
	allowwindowops = 1;  /* exercise OSC 52 and base64dec() */
	scrollback_lines = 1 + seed % 512;

	rngstate = seed ? seed : 0x9E3779B97F4A7C15ull;
	for (e = 0; e < 8; e++)
		rnd();

	while (episodes-- > 0) {
		int cols = 1 + rndmod(80), rows = 1 + rndmod(30);

		pending = 0;
		reset(cols, rows);
		for (o = 0; o < ops; o++) {
			switch (rnd() % 10) {
			case 0: case 1: case 2: case 3:
				writepayload(buf, sizeof(buf), &pending);
				break;
			case 4:
				do_resize();
				break;
			case 5:
				do_scroll();
				break;
			case 6:
				do_selection();
				break;
			case 7:
				do_urls();
				break;
			case 8:
				writepayload(buf, sizeof(buf), &pending);
				do_scroll();
				break;
			case 9:
				if (rnd() % 6 == 0)
					treset();
				else
					do_selection();
				break;
			}
			step++;
			checkinvariants(0);
			/* Consume replies so the pty peer never blocks us. */
			while (recv(fds[1], drain, sizeof(drain), MSG_DONTWAIT) > 0)
				;
		}
		checkinvariants(1);
	}

	fprintf(stderr, "fuzz: seed %llu done, %d steps, %d pairing warnings\n",
	        (unsigned long long)seed, step, pairwarnings);
	fflush(NULL);
	_exit(0);
}
