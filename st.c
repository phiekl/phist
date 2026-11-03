/* See LICENSE for license details. */
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <regex.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "st.h"
#include "win.h"
#include "compose.h"
#include "combining.h"

#if   defined(__linux)
 #include <pty.h>
#elif defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
 #include <util.h>
#elif defined(__FreeBSD__) || defined(__DragonFly__)
 #include <libutil.h>
#endif

/* Arbitrary sizes */
#define UTF_INVALID   0xFFFD
#define UTF_SIZ       4
#define ESC_BUF_SIZ   (128*UTF_SIZ)
#define ESC_ARG_SIZ   16
#define CAR_PER_ARG   6
#define URLTARGET_MAX 2048 /* characters of a URL underlined and copied */
#define SELCONTEXT    4096 /* cells either side of a click matched by selection patterns */
#define VIM_COUNT_MAX 99999 /* bound repeated motions and multi-key counts */
#define STR_BUF_SIZ   ESC_BUF_SIZ
#define STR_MAX_SIZ   (1 << 20) /* longer DCS/OSC/PM/APC strings are discarded */
#define TITLE_MAX     512       /* bytes of a window title set by applications */
#define CWDURI_MAX    16384     /* bytes of an OSC 7 directory URI */
#define HLINK_MAX      8191     /* fits Glyph.link without enlarging cells */
#define HLINK_ID_MAX   256
#define HLINK_BYTES    (4 << 20) /* retained URI and explicit ID strings */
#define HLINK_BUCKETS  16384    /* hash chains stay short at the slot limit */
#define ERRESC_BURST  20        /* diagnostics printed before rate limiting */
#define ERRESC_RATE   5         /* diagnostics per second after a burst */
#define PRINT_MAX     (1 << 20) /* bytes of -o output waiting for a reader */
#define SHELL_FINISH_MAX 3      /* seconds phist stays after its shell exits */
#define REPLY_MAX     4096      /* bytes of query replies waiting for the child */
#define INPUT_MAX     (64 * 1024 * 1024) /* queued-input limit; an empty queue accepts one larger input */
#define DUMP_MAX      128       /* bytes of a sequence shown in a diagnostic */
#define TITLE_MARKS   4         /* combining marks kept per character in titles */
#define STR_ARG_SIZ   ESC_ARG_SIZ

/* macros */
#ifndef ROWVISIT
#define ROWVISIT() /* tests count row lookups to bound work per update */
#endif
#ifndef LINKVISIT
#define LINKVISIT() /* tests count link lookups to bound work at saturation */
#endif
#define IS_SET(flag)		((term.mode & (flag)) != 0)
#define ISCONTROLC0(c)		(BETWEEN(c, 0, 0x1f) || (c) == 0x7f)
#define ISCONTROLC1(c)		(BETWEEN(c, 0x80, 0x9f))
#define ISCONTROL(c)		(ISCONTROLC0(c) || ISCONTROLC1(c))
#define ISDELIM(u)		(u && wcschr(worddelimiters, u))

enum term_mode {
	MODE_WRAP        = 1 << 0,
	MODE_INSERT      = 1 << 1,
	MODE_ALTSCREEN   = 1 << 2,
	MODE_CRLF        = 1 << 3,
	MODE_ECHO        = 1 << 4,
	MODE_PRINT       = 1 << 5,
	MODE_UTF8        = 1 << 6,
};

enum cursor_movement {
	CURSOR_SAVE,
	CURSOR_LOAD
};

enum cursor_state {
	CURSOR_DEFAULT  = 0,
	CURSOR_WRAPNEXT = 1,
	CURSOR_ORIGIN   = 2
};

enum charset {
	CS_GRAPHIC0,
	CS_GRAPHIC1,
	CS_UK,
	CS_USA,
	CS_MULTI,
	CS_GER,
	CS_FIN
};

enum escape_state {
	ESC_START      = 1,
	ESC_CSI        = 2,
	ESC_STR        = 4,  /* DCS, OSC, PM, APC */
	ESC_ALTCHARSET = 8,
	ESC_STR_END    = 16, /* a final string was encountered */
	ESC_TEST       = 32, /* Enter in test mode */
	ESC_UTF8       = 64,
};

typedef struct {
	Glyph attr; /* current char attributes */
	int x;
	int y;
	char state;
} TCursor;

typedef struct {
	int mode;
	int type;
	int snap;
	/*
	 * Selection variables:
	 * nb – normalized coordinates of the beginning of the selection
	 * ne – normalized coordinates of the end of the selection
	 * ob – original coordinates of the beginning of the selection
	 * oe – original coordinates of the end of the selection
	 */
	struct {
		int x, y;
	} nb, ne, ob, oe;

	int alt;
} Selection;

typedef struct {
	int first, last;
} SelMatch;

/* Internal representation of the screen */
typedef struct {
	int row;      /* nb row */
	int col;      /* nb col */
	Line *line;   /* screen */
	Line *alt;    /* alternate screen */
	int *dirty;   /* dirtyness of lines */
	TCursor c;    /* cursor */
	TCursor saved[2]; /* saved main/alternate cursors */
	TCursor mainc; /* main cursor while an alternate screen is active */
	int ocx;      /* old cursor col */
	int ocy;      /* old cursor row */
	int top;      /* top    scroll limit */
	int bot;      /* bottom scroll limit */
	int mode;     /* terminal mode flags */
	int esc;      /* escape state flags */
	char trantbl[4]; /* charset table translation */
	int charset;  /* current charset */
	int icharset; /* selected charset for sequence */
	int *tabs;
	Rune lastc;   /* last printed char outside of sequence, 0 if control */
} Term;

/* CSI Escape sequence structs */
/* ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]] */
typedef struct {
	char buf[ESC_BUF_SIZ]; /* raw string */
	size_t len;            /* raw string length */
	char priv;
	int arg[ESC_ARG_SIZ];
	int narg;              /* nb of args */
	char mode[2];
	int carg[ESC_ARG_SIZ][CAR_PER_ARG]; /* colon args */
	int cargc[ESC_ARG_SIZ]; /* includes omitted colon fields */
} CSIEscape;

/* STR Escape sequence structs */
/* ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\' */
typedef struct {
	char type;             /* ESC type ... */
	char *buf;             /* allocated raw string */
	size_t siz;            /* allocation size */
	size_t len;            /* raw string length */
	int overflow;          /* exceeded STR_MAX_SIZ and is being discarded */
	char *args[STR_ARG_SIZ];
	int narg;              /* nb of args */
} STREscape;

static void execsh(char *, char **);
static void stty(char **);
static void sigchld(int);

static int erresc(const char *, ...);
static void csidump(void);
static void csihandle(void);
static int readcolonargs(char **, int, int[][CAR_PER_ARG]);
static void csiparse(void);
static void csireset(void);
static void osc_color_response(int, int, int);
static void osc_cwd(const char *, size_t);
static void osc_link(char *, size_t);
static int eschandle(Rune);
static void strdump(void);
static void strhandle(void);
static void strparse(void);
static char *strtitle(char *);
static void strreset(void);

static void tprinter(char *, size_t);
static void printopen(const char *);
static void tdumpsel(void);
static void tdumpline(Line);
static void tdump(void);
static void tclearregion(int, int, int, int);
static void tcursor(int);
static void tdeletechar(int);
static void tdeleteline(int);
static void tinsertblank(int);
static void tinsertblankline(int);
static int tlinelen(Line);
static void tmoveto(int, int);
static void tmoveato(int, int);
static void tnewline(int);
static void tputtab(int);
static void tputc(Rune);
static Glyph *tprevcell(void);
static void tpresentation(int);
static Rune compose(Rune, Rune);
static void tcombine(Rune);
static void treset(void);
static void tresetstate(void);
static void tscrollup(int, int, int);
static void tscrolldown(int, int);
static void tsetattr(const int *, int);
static void tsetchar(Rune, const Glyph *, int, int);
static void tsetdirt(int, int);
static void tsetscroll(int, int);
static void tswapscreen(void);
static void tsetmode(int, int, const int *, int);
static int twrite(const char *, int, int);
static void tcontrolcode(uchar );
static void tdectest(Rune);
static void tdefutf8(Rune);
static int32_t tdefcolor(const int *, int *, int);
static void tdeftran(Rune);
static void tstrsequence(uchar);

static void drawregion(int, int, int, int);

static void selnormalize(void);
static void selscroll(int, int);
static void selpushed(int, int, int);
static void selsnap(int *, int *, int);
static void selregex(int *, int *, int);

static size_t utf8decode(const char *, Rune *, size_t);
static Rune utf8decodebyte(char, size_t *);
static char utf8encodebyte(Rune, size_t);
static size_t utf8validate(Rune *, size_t);

static char *base64dec(const char *);
static char base64dec_getc(const char **);

static ssize_t xwrite(int, const char *, size_t);
static void urlupdate(void);
static Line viewline(int);

/* Globals */
static Term term;
static Selection sel;
static CSIEscape csiescseq;
static STREscape strescseq;
static int iofd = 1;
static int mcallowed; /* honor Media Copy only with an explicit -o */
static int cmdfd;
static pid_t pid;

/*
 * URL overlays belong to the displayed view, not the terminal's stored
 * glyphs. They are recomputed from the visible text and some context
 * whenever anything that could affect it changes.
 */
static struct {
	int *cells, *old; /* target index + 1 for each visible cell */
	int cols, rows, count, capacity, dirty;
	char **targets;
	int flash, flashcol, flashrow;
	char *flashtext;
} urls = { .dirty = 1 };

static struct {
	const char **seen, **prefixes; /* configured and accepted prefixes */
	size_t *lengths;
	int nseen, count, ready;
	uchar allowed[256], trailing[256];
} urlcfg;

static const uchar utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const uchar utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static const Rune utfmin[UTF_SIZ + 1] = {       0,    0,  0x80,  0x800,  0x10000};
static const Rune utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

/* History keeps text separately from runs of identical cell attributes. */
typedef struct {
	uint64_t end : 21; /* exclusive cell offset; wider rows stay raw */
	uint64_t mode : 18;
	uint64_t fg : 25;
	uint64_t bg : 25;
	uint64_t ucolor : 26;
	uint64_t link : 13;
} HistoryRun;

/* A run must cost no more than a raw cell, or compact rows lose their benefit. */
typedef char history_run_size[(sizeof(HistoryRun) <= sizeof(Glyph)) ? 1 : -1];

typedef struct {
	void *cells; /* raw Glyphs, or HistoryRun[runs] followed by Rune[cols] */
	int cols;
	int runs; /* zero selects raw cells when encoding would cost more */
} HistoryLine;

typedef struct
{
	HistoryLine *buf; /* ring of independently sized history rows */
	int cap;         /* max number of lines */
	int limit;       /* retained lines; cap also reserves resize scratch space */
	int len;         /* current number of valid lines (<= cap) */
	int head;        /* physical index of logical oldest (valid when len>0) */
	uint64_t base;   /* absolute index of the oldest retained row */
	/* Widest row pushed; may overestimate after eviction, causing extra reflow. */
	int max_width;
	int view_offset; /* 0 means live screen */
} Scrollback;

static Scrollback sb;
typedef struct {
	uint64_t row;
	int x;
} SearchPos;

static struct {
	int active, found, len, refresh, originlive;
	int count, index; /* total matches and current 1-based match index */
	Rune query[SEARCH_MAX];
	int failure[SEARCH_MAX];
	uint64_t origin;
	SearchPos start, end;
	/* Cells of every match in view, as of the last frame. */
	uchar *marks, *newmarks;
	int markrows, markcols, marked;
} search;

static void searchvalidate(void);
static void searchmarks(void);

static struct {
	int mode; /* VIM_NORMAL and up, or 0 outside the mode */
	int count, originlive, searchdir;
	Rune pending, find, findkind;
	SearchPos pos, anchor;
	uint64_t origin;
} vim;

static void vimupdate(void);
static void vimvalidatesel(void);
static void vimvalidate(void);
static regex_t *selpatterns; /* compiled selectionpatterns */
static int nselpatterns;

static struct {
	struct {
		char *uri, *name;
		uint64_t refs, hash;
		int next, overlay; /* next hash-chain entry, or next free slot */
	} slots[HLINK_MAX + 1];
	int buckets[HLINK_BUCKETS];
	int allocated, free, used, seeded;
	uint64_t seed;
	size_t bytes;
} hlinks;

/* Output must not be able to prepare targets sharing a bucket, which would
 * make every lookup walk a long chain. The clock only covers a missing
 * /dev/urandom. */
static void
linkseed(void)
{
	struct timespec now;
	int fd = open("/dev/urandom", O_RDONLY);
	ssize_t got = fd < 0 ? -1 : read(fd, &hlinks.seed, sizeof(hlinks.seed));

	if (fd >= 0)
		close(fd);
	if (got != sizeof(hlinks.seed)) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		hlinks.seed = (uint64_t)now.tv_nsec << 32 ^ now.tv_sec ^ getpid();
	}
	hlinks.seeded = 1;
}

/* Include the URI's terminating NUL to separate it from the optional ID. The
 * last steps matter: multiplying only carries bits upwards, so a bucket taken
 * straight from the low bits would ignore most of the seed. */
static uint64_t
linkhash(const char *uri, const char *name)
{
	const uchar *p;
	uint64_t hash;

	if (!hlinks.seeded)
		linkseed();
	/* 64-bit FNV-1a, seeded: its offset basis, then its prime for each byte. */
	hash = hlinks.seed ^ UINT64_C(14695981039346656037);
	for (p = (const uchar *)uri; *p; p++)
		hash = (hash ^ *p) * UINT64_C(1099511628211);
	hash *= UINT64_C(1099511628211);
	if (name)
		for (p = (const uchar *)name; *p; p++)
			hash = (hash ^ *p) * UINT64_C(1099511628211);
	/* MurmurHash3's fmix64 finalizer: its published multipliers. */
	hash = (hash ^ (hash >> 33)) * UINT64_C(0xff51afd7ed558ccd);
	hash = (hash ^ (hash >> 33)) * UINT64_C(0xc4ceb9fe1a85ec53);
	return hash ^ (hash >> 33);
}

static int
linkfind(const char *uri, const char *name, uint64_t hash)
{
	int id;

	for (id = hlinks.buckets[hash & (HLINK_BUCKETS - 1)]; id;
	     id = hlinks.slots[id].next) {
		LINKVISIT();
		if (hlinks.slots[id].hash == hash && !strcmp(uri, hlinks.slots[id].uri) &&
		    ((!name && !hlinks.slots[id].name) ||
		     (name && hlinks.slots[id].name && !strcmp(name, hlinks.slots[id].name))))
			return id;
	}
	return 0;
}

/* Only owned cells, history and cursor attributes count; decoded scratch
 * rows borrow their references. Reclaim a target as its last owner goes. */
static void
linkref(unsigned int id, int delta)
{
	int *p;

	if (!id)
		return;
	if (delta > 0) {
		hlinks.slots[id].refs += delta;
		return;
	}
	hlinks.slots[id].refs -= -delta;
	if (hlinks.slots[id].refs)
		return;
	for (p = &hlinks.buckets[hlinks.slots[id].hash & (HLINK_BUCKETS - 1)];
	     *p != id; p = &hlinks.slots[*p].next)
		;
	*p = hlinks.slots[id].next;
	hlinks.bytes -= strlen(hlinks.slots[id].uri) + 1;
	if (hlinks.slots[id].name)
		hlinks.bytes -= strlen(hlinks.slots[id].name) + 1;
	free(hlinks.slots[id].uri);
	free(hlinks.slots[id].name);
	memset(&hlinks.slots[id], 0, sizeof(hlinks.slots[id]));
	hlinks.slots[id].next = hlinks.free;
	hlinks.free = id;
	hlinks.used--;
}

static void
linkset(Glyph *g, unsigned int id)
{
	if (!id)
		g->mode &= ~ATTR_LINKSTART;
	if (g->link == id)
		return;
	linkref(id, 1);
	linkref(g->link, -1);
	g->link = id;
}

static void
linkcells(const Glyph *line, int cols, int delta)
{
	int x;
	for (x = 0; x < cols; x++)
		linkref(line[x].link, delta);
}

static void
linkmove(Line dst, Line src, int cols)
{
	linkcells(src, cols, 1);
	linkcells(dst, cols, -1);
	memmove(dst, src, cols * sizeof(Glyph));
}

static void
sb_free(HistoryLine *row)
{
	HistoryRun *runs = row->cells;
	int r, start = 0;
	if (!row->runs)
		linkcells(row->cells, row->cols, -1);
	else
		for (r = 0; r < row->runs; r++) {
			linkref(runs[r].link, -(int)(runs[r].end - start));
			start = runs[r].end;
		}
	free(row->cells);
	*row = (HistoryLine){0};
}

static int
sb_phys_index(int logical_idx)
{
	/* logical_idx: 0..sb.len-1 (0 = oldest) */
	return (sb.head + logical_idx) % sb.cap;
}

/* Blank cells sb_get() recreates for history lines narrower than the screen. */
static Glyph
sb_padglyph(void)
{
	return (Glyph){ .u = ' ', .fg = defaultfg, .bg = defaultbg,
		.ucolor = UCOLOR_DEFAULT };
}

/* Foreground and underline colors are invisible on unstyled blank cells. */
static int
sb_ispadding(const Glyph *glyph)
{
	return glyph->u == ' ' && glyph->mode == 0 && glyph->bg == defaultbg && !glyph->link;
}

/* Number of leading content cells; the trailing padding is recreated. */
static int
sb_contentwidth(const Glyph *line, int cols)
{
	if (cols > 0 && line[cols - 1].mode & ATTR_WRAP)
		return cols;
	while (cols > 0 && sb_ispadding(&line[cols - 1]))
		cols--;
	return cols;
}

static int
sb_storedwidth(const Glyph *line, int cols)
{
	return MAX(1, sb_contentwidth(line, cols));
}

/* One allocation per row; unusually varied attributes fall back to raw cells. */
static HistoryLine
sb_pack(const Glyph *line, int cols)
{
	HistoryLine row = { .cols = cols };
	HistoryRun *runs;
	Rune *text;
	size_t rawsize = (size_t)cols * sizeof(Glyph), size;
	int x, n = 1, r = 0;

	linkcells(line, cols, 1);
	for (x = 1; x < cols; x++) {
		if (ATTRCMP(line[x - 1], line[x]))
			n++;
		if ((size_t)cols * sizeof(Rune) + n * sizeof(HistoryRun) >= rawsize)
			break;
	}
	size = (size_t)cols * sizeof(Rune) + n * sizeof(HistoryRun);
	if (size >= rawsize || cols >= 1 << 21) {
		row.cells = xmalloc(rawsize);
		memcpy(row.cells, line, rawsize);
		return row;
	}
	row.runs = n;
	row.cells = runs = xmalloc(size);
	text = (Rune *)(runs + n);
	for (x = 0; x < cols; x++) {
		text[x] = line[x].u;
		if (!x || ATTRCMP(line[x - 1], line[x])) {
			if (x)
				runs[r++].end = x;
			runs[r] = (HistoryRun){ .mode = line[x].mode,
				.fg = line[x].fg, .bg = line[x].bg,
				.ucolor = line[x].ucolor, .link = line[x].link };
		}
	}
	runs[r].end = cols;
	return row;
}

/* Decode a prefix and recreate padding without changing the stored row. */
static void
sb_unpack(const HistoryLine *row, Line line, int cols)
{
	const HistoryRun *runs = row->cells;
	const Rune *text = (const Rune *)(runs + row->runs);
	Glyph attr;
	int x = 0, r, end, stored = MIN(cols, row->cols);

	if (!row->runs) {
		memcpy(line, row->cells, (size_t)stored * sizeof(Glyph));
		x = stored;
	} else {
		for (r = 0; x < stored; r++) {
			attr = (Glyph){ .mode = runs[r].mode, .fg = runs[r].fg,
				.bg = runs[r].bg, .ucolor = runs[r].ucolor, .link = runs[r].link };
			end = MIN(stored, runs[r].end);
			for (; x < end; x++) {
				line[x] = attr;
				line[x].u = text[x];
			}
		}
	}
	for (; x < cols; x++)
		line[x] = sb_padglyph();
}

static void
sb_init(int lines)
{
	int i;

	urls.dirty = 1;
	sb.buf  = xmalloc(sizeof(*sb.buf) * MAX(1, lines));
	sb.cap  = lines;
	sb.limit = lines;
	sb.len  = 0;
	sb.head = 0;
	sb.base = 0;
	for (i = 0; i < sb.cap; i++)
		sb.buf[i] = (HistoryLine){0};

	sb.view_offset = 0;
	sb.max_width = 0;
}

/* Evict an oldest row from either the live history or a reflow replacement. */
static void
sb_evict(Scrollback *history)
{
	if (!history->len)
		return;
	sb_free(&history->buf[history->head]);
	history->head = (history->head + 1) % history->cap;
	history->base++;
	history->len--;
	/* max_width remains a conservative bound if its widest row was evicted. */
	urls.dirty = 1;
}

/* Push one screen line into scrollback.
 * Overwrites oldest when full (ring buffer).
 */
static void
sb_push(Line line)
{
	int tail;
	int width, stored;

	if (sb.limit <= 0)
		return;

	stored = sb_storedwidth(line, term.col);

	if (sb.len == sb.limit)
		sb_evict(&sb);
	tail = sb_phys_index(sb.len);
	sb.buf[tail] = sb_pack(line, stored);
	sb.len++;
	width = sb_contentwidth(line, term.col);
	if (width > sb.max_width)
		sb.max_width = width;
}

/*
 * Compact or narrow rows are decoded/padded into scratch rows instead of
 * growing in place, so history storage stays compact.  A returned row is
 * valid for the next LEN(scratch) - 1 calls, more than the few rows any
 * caller compares at once; it must not be written to.
 */
static Line
sb_get(int idx)
{
	static Line scratch[16];
	static int scratchcols;
	static unsigned int next; /* wraps safely; LEN(scratch) divides 2^32 */
	HistoryLine *line;
	Line row;
	int x;

	ROWVISIT();
	/* idx is logical: 0..sb.len-1 */
	if (idx < 0 || idx >= sb.len)
		return NULL;
	line = &sb.buf[sb_phys_index(idx)];
	if (!line->runs && line->cols >= term.col)
		return line->cells;
	if (scratchcols != term.col) {
		for (x = 0; x < LEN(scratch); x++) {
			free(scratch[x]);
			scratch[x] = xmalloc(term.col * sizeof(Glyph));
		}
		scratchcols = term.col;
	}
	row = scratch[next++ % LEN(scratch)];
	sb_unpack(line, row, term.col);
	return row;
}

/* Largest history scroll; the alternate screen also reveals the main one. */
static int
sb_maxoffset(void)
{
	return sb.len + (IS_SET(MODE_ALTSCREEN) ? term.row : 0);
}

/* Keep visible rows outside the history limit while rebuilding the screen. */
static void
sb_setlimit(int limit)
{
	HistoryLine *buf;
	int i, cap;

	/* Keep the index allocation until a larger scratch area is needed. */
	if (limit > sb.cap) {
		cap = MAX(limit, sb.cap + 64);
		buf = xmalloc(cap * sizeof(*buf));
		for (i = 0; i < sb.len; i++)
			buf[i] = sb.buf[sb_phys_index(i)];
		free(sb.buf);
		sb.buf = buf;
		sb.cap = cap;
		sb.head = 0;
	}
	while (sb.len > limit)
		sb_evict(&sb);
	sb.limit = limit;
	sb.view_offset = MIN(sb.view_offset, sb_maxoffset());
}

static void
sb_clear(void)
{
	int i;
	int p;

	urls.dirty = 1;
	if (!sb.buf)
		return;

	for (i = 0; i < sb.len; i++) {
		p = sb_phys_index(i);
		if (sb.buf[p].cells) {
			sb_free(&sb.buf[p]);
		}
	}

	sb.len = 0;
	sb.head = 0;
	sb.base = 0;
	sb.view_offset = 0;
	sb.max_width = 0;
}

/*
 * Join soft-wrapped rows into logical lines, then pack them into a new history
 * ring at width col. Keep wide glyphs with their dummy cells, mark unused margin
 * cells as padding and trim ordinary trailing blanks. Cursor marks follow text.
 * Rebuilding is linear in the stored text and replaces sb's ring and indexes.
 */
typedef struct {
	TCursor *cursor;
	int source_row, row, offset;
} ReflowCursor;

static void
sb_resize(int col, int rows, ReflowCursor *marks, int nmarks)
{
	Scrollback next = { .cap = sb.cap, .limit = sb.limit };
	int i, j, k, produced = 0, minimum, filled;
	int logical_cap, logical_len, is_wrapped, cursor;
	int copy_width, tail, current_width, stored, position, pending;
	HistoryLine *source;
	Line logical, line, nl;
	int screen_end = INT_MAX;

	if (sb.len == 0)
		return;

	next.buf = xmalloc(sizeof(*next.buf) * next.cap);
	for (i = 0; i < sb.cap; i++)
		next.buf[i] = (HistoryLine){0};

	logical_cap = term.col * 2;
	logical = xmalloc(logical_cap * sizeof(Glyph));
	logical_len = 0;
	line = xmalloc(term.col * sizeof(Glyph));
	nl = xmalloc(col * sizeof(Glyph));

	for (i = 0; i < sb.len; i++) {
		/* Unwrap: Accumulate physical lines into one logical line. */
		ROWVISIT();
		source = &sb.buf[sb_phys_index(i)];
		/* Only the stored cells are needed; wrapped lines are full width. */
		stored = MIN(source->cols, term.col);
		sb_unpack(source, line, stored);
		is_wrapped = i + 1 < sb.len && stored == term.col &&
		             (line[term.col - 1].mode & ATTR_WRAP);
		if (logical_len + 2 * term.col > logical_cap) {
			logical_cap = MAX(logical_cap * 2, logical_len + 2 * term.col);
			logical = xrealloc(logical, logical_cap * sizeof(Glyph));
		}

		for (j = 0; j < stored; j++) {
			/* Recreate pairs, including wide glyphs squeezed into one column. */
			position = MAX(0, logical_len - !!(line[j].mode & ATTR_WDUMMY));
			for (k = 0; k < nmarks; k++) {
				if (marks[k].source_row != i || marks[k].cursor->x != j)
					continue;
				pending = !!(marks[k].cursor->state & CURSOR_WRAPNEXT);
				marks[k].offset = position + pending *
					((line[j].mode & ATTR_WPAD) ? 0 :
					 (line[j].mode & ATTR_WIDE) ? 2 : 1);
			}
			if (line[j].mode & (ATTR_WDUMMY | ATTR_WPAD))
				continue;
			logical[logical_len] = line[j];
			logical[logical_len++].mode &= ~ATTR_WRAP;
			if (line[j].mode & ATTR_WIDE) {
				logical[logical_len] = line[j];
				logical[logical_len].u = 0;
				logical[logical_len++].mode = ATTR_WDUMMY;
			}
		}
		for (k = 0; k < nmarks; k++)
			if (marks[k].source_row == i && marks[k].cursor->x >= stored)
				marks[k].offset = logical_len + marks[k].cursor->x - stored +
					!!(marks[k].cursor->state & CURSOR_WRAPNEXT);
		/* If the line was wrapped, continue accumulating before reflowing. */
		if (is_wrapped) {
			continue;
		}
		if (!logical_len)
			logical[logical_len++] = sb_padglyph();
		filled = logical_len;
		/* Preserve styled blanks; only ordinary padding may be trimmed. */
		logical_len = sb_storedwidth(logical, logical_len);
		/* Preserve cursor positions in otherwise trimmable blank cells. */
		for (k = 0; k < nmarks; k++) {
			if (marks[k].source_row < 0 || marks[k].source_row > i)
				continue;
			minimum = marks[k].offset +
				!(marks[k].cursor->state & CURSOR_WRAPNEXT);
			if (minimum > logical_cap) {
				logical_cap = MAX(minimum, logical_cap * 2);
				logical = xrealloc(logical, logical_cap * sizeof(*logical));
			}
			while (filled < minimum)
				logical[filled++] = sb_padglyph();
			logical_len = MAX(logical_len, minimum);
		}

		/* Reflow: Pack whole glyphs into rows. */
		cursor = 0;
		while (cursor < logical_len && produced < screen_end) {
			for (j = 0; j < col; j++)
				nl[j] = sb_padglyph();

			copy_width = logical_len - cursor;
			if (copy_width > col)
				copy_width = col;
			if (col > 1 && logical[cursor + copy_width - 1].mode & ATTR_WIDE)
				copy_width--;
			current_width = copy_width;
			/* A one-column window clips a wide glyph, retaining it for widening. */
			if (col == 1 && logical[cursor].mode & ATTR_WIDE) {
				copy_width = 2;
				current_width = 1;
			}

			memcpy(nl, logical + cursor, current_width * sizeof(Glyph));

			/* Cursor coordinates follow actual row boundaries, including padding. */
			for (k = 0; k < nmarks; k++) {
				if (marks[k].source_row < 0 || marks[k].source_row > i)
					continue;
				position = marks[k].offset - cursor;
				pending = marks[k].cursor->state & CURSOR_WRAPNEXT;
				if (position < 0 || position > copy_width ||
				    (position == copy_width && (!pending ||
				     (current_width < col && cursor + copy_width < logical_len))))
					continue;
				marks[k].cursor->x = MIN(position, col - 1);
				MODBIT(marks[k].cursor->state,
				       pending && position == copy_width && current_width == col,
				       CURSOR_WRAPNEXT);
				marks[k].row = produced;
				marks[k].source_row = -1;
			}
			/* Later rows cannot share a screen with the active cursor. */
			if (marks[0].source_row == -1)
				screen_end = marks[0].row + rows;

			if (cursor + copy_width < logical_len) {
				nl[col - 1].mode |= ATTR_WRAP;
				if (current_width < col)
					nl[col - 1].mode |= ATTR_WPAD;
			}

			/* Rebuild: Push new lines into the ring buffer. */
			if (next.len == next.limit)
				sb_evict(&next);
			stored = sb_storedwidth(nl, col);
			tail = (next.head + next.len) % next.cap;
			next.buf[tail] = sb_pack(nl, stored);
			next.len++;
			if (cursor + copy_width < logical_len)
				current_width = col;
			if (current_width > next.max_width)
				next.max_width = current_width;
			cursor += copy_width;
			produced++;
		}
		logical_len = 0;
		if (produced == screen_end)
			break;
	}
	free(nl);
	free(line);
	free(logical);
	sb_clear();
	free(sb.buf);
	sb = next;
	for (k = 0; k < nmarks; k++) {
		if (marks[k].source_row >= 0)
			marks[k].row = produced - 1;
		marks[k].row -= sb.base;
	}
}

static void
sb_pop_screen(int start, int loaded, int new_cols)
{
	int i, p;

	loaded = MIN(loaded, sb.len - start);
	if (loaded > 0)
		urls.dirty = 1;
	new_cols = MIN(new_cols, term.col);
	for (i = 0; i < sb.len - start; i++) {
		p = sb_phys_index(start + i);
		if (i < loaded) {
			linkcells(term.line[i], new_cols, -1);
			sb_unpack(&sb.buf[p], term.line[i], new_cols);
			linkcells(term.line[i], new_cols, 1);
		}

		sb_free(&sb.buf[p]);
	}

	sb.len = start;
}

static uint64_t
sb_view_start(void)
{
	return sb.base + sb.len - sb.view_offset;
}

/* Rows the view moved down by; starts differ by at most the history size. */
static int
sb_startdelta(uint64_t oldstart, uint64_t newstart)
{
	return oldstart >= newstart ? (int)(oldstart - newstart) :
	       -(int)(newstart - oldstart);
}

static void
sb_view_changed(void)
{
	urls.dirty = 1;
	if (!term.dirty || term.row <= 0)
		return;
	tfulldirt();
}

/* Follow text the view moved by delta rows; keep it while its rows exist. */
static void
selscrollback(int delta)
{
	if (sel.ob.x == -1 || sel.alt != IS_SET(MODE_ALTSCREEN))
		return;

	sel.nb.y += delta;
	sel.ne.y += delta;
	sel.ob.y += delta;
	sel.oe.y += delta;

	/* Even a fixed view can lose selected rows evicted from history. */
	if (!viewline(sel.nb.y) || !viewline(sel.ne.y))
		selclear();
	else if (delta)
		tfulldirt();
}

static Line
emptyline(void)
{
	static Line empty;
	static int empty_cols;
	int i = 0;

	if (empty_cols != term.col) {
		free(empty);
		empty = xmalloc(term.col * sizeof(Glyph));
		empty_cols = term.col;
	}

	for (i = 0; i < term.col; i++) {
		empty[i] = term.c.attr;
		empty[i].u = ' ';
		empty[i].mode = 0;
		empty[i].link = 0;
	}
	return empty;
}

/*
 * Resolve rows relative to the viewport, including hidden wrapped
 * continuations. On the alternate screen the main screen sits between the
 * history and the alternate screen, so scrolling up shows what the program
 * covers.
 */
static Line
viewline(int row)
{
	static Line boundary;
	static int boundarycols;
	int logical;

	ROWVISIT();
	logical = sb_maxoffset() - sb.view_offset + row;
	if (logical < 0 || logical >= sb_maxoffset() + term.row)
		return NULL;
	if (logical < sb.len)
		return sb_get(logical);
	logical -= sb.len;
	if (!IS_SET(MODE_ALTSCREEN))
		return term.line[logical];
	if (logical >= term.row)
		return term.line[logical - term.row];
	if (logical < term.row - 1)
		return term.alt[logical];
	/* The main screen's last row never continues onto the alternate one. */
	if (boundarycols != term.col) {
		free(boundary);
		boundary = xmalloc(term.col * sizeof(Glyph));
		boundarycols = term.col;
	}
	memcpy(boundary, term.alt[logical], term.col * sizeof(Glyph));
	boundary[term.col - 1].mode &= ~ATTR_WRAP;
	return boundary;
}

static Line
renderline(int y)
{
	Line line = viewline(y);

	return line ? line : emptyline();
}

/* Search positions use the same row order as viewline(): history, main
 * screen, then the alternate screen when present. Absolute rows follow
 * retained text while new output moves it into and through the ring. */
static uint64_t
searchtop(void)
{
	return sb.base + sb_maxoffset() - sb.view_offset;
}

static int
searchcmp(SearchPos a, SearchPos b)
{
	if (a.row != b.row)
		return a.row < b.row ? -1 : 1;
	return (a.x > b.x) - (a.x < b.x);
}

static void
searchview(uint64_t top)
{
	uint64_t oldtop = searchtop(), bottom = sb.base + sb_maxoffset();

	top = MAX(sb.base, MIN(top, bottom));
	sb.view_offset = bottom - top;
	selscrollback(sb_startdelta(oldtop, top));
	sb_view_changed();
}

int
tsearchactive(void)
{
	return search.active;
}

int
tsearchfound(void)
{
	return search.found;
}

void
tsearchstart(void)
{
	search.active = 1;
	search.found = search.len = search.refresh = search.count = 0;
	search.origin = searchtop();
	search.originlive = sb.view_offset == 0;
	tfulldirt();
}

void
tsearchend(int cancel)
{
	if (!search.active)
		return;
	if (vim.mode && !cancel && search.found)
		vim.pos = search.start;
	search.active = search.found = search.refresh = 0;
	if (cancel) {
		searchview(search.originlive ? sb.base + sb_maxoffset() : search.origin);
		/* n and N have nothing to repeat after a cancelled search. */
		search.len = 0;
	}
	if (vim.mode) {
		vimvalidatesel();
		vimupdate();
	}
	tfulldirt();
}

/* Feed one character to the matcher, ready for overlapping matches. Return
 * whether it completed a match. */
static int
searchfeed(Rune u, int *matched)
{
	while (*matched && u != search.query[*matched])
		*matched = search.failure[*matched - 1];
	if (u == search.query[*matched])
		(*matched)++;
	if (*matched != search.len)
		return 0;
	*matched = search.failure[*matched - 1];
	return 1;
}

/* Find the nth match past pivot in direction, wrapping around if
 * asked, or else keeping the current match if there is none. Return
 * whether there was one past pivot. Scan once using KMP, including
 * overlapping matches. A query-sized ring finds match bounds. For counted
 * navigation, retain at most n matches for a possible wrap: the first n
 * forwards, or a ring of the last n backwards. The retained history is never
 * flattened or expanded. Hard breaks reset the matcher, soft wraps do not. */
static int
searchscan(int direction, SearchPos pivot, int canwrap, int n)
{
	SearchPos positions[SEARCH_MAX], start, end;
	struct SearchHit { SearchPos start, end; } single, best, *hits = &single;
	Line line;
	uint64_t top;
	int y, x, width, matched = 0, slot = 0, havebest = 0;
	int relative, total = sb_maxoffset() + term.row;
	int count = 0, before = 0, upto = 0, after = 0;
	int64_t index;

	if (!search.active || !search.len)
		return 0;
	if (n > 1)
		hits = xmalloc((size_t)n * sizeof(*hits));
	for (y = 0; y < total; y++) {
		line = viewline(y - (sb_maxoffset() - sb.view_offset));
		width = tlinelen(line);
		for (x = 0; x < width; x++) {
			if (line[x].mode & (ATTR_WDUMMY | ATTR_WPAD))
				continue;
			positions[slot] = (SearchPos){ sb.base + y, x };
			slot = (slot + 1) % search.len;
			if (!searchfeed(line[x].u, &matched))
				continue;
			start = positions[slot];
			end = (SearchPos){ sb.base + y,
				MIN(term.col - 1, x + !!(line[x].mode & ATTR_WIDE)) };
			count++;
			relative = searchcmp(start, pivot);
			before += relative < 0;
			upto += relative <= 0;
			if (direction < 0 || count <= n)
				hits[(count - 1) % n] = (struct SearchHit){ start, end };
			if (direction > 0 && relative > 0 && ++after == n) {
				best = (struct SearchHit){ start, end };
				havebest = 1;
			} else if (direction < 0 && relative < 0 && count >= n) {
				best = hits[(count - n) % n];
				havebest = 1;
			}
		}
		if (!(line[term.col - 1].mode & ATTR_WRAP))
			matched = 0;
	}
	if (!havebest && !canwrap) {
		if (hits != &single)
			free(hits);
		return 0;
	}
	search.found = count > 0;
	search.count = count;
	if (search.found) {
		index = direction < 0 ? before - (int64_t)n : upto + (int64_t)n - 1;
		index = (index % count + count) % count;
		search.index = index + 1;
		if (!havebest)
			best = hits[index % n];
		search.start = best.start;
		search.end = best.end;
		/* Leave room for the prompt and context around the first row. */
		top = search.start.row - MIN(search.start.row - sb.base,
		                            (uint64_t)(term.row / 2));
		searchview(top);
	}
	if (hits != &single)
		free(hits);
	tfulldirt();
	return havebest;
}

void
tsearchnext(int direction)
{
	/* Without a current match, use the live edge or the retained view;
	 * output may have moved or evicted the original cancellation point. */
	uint64_t top = search.originlive ? sb.base + sb_maxoffset() : searchtop();

	searchscan(direction < 0 ? -1 : 1, search.found ? search.start :
	           (SearchPos){ top + term.row - 1, term.col }, 1, 1);
}

/* Find the nearest match above the rows in view, or below them, skipping
 * any in view. On the first or last page with matches, go to the first or
 * last match instead. */
void
tsearchpage(int direction)
{
	int found;

	if (direction < 0)
		found = searchscan(-1, (SearchPos){ searchtop(), -1 }, 0, 1);
	else
		found = searchscan(1, (SearchPos){ searchtop() + term.row - 1,
		                                   term.col }, 0, 1);
	if (!found)
		tsearchedge(direction > 0);
}

/* Find the first match in the history, or the last. */
void
tsearchedge(int last)
{
	if (last)
		searchscan(-1, (SearchPos){ UINT64_MAX, INT_MAX }, 1, 1);
	else
		searchscan(1, (SearchPos){ 0, -1 }, 1, 1);
}

void
tsearchset(const char *text)
{
	Rune u, composed;
	size_t i = 0, n, bytes = strlen(text);
	int j, k, width;

	search.len = search.found = search.refresh = search.count = 0;
	while (i < bytes && (size_t)search.len < LEN(search.query)) {
		n = utf8decode(text + i, &u, bytes - i);
		if (!n)
			break;
		i += n;
		/* Like tputc()/tcombine(), only merge zero-width characters
		 * when the composed form preserves the previous cell's width.
		 * Unknown-width runes also occupy a cell in terminal output. */
		width = wcwidth(u);
		if (width != 0)
			search.query[search.len++] = u;
		else if (search.len &&
		         (composed = compose(search.query[search.len - 1], u)) &&
		         wcwidth(composed) == wcwidth(search.query[search.len - 1]))
			search.query[search.len - 1] = composed;
	}
	for (j = 1, k = 0; j < search.len; j++) {
		while (k && search.query[j] != search.query[k])
			k = search.failure[k - 1];
		if (search.query[j] == search.query[k])
			k++;
		search.failure[j] = k;
	}
	search.failure[0] = 0;
	searchview(search.originlive ? sb.base + sb_maxoffset() : search.origin);
	tsearchnext(-1);
	tfulldirt();
}

/* How many matches the last scan found, and in *index which of them,
 * counting from the top of the history, is the current one. A negative
 * count means output changed the results and navigation must recount. */
int
tsearchcount(int *index)
{
	*index = search.found ? search.index : 0;
	return search.found || search.count < 0 ? search.count : 0;
}

/* SEARCH_CURRENT if the cell in view at x, y is in the current match,
 * SEARCH_OTHER if it is in another one in view, otherwise 0. */
int
tsearchmatch(int x, int y)
{
	SearchPos p = { searchtop() + y, x };

	if (!search.active)
		return 0;
	if (search.found && searchcmp(p, search.start) >= 0 &&
	    searchcmp(p, search.end) <= 0)
		return SEARCH_CURRENT;
	if (search.marks && y >= 0 && y < search.markrows && x >= 0 &&
	    x < search.markcols && search.marks[y * search.markcols + x])
		return SEARCH_OTHER;
	return 0;
}

/* Mark the cells of every match in view for the next frame, scanning only
 * the rows in view and the soft-wrapped rows around them that a match
 * crossing an edge could reach. Rows whose marks change are redrawn,
 * including ones output did not touch, e.g. when a match is completed on
 * the row below. */
static void
searchmarks(void)
{
	struct { int y, x, wide; } cells[SEARCH_MAX];
	Line line;
	size_t size = (size_t)term.row * term.col;
	uchar *swap;
	int reach, first, last, width, y, x, i, matched = 0, slot = 0;

	/* Outside search, there is only something to do once, to clear. */
	if (!(search.active && search.len) && !search.marked)
		return;
	if (search.markrows != term.row || search.markcols != term.col) {
		free(search.marks);
		free(search.newmarks);
		search.marks = xmalloc(size);
		search.newmarks = xmalloc(size);
		memset(search.marks, 0, size);
		search.markrows = term.row;
		search.markcols = term.col;
		tfulldirt();
	}
	memset(search.newmarks, 0, size);
	search.marked = search.active && search.len;
	if (search.marked) {
		/* A wide character takes two cells for one in the query. */
		reach = (2 * search.len + term.col - 1) / term.col;
		for (first = 0; first > -reach && (line = viewline(first - 1)) &&
		     (line[term.col - 1].mode & ATTR_WRAP); first--)
			;
		for (last = term.row - 1; last < term.row - 1 + reach &&
		     (line = viewline(last)) && (line[term.col - 1].mode & ATTR_WRAP) &&
		     viewline(last + 1); last++)
			;
		for (y = first; y <= last; y++) {
			if (!(line = viewline(y)))
				continue;
			width = tlinelen(line);
			for (x = 0; x < width; x++) {
				if (line[x].mode & (ATTR_WDUMMY | ATTR_WPAD))
					continue;
				cells[slot].y = y;
				cells[slot].x = x;
				cells[slot].wide = (line[x].mode & ATTR_WIDE) && x + 1 < term.col;
				slot = (slot + 1) % search.len;
				if (!searchfeed(line[x].u, &matched))
					continue;
				/* The ring holds just the match's cells. */
				for (i = 0; i < search.len; i++) {
					if (cells[i].y < 0 || cells[i].y >= term.row)
						continue;
					if (!search.found)
						search.count = -1;
					search.newmarks[cells[i].y * term.col + cells[i].x] = 1;
					if (cells[i].wide)
						search.newmarks[cells[i].y * term.col + cells[i].x + 1] = 1;
				}
			}
			if (!(line[term.col - 1].mode & ATTR_WRAP))
				matched = 0;
		}
	}
	for (y = 0; y < term.row; y++)
		if (memcmp(search.marks + y * term.col, search.newmarks + y * term.col,
		           term.col))
			term.dirty[y] = 1;
	swap = search.marks;
	search.marks = search.newmarks;
	search.newmarks = swap;
}

/* Output can overwrite or evict the chosen text. Check only that match on
 * redraw; rescanning all history on every output frame would stall a busy
 * terminal. Navigation and edits always search the current contents. */
static void
searchvalidate(void)
{
	Line line;
	uint64_t row, top = searchtop();
	int x, i = 0, valid = 1;

	if (!search.active)
		return;
	if (search.refresh) {
		search.refresh = search.found = 0;
		search.origin = top;
		search.originlive = sb.view_offset == 0;
		tsearchnext(-1);
	}
	if (!search.found)
		return;
	if (search.start.row < sb.base ||
	    search.end.row >= sb.base + sb_maxoffset() + term.row)
		valid = 0;
	for (row = search.start.row; valid && row <= search.end.row; row++) {
		line = viewline((int)(row - sb.base) - (sb_maxoffset() - sb.view_offset));
		for (x = row == search.start.row ? search.start.x : 0;
		     x < term.col && (row < search.end.row || x <= search.end.x); x++) {
			if (line[x].mode & (ATTR_WDUMMY | ATTR_WPAD))
				continue;
			if (i >= search.len || line[x].u != search.query[i++]) {
				valid = 0;
				break;
			}
		}
		if (row < search.end.row && !(line[term.col - 1].mode & ATTR_WRAP))
			valid = 0;
	}
	if (!valid || i != search.len) {
		search.found = 0;
		search.count = -1;
		tfulldirt();
	}
}

/* Accept "scheme://", where a scheme is a letter then [A-Za-z0-9+.-]. */
static int
urlscheme(const char *prefix)
{
	const char *s = prefix;

	if (!isalpha((uchar)*s))
		return 0;
	while (isalnum((uchar)*s) || *s == '+' || *s == '-' || *s == '.')
		s++;
	if (strcmp(s, "://"))
		return 0;
	for (s = prefix; *s; s++)
		if (!urlcfg.allowed[(uchar)*s])
			return 0;
	return 1;
}

/* Rebuild the prefix list only when the configured one changes. */
static void
urlconfig(void)
{
	const char *s;
	int p, count;

	for (count = 0; urlprefixes[count]; count++)
		;
	for (p = 0; p < count && p < urlcfg.nseen && urlcfg.seen[p] == urlprefixes[p]; p++)
		;
	if (urlcfg.ready && p == count && count == urlcfg.nseen)
		return;
	if (!urlcfg.ready) {
		for (s = urlchars; *s; s++)
			urlcfg.allowed[(uchar)*s] = 1;
		for (s = ".,;:!?'"; *s; s++)
			urlcfg.trailing[(uchar)*s] = 1;
	}
	urlcfg.seen = xrealloc(urlcfg.seen, (count + 1) * sizeof(*urlcfg.seen));
	urlcfg.prefixes = xrealloc(urlcfg.prefixes, (count + 1) * sizeof(*urlcfg.prefixes));
	urlcfg.lengths = xrealloc(urlcfg.lengths, (count + 1) * sizeof(*urlcfg.lengths));
	urlcfg.count = 0;
	for (p = 0; p < count; p++) {
		urlcfg.seen[p] = urlprefixes[p];
		if (!urlscheme(urlprefixes[p])) {
			fprintf(stderr, "phist: ignoring URL prefix \"%s\"\n", urlprefixes[p]);
			continue;
		}
		urlcfg.prefixes[urlcfg.count] = urlprefixes[p];
		urlcfg.lengths[urlcfg.count++] = strlen(urlprefixes[p]);
	}
	urlcfg.nseen = count;
	urlcfg.ready = 1;
}

/*
 * Find the next URL at or after *cursor. Bodies may balance parentheses and
 * brackets, lose trailing punctuation, and end at most URLTARGET_MAX bytes
 * after their start, so a URL never depends on text further away.
 */
static int
urlmatch(const char *text, size_t length, size_t *cursor, size_t *begin, size_t *finish)
{
	size_t start, end, limit, trimmed, n;
	int p, parens, brackets;
	uchar c;

	urlconfig();
	while (*cursor < length) {
		start = (*cursor)++;
		for (p = 0; p < urlcfg.count; p++) {
			n = urlcfg.lengths[p];
			if (n <= length - start &&
			    tolower((uchar)text[start]) == tolower((uchar)*urlcfg.prefixes[p]) &&
			    !strncasecmp(text + start, urlcfg.prefixes[p], n))
				break;
		}
		if (p == urlcfg.count)
			continue;
		limit = MIN(length, start + URLTARGET_MAX);
		parens = brackets = 0;
		for (end = trimmed = start + n; end < limit; end++) {
			c = text[end];
			if (!urlcfg.allowed[c])
				break;
			parens += (c == '(') - (c == ')');
			brackets += (c == '[') - (c == ']');
			if (parens < 0 || brackets < 0)
				break;
			if (!urlcfg.trailing[c])
				trimmed = end + 1;
		}
		*cursor = trimmed;
		if (trimmed > start + n) {
			*begin = start;
			*finish = trimmed;
			return 1;
		}
	}
	return 0;
}

static int
urlwraps(int y)
{
	Line line = viewline(y);

	return line && line[term.col - 1].mode & ATTR_WRAP;
}

/* Absolute row of a view row and back, as for search positions. */
static int
vimy(uint64_t row)
{
	return (int)(row - sb.base) - (sb_maxoffset() - sb.view_offset);
}

static uint64_t
vimlast(void)
{
	return sb.base + sb_maxoffset() + term.row - 1;
}

static uint64_t
vimadd(uint64_t row, int n)
{
	if (n < 0 && row - sb.base < (uint64_t)-(int64_t)n)
		return sb.base;
	return row + n;
}

/* Follow rows moved by output, including both ends of a visual selection.
 * Clip to the region when text can be lost at either margin. */
static void
vimscrollrows(uint64_t first, uint64_t last, int n, int clip)
{
	SearchPos *points[] = { &vim.pos, &vim.anchor };
	int i;

	if (!vim.mode)
		return;
	for (i = 0; i < (vim.mode > VIM_NORMAL ? 2 : 1); i++) {
		if (!BETWEEN(points[i]->row, first, last))
			continue;
		points[i]->row = vimadd(points[i]->row, n);
		if (clip)
			LIMIT(points[i]->row, first, last);
	}
}

/* Keep the cursor on a character and in view, and the selection on it. */
static void
vimupdate(void)
{
	uint64_t top;

	LIMIT(vim.pos.row, sb.base, vimlast());
	LIMIT(vim.pos.x, 0, term.col - 1);
	top = searchtop();
	if (vim.pos.row < top)
		searchview(vim.pos.row);
	else if (vim.pos.row >= top + term.row)
		searchview(vim.pos.row - term.row + 1);
	if (vim.pos.x > 0 && (viewline(vimy(vim.pos.row))[vim.pos.x].mode & ATTR_WDUMMY))
		vim.pos.x--;
	if (vim.mode > VIM_NORMAL) {
		sel.mode = SEL_READY;
		sel.alt = IS_SET(MODE_ALTSCREEN);
		sel.type = vim.mode == VIM_BLOCK ? SEL_RECTANGULAR : SEL_REGULAR;
		sel.snap = vim.mode == VIM_LINE ? SNAP_LINE : 0;
		sel.ob.x = vim.anchor.x;
		sel.ob.y = vimy(vim.anchor.row);
		sel.oe.x = vim.pos.x;
		sel.oe.y = vimy(vim.pos.row);
		selnormalize();
	}
	tfulldirt();
}

/* Output, edits and history eviction can end a selection beneath the mode.
 * Check before vimupdate(), which would otherwise bring it back. */
static void
vimvalidatesel(void)
{
	if (vim.mode > VIM_NORMAL && (sel.ob.x == -1 || vim.anchor.row < sb.base ||
	    vim.anchor.row > vimlast())) {
		selclear();
		vim.mode = VIM_NORMAL;
	}
}

static void
vimvalidate(void)
{
	uint64_t top;

	if (!vim.mode)
		return;
	vimvalidatesel();
	if (search.active)
		return;
	top = searchtop();
	if (vim.pos.row < top || vim.pos.row >= top + term.row || vim.pos.x >= term.col ||
	    (vim.pos.x > 0 && (viewline(vimy(vim.pos.row))[vim.pos.x].mode & ATTR_WDUMMY)))
		vimupdate();
}

/* Move to the neighboring character. Return 0 at the end of the text, 2
 * after crossing a hard line break and otherwise 1. Only *line is usable
 * afterwards, as decoding another row may reuse the row before it. */
static int
vimstep(SearchPos *p, Line *line, int direction)
{
	int hard = 0;

	do {
		p->x += direction;
		if (BETWEEN(p->x, 0, term.col - 1))
			continue;
		if (direction > 0 ? p->row >= vimlast() : p->row <= sb.base)
			return 0;
		if (direction > 0)
			hard |= !((*line)[term.col - 1].mode & ATTR_WRAP);
		p->row += direction;
		p->x = direction > 0 ? 0 : term.col - 1;
		*line = viewline(vimy(p->row));
		if (direction < 0)
			hard |= !((*line)[term.col - 1].mode & ATTR_WRAP);
	} while (!BETWEEN(p->x, 0, term.col - 1) ||
	         ((*line)[p->x].mode & (ATTR_WDUMMY | ATTR_WPAD)));
	return 1 + hard;
}

/* 0 for blanks and otherwise a word's class, as Vim's w and W see it. */
static int
vimclass(Rune u, int big)
{
	if (!u || u == ' ' || u == '\t')
		return 0;
	return big || u >= 0x80 || isalnum((int)u) || u == '_' ? 1 : 2;
}

/* w and W: the next word's start, staying put if there is none. */
static int
vimwordstart(int big)
{
	SearchPos p = vim.pos;
	Line line = viewline(vimy(p.row));
	int class = vimclass(line[p.x].u, big), next, step;

	while ((step = vimstep(&p, &line, 1))) {
		next = vimclass(line[p.x].u, big);
		if (step == 2)
			class = 0;
		if (next && next != class) {
			vim.pos = p;
			return 1;
		}
		if (!next)
			class = 0;
	}
	return 0;
}

/* e and E going forwards, b and B going backwards: the far end of this or
 * the next word in that direction. */
static int
vimwordend(int direction, int big)
{
	SearchPos p = vim.pos, last;
	Line line = viewline(vimy(p.row));
	int class;

	do {
		if (!vimstep(&p, &line, direction))
			return 0;
	} while (!(class = vimclass(line[p.x].u, big)));
	do
		last = p;
	while (vimstep(&p, &line, direction) == 1 &&
	       vimclass(line[p.x].u, big) == class);
	vim.pos = last;
	return 1;
}

/* f, F, t and T within the logical line. A repeated t or T must get past
 * the character it stopped before. */
static int
vimfind(Rune u, int direction, int till, int repeat)
{
	SearchPos p = vim.pos, last = p;
	Line line = viewline(vimy(p.row));

	if (repeat && till && vimstep(&p, &line, direction) != 1)
		return 0;
	if (repeat && till)
		last = p;
	while (vimstep(&p, &line, direction) == 1) {
		if (line[p.x].u == u) {
			vim.pos = till ? last : p;
			return 1;
		}
		last = p;
	}
	return 0;
}

/* The first row of the logical line, or its last. */
static void
vimlogicalline(int direction)
{
	if (direction < 0) {
		while (vim.pos.row > sb.base &&
		       viewline(vimy(vim.pos.row - 1))[term.col - 1].mode & ATTR_WRAP)
			vim.pos.row--;
	} else {
		while (vim.pos.row < vimlast() &&
		       viewline(vimy(vim.pos.row))[term.col - 1].mode & ATTR_WRAP)
			vim.pos.row++;
	}
}

/* The first character on the cursor's row that is not blank. */
static void
vimnonblank(void)
{
	Line line = viewline(vimy(vim.pos.row));
	int len = tlinelen(line);

	for (vim.pos.x = 0; vim.pos.x < len; vim.pos.x++)
		if (vimclass(line[vim.pos.x].u, 1))
			return;
	vim.pos.x = 0;
}

/* Move the view by n rows, and the cursor with it if it moves as well. */
static void
vimscroll(int n, int follow)
{
	uint64_t top;

	searchview(vimadd(searchtop(), n));
	top = searchtop();
	if (follow)
		vim.pos.row = vimadd(vim.pos.row, n);
	LIMIT(vim.pos.row, top, top + term.row - 1);
}

/* n and N, including the full count, in one history scan. */
static void
vimsearchnext(int direction, int n)
{
	if (!search.len)
		return;
	search.active = 1;
	searchscan(direction, vim.pos, 1, n);
	if (search.found)
		vim.pos = search.start;
	search.active = search.found = 0;
}

/* End the mode, returning to the view it started from. */
void
tvimend(void)
{
	if (!vim.mode)
		return;
	if (vim.mode > VIM_NORMAL)
		selclear();
	vim.mode = 0;
	searchview(vim.originlive ? sb.base + sb_maxoffset() : vim.origin);
	tfulldirt();
}

/* Copy like the mouse and a URL click do. The selection stays, as after
 * dragging, until something replaces it. */
static void
vimyank(void)
{
	char *text;
	size_t len;

	vimupdate();
	if ((text = getsel())) {
		/* Whole lines end in a newline, also those filling the width,
		 * where getsel() sees no padding selected past the text. It
		 * leaves room for one on every row. */
		len = strlen(text);
		if (vim.mode == VIM_LINE && (!len || text[len - 1] != '\n')) {
			text[len] = '\n';
			text[len + 1] = '\0';
		}
		xsetsel(text);
		xclipcopy();
	}
	sel.mode = SEL_IDLE;
	vim.mode = VIM_NORMAL;
	tvimend();
}

/* Toggle a visual mode, or change to it from another one. */
static void
vimvisual(int mode)
{
	if (vim.mode == mode) {
		selclear();
		vim.mode = VIM_NORMAL;
		return;
	}
	if (vim.mode == VIM_NORMAL) {
		selclear();
		vim.anchor = vim.pos;
	}
	vim.mode = mode;
}

/* yy and Y count logical lines, wherever the cursor sits in a wrap. */
static void
vimyanklines(int n)
{
	vimvisual(VIM_LINE);
	while (--n > 0) {
		vimlogicalline(1);
		if (vim.pos.row == vimlast())
			break;
		vim.pos.row++;
	}
	vimyank();
}

/* The rest of a multi-key command. */
static void
vimpending(Rune pending, Rune u, int n)
{
	int direction = pending == 'f' || pending == 't' ? 1 : -1;
	int till = pending == 't' || pending == 'T', i;

	switch (pending) {
	case 'g':
		if (u != 'g')
			return;
		vim.pos.row = vimadd(sb.base, n - 1);
		LIMIT(vim.pos.row, sb.base, vimlast());
		vimnonblank();
		break;
	case 'y':
		if (u != 'y')
			return;
		vimyanklines(n);
		return;
	default:
		vim.find = u;
		vim.findkind = pending;
		for (i = 0; i < n && vimfind(u, direction, till, i > 0); i++)
			;
		break;
	}
}

/* Handle a key, ctrl if it is a control key named by its lowercase letter.
 * Return whether the window should open the search bar. */
int
tvimkey(Rune u, int ctrl)
{
	Rune pending = vim.pending;
	int count = vim.count, n = count ? count : 1, i, direction, till;
	int named = BETWEEN(u, VIM_LEFT, VIM_ENTER);
	int half = MAX(1, term.row / 2);
	int pagejump = MIN((int64_t)n * MAX(1, term.row - 2), INT_MAX);
	uint64_t top;

	if (!vim.mode)
		return 0;
	/* Translate after retaining the distinction from typed characters. */
	switch (u) {
	case VIM_LEFT: u = 'h'; break;
	case VIM_DOWN: u = 'j'; break;
	case VIM_UP: u = 'k'; break;
	case VIM_RIGHT: u = 'l'; break;
	case VIM_HOME: u = '0'; break;
	case VIM_END: u = '$'; break;
	case VIM_PAGEUP: u = 'b'; ctrl = 1; break;
	case VIM_PAGEDOWN: u = 'f'; ctrl = 1; break;
	case VIM_ENTER: u = '\r'; break;
	}
	vimvalidate();
	vim.pending = 0;
	if (u == VIM_ESCAPE && (pending || count)) {
		vim.count = 0;
		return 0;
	}
	if (u == VIM_ESCAPE) {
		if (vim.mode > VIM_NORMAL) {
			selclear();
			vim.mode = VIM_NORMAL;
		} else {
			tvimend();
		}
		return 0;
	}
	if (pending && !ctrl && !named) {
		vim.count = 0;
		vimpending(pending, u, n);
		if (vim.mode)
			vimupdate();
		return 0;
	}
	if (!ctrl && !named && (BETWEEN(u, '1', '9') || (u == '0' && count))) {
		vim.count = MIN(count * 10 + (u - '0'), VIM_COUNT_MAX);
		return 0;
	}
	vim.count = 0;
	top = searchtop();

	if (ctrl) {
		switch (u) {
		case 'b': vimscroll(-pagejump, 1); break;
		case 'f': vimscroll(pagejump, 1); break;
		case 'u': vimscroll(-(count ? n : half), 1); break;
		case 'd': vimscroll(count ? n : half, 1); break;
		case 'y': vimscroll(-n, 0); break;
		case 'e': vimscroll(n, 0); break;
		case 'v': vimvisual(VIM_BLOCK); break;
		case 'c':
			tvimend();
			return 0;
		default:
			return 0;
		}
		vimupdate();
		return 0;
	}

	switch (u) {
	case 'h':
		vim.pos.x -= MIN(n, term.col);
		break;
	case 'l':
		for (i = MIN(n, term.col); i > 0 && vim.pos.x < term.col - 1; i--)
			vim.pos.x += 1 + !!(viewline(vimy(vim.pos.row))[vim.pos.x].mode & ATTR_WIDE);
		break;
	case 'j':
		vim.pos.row = vimadd(vim.pos.row, n);
		break;
	case 'k':
		vim.pos.row = vimadd(vim.pos.row, -n);
		break;
	case 'w': case 'W':
		for (; n > 0 && vimwordstart(u == 'W'); n--)
			;
		break;
	case 'e': case 'E':
		for (; n > 0 && vimwordend(1, u == 'E'); n--)
			;
		break;
	case 'b': case 'B':
		for (; n > 0 && vimwordend(-1, u == 'B'); n--)
			;
		break;
	case '0':
		vimlogicalline(-1);
		vim.pos.x = 0;
		break;
	case '^':
		vimlogicalline(-1);
		vimnonblank();
		break;
	case '$':
		vimlogicalline(1);
		vim.pos.x = MAX(0, tlinelen(viewline(vimy(vim.pos.row))) - 1);
		break;
	case 'G':
		if (count) {
			vim.pos.row = vimadd(sb.base, n - 1);
		} else {
			/* The rows below the last output hold nothing to copy. */
			vim.pos.row = vimlast();
			while (vim.pos.row > sb.base &&
			       !tlinelen(viewline(vimy(vim.pos.row))))
				vim.pos.row--;
		}
		LIMIT(vim.pos.row, sb.base, vimlast());
		vimnonblank();
		break;
	case 'H':
		vim.pos.row = top + MIN(n - 1, term.row - 1);
		vimnonblank();
		break;
	case 'M':
		vim.pos.row = top + (term.row - 1) / 2;
		vimnonblank();
		break;
	case 'L':
		vim.pos.row = top + term.row - 1 - MIN(n - 1, term.row - 1);
		vimnonblank();
		break;
	case 'g': case 'f': case 'F': case 't': case 'T':
		vim.pending = u;
		vim.count = count;
		return 0;
	case ';': case ',':
		if (!vim.find)
			break;
		direction = vim.findkind == 'f' || vim.findkind == 't' ? 1 : -1;
		till = vim.findkind == 't' || vim.findkind == 'T';
		for (; n > 0 && vimfind(vim.find, u == ';' ? direction : -direction,
		                        till, 1); n--)
			;
		break;
	case '/':
	case '?':
		vim.searchdir = u == '/' ? 1 : -1;
		return 1;
	case 'n':
		vimsearchnext(vim.searchdir, n);
		break;
	case 'N':
		vimsearchnext(-vim.searchdir, n);
		break;
	case 'v':
		vimvisual(VIM_VISUAL);
		break;
	case 'V':
		vimvisual(VIM_LINE);
		break;
	case 'o':
	case 'O':
		if (vim.mode > VIM_NORMAL) {
			SearchPos swap = vim.pos;

			vim.pos = vim.anchor;
			vim.anchor = swap;
		}
		break;
	case 'y':
	case '\r':
		if (vim.mode > VIM_NORMAL)
			vimyank();
		else if (u == 'y') {
			vim.pending = 'y';
			vim.count = count;
		}
		return 0;
	case 'Y':
		if (vim.mode == VIM_NORMAL) {
			vimyanklines(n);
			return 0;
		} else if (vim.mode == VIM_VISUAL) {
			vim.mode = VIM_LINE;
		}
		vimyank();
		return 0;
	case 'q':
	case 'i':
	case 'a':
		tvimend();
		return 0;
	default:
		return 0;
	}
	vimupdate();
	return 0;
}

void
tvimstart(void)
{
	uint64_t top = searchtop();
	int live = sb.view_offset == 0;

	memset(&vim, 0, sizeof(vim));
	vim.mode = VIM_NORMAL;
	vim.searchdir = 1;
	vim.origin = top;
	vim.originlive = live;
	vim.pos.row = top + (live ? term.c.y : term.row - 1);
	vim.pos.x = live ? term.c.x : 0;
	/* A drag in progress must not end by replacing what gets selected. */
	if (sel.mode != SEL_IDLE)
		selclear();
	vimupdate();
}

int
tvimactive(void)
{
	return vim.mode;
}

/* The view row of the cursor, and whether a cell of the view holds it. */
int
tvimrow(void)
{
	return vimy(vim.pos.row);
}

int
tvimcursor(int x, int y)
{
	return vim.mode && !search.active && x == vim.pos.x && y == vimy(vim.pos.row);
}

/*
 * Scan the view plus URLTARGET_MAX bytes of wrapped context on each side,
 * rounded to whole rows, to include targets crossing the view's edges.
 * Chains of nested URLs hitting the length cap may be segmented differently
 * as the view moves.
 */
static void
urlupdate(void)
{
	char *text;
	size_t *offsets, length = 0, start, end, lo, hi, x;
	int top = 0, bottom = term.row - 1, y, row, id, *swap, i;
	int context = (URLTARGET_MAX + term.col - 1) / term.col;
	Line line;

	if (!urls.dirty || term.col < 1 || term.row < 1)
		return;
	urls.dirty = 0;
	for (i = 0; i < urls.count; i++)
		free(urls.targets[i]);
	urls.count = 0;
	if (urls.cols != term.col || urls.rows != term.row) {
		free(urls.cells);
		free(urls.old);
		urls.cells = xmalloc((size_t)term.row * term.col * sizeof(*urls.cells));
		urls.old = xmalloc((size_t)term.row * term.col * sizeof(*urls.old));
		memset(urls.cells, 0, (size_t)term.row * term.col * sizeof(*urls.cells));
		urls.cols = term.col;
		urls.rows = term.row;
		tfulldirt();
	}
	swap = urls.old;
	urls.old = urls.cells;
	urls.cells = swap;
	memset(urls.cells, 0, (size_t)term.row * term.col * sizeof(*urls.cells));

	while (top > -context && urlwraps(top - 1))
		top--;
	while (bottom < term.row - 1 + context && urlwraps(bottom) && viewline(bottom + 1))
		bottom++;
	text = xmalloc((size_t)(bottom - top + 1) * (term.col + 1));
	offsets = xmalloc((size_t)(bottom - top + 2) * sizeof(*offsets));
	for (y = top; y <= bottom; y++) {
		line = renderline(y);
		offsets[y - top] = length;
		for (x = 0; x < term.col; x++)
			text[length++] = !line[x].link && !(line[x].mode & ATTR_WDUMMY) &&
				BETWEEN(line[x].u, 1, 127) ? line[x].u : ' ';
		if (!(line[term.col - 1].mode & ATTR_WRAP))
			text[length++] = '\n';
	}
	offsets[bottom - top + 1] = length;

	for (x = 0, row = 0; urlmatch(text, length, &x, &start, &end);) {
		while (row < term.row && offsets[row - top + 1] <= start)
			row++;
		id = 0;
		for (y = row; y < term.row && offsets[y - top] < end; y++) {
			lo = MAX(start, offsets[y - top]);
			hi = MIN(end, offsets[y - top] + term.col);
			if (lo >= hi)
				continue;
			if (!id) {
				if (urls.count == urls.capacity) {
					urls.capacity = urls.capacity ? 2 * urls.capacity : 16;
					urls.targets = xrealloc(urls.targets,
					                        urls.capacity * sizeof(*urls.targets));
				}
				urls.targets[urls.count] = xmalloc(end - start + 1);
				memcpy(urls.targets[urls.count], text + start, end - start);
				urls.targets[urls.count][end - start] = '\0';
				id = ++urls.count;
			}
			for (; lo < hi; lo++)
				urls.cells[(size_t)y * term.col + lo - offsets[y - top]] = id;
		}
	}
	free(text);
	free(offsets);

	/* Explicit links override text detection, including spaces and both
	 * halves of wide glyphs. A shared OSC 8 id gets one overlay identity. */
	if (hlinks.used) {
		for (i = 1; i <= hlinks.allocated; i++)
			hlinks.slots[i].overlay = 0;
		for (y = 0; y < term.row; y++) {
			line = renderline(y);
			for (x = 0; x < term.col; x++) {
				if (!(i = line[x].link))
					continue;
				if (!hlinks.slots[i].overlay) {
					if (urls.count == urls.capacity) {
						urls.capacity = urls.capacity ? 2 * urls.capacity : 16;
						urls.targets = xrealloc(urls.targets,
						                        urls.capacity * sizeof(*urls.targets));
					}
					urls.targets[urls.count++] = xstrdup(hlinks.slots[i].uri);
					hlinks.slots[i].overlay = urls.count;
				}
				urls.cells[(size_t)y * term.col + x] = hlinks.slots[i].overlay;
			}
		}
	}

	for (y = 0; y < term.row; y++)
		if (memcmp(urls.cells + (size_t)y * term.col, urls.old + (size_t)y * term.col,
		           term.col * sizeof(*urls.cells)))
			tsetdirt(y, y);
	if (urls.flashtext) {
		id = turl(urls.flashcol, urls.flashrow);
		if (id && !strcmp(urls.targets[id - 1], urls.flashtext))
			urls.flash = id;
		else
			tclearurlflash();
	}
}

int
turl(int col, int row)
{
	urlupdate();
	if (!BETWEEN(col, 0, urls.cols - 1) || !BETWEEN(row, 0, urls.rows - 1))
		return 0;
	return urls.cells[(size_t)row * urls.cols + col];
}

char *
tgeturl(int col, int row)
{
	int id = turl(col, row);

	return id ? xstrdup(urls.targets[id - 1]) : NULL;
}

int
thyperlink(int col, int row)
{
	Line line;
	return BETWEEN(col, 0, term.col - 1) && BETWEEN(row, 0, term.row - 1) &&
	       (line = viewline(row)) && line[col].link;
}

/* Visible bounds of this contiguous label, following soft wraps. */
void
thyperlinkbounds(int *left, int *top, int *right, int *bottom)
{
	Line line, previous;
	unsigned int id;
	int col = *left, row = *top, last, first = 1;

	*right = col;
	*bottom = row;
	if (!thyperlink(col, row))
		return;
	line = viewline(row);
	id = line[col].link;
	for (;;) {
		while (col > 0 && !(line[col].mode & ATTR_LINKSTART) &&
		       line[col - 1].link == id)
			--col;
		if ((line[col].mode & ATTR_LINKSTART) || col || !row ||
		    !(previous = viewline(row - 1)) ||
		    !(previous[term.col - 1].mode & ATTR_WRAP))
			break;
		last = term.col - 1 - !!(previous[term.col - 1].mode & ATTR_WPAD);
		if (last < 0 || previous[last].link != id)
			break;
		--row;
		col = last;
		line = previous;
	}
	*left = *right = col;
	*top = row;
	for (;;) {
		last = term.col - 1 - !!(line[term.col - 1].mode & ATTR_WPAD);
		while (col <= last && line[col].link == id &&
		       (first || !(line[col].mode & ATTR_LINKSTART))) {
			first = 0;
			*left = MIN(*left, col);
			*right = MAX(*right, col);
			col++;
		}
		*bottom = row;
		if (col <= last || !(line[term.col - 1].mode & ATTR_WRAP) ||
		    row + 1 >= term.row || !(line = viewline(row + 1)) ||
		    line[0].link != id || (line[0].mode & ATTR_LINKSTART))
			break;
		row++;
		col = 0;
	}
}

void
tflashurl(int col, int row, const char *text)
{
	int id = turl(col, row);

	if (id && (!text || strcmp(urls.targets[id - 1], text)))
		id = 0;
	free(urls.flashtext);
	urls.flashtext = id ? xstrdup(urls.targets[id - 1]) : NULL;
	urls.flash = id;
	urls.flashcol = col;
	urls.flashrow = row;
	tfulldirt();
}

int
turlflashed(int col, int row)
{
	int id = turl(col, row);

	return id && id == urls.flash;
}

void
tclearurlflash(void)
{
	free(urls.flashtext);
	urls.flashtext = NULL;
	urls.flash = 0;
	tfulldirt();
}

static void
sb_preserve_screen(void)
{
	int y, last = term.row - 1;
	HistoryLine *hl;
	HistoryLine old;
	Line line;
	uint64_t oldstart = sb_view_start();

	/* Avoid adding a screenful of empty rows after every clear. */
	while (last >= 0 && tlinelen(term.line[last]) == 0)
		last--;
	for (y = 0; y <= last; y++)
		sb_push(term.line[y]);
	/* A clear ends the old logical line, even if its last row was wrapped. */
	if (last >= 0 && sb.len > 0) {
		hl = &sb.buf[sb_phys_index(sb.len - 1)];
		line = xmalloc(hl->cols * sizeof(Glyph));
		sb_unpack(hl, line, hl->cols);
		line[hl->cols - 1].mode &= ~ATTR_WRAP;
		old = *hl;
		*hl = sb_pack(line, hl->cols);
		sb_free(&old);
		free(line);
		urls.dirty = 1;
	}

	sb.view_offset = 0;
	/* Selected text moved to history stays selected; the clear drops the rest. */
	selscrollback(sb_startdelta(oldstart, sb_view_start()));
	tclearurlflash();
	sb_view_changed();
}

ssize_t
xwrite(int fd, const char *s, size_t len)
{
	size_t aux = len;
	ssize_t r;

	while (len > 0) {
		r = write(fd, s, len);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return r;
		}
		len -= r;
		s += r;
	}

	return aux;
}

void *
xmalloc(size_t len)
{
	void *p;

	if (!(p = malloc(len)))
		die("malloc: %s\n", strerror(errno));

	return p;
}

void *
xrealloc(void *p, size_t len)
{
	if ((p = realloc(p, len)) == NULL)
		die("realloc: %s\n", strerror(errno));

	return p;
}

char *
xstrdup(const char *s)
{
	char *p;

	if ((p = strdup(s)) == NULL)
		die("strdup: %s\n", strerror(errno));

	return p;
}

size_t
utf8decode(const char *c, Rune *u, size_t clen)
{
	size_t i, j, len, type;
	Rune udecoded;

	*u = UTF_INVALID;
	if (!clen)
		return 0;
	udecoded = utf8decodebyte(c[0], &len);
	if (!BETWEEN(len, 1, UTF_SIZ))
		return 1;
	for (i = 1, j = 1; i < clen && j < len; ++i, ++j) {
		udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
		if (type != 0)
			return j;
	}
	if (j < len)
		return 0;
	*u = udecoded;
	utf8validate(u, len);

	return len;
}

Rune
utf8decodebyte(char c, size_t *i)
{
	for (*i = 0; *i < LEN(utfmask); ++(*i))
		if (((uchar)c & utfmask[*i]) == utfbyte[*i])
			return (uchar)c & ~utfmask[*i];

	return 0;
}

size_t
utf8encode(Rune u, char *c)
{
	size_t len, i;

	len = utf8validate(&u, 0);
	if (len > UTF_SIZ)
		return 0;

	for (i = len - 1; i != 0; --i) {
		c[i] = utf8encodebyte(u, 0);
		u >>= 6;
	}
	c[0] = utf8encodebyte(u, len);

	return len;
}

char
utf8encodebyte(Rune u, size_t i)
{
	return utfbyte[i] | (u & ~utfmask[i]);
}

size_t
utf8validate(Rune *u, size_t i)
{
	if (!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF))
		*u = UTF_INVALID;
	for (i = 1; *u > utfmax[i]; ++i)
		;

	return i;
}

char
base64dec_getc(const char **src)
{
	while (**src && !isprint((unsigned char)**src))
		(*src)++;
	return **src ? *((*src)++) : '=';  /* emulate padding if string ends */
}

char *
base64dec(const char *src)
{
	size_t in_len = strlen(src);
	char *result, *dst;
	/* Signed: -1 marks padding, also where plain char is unsigned. */
	static const signed char base64_digits[256] = {
		[43] = 62, 0, 0, 0, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
		0, 0, 0, -1, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
		13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0, 0, 0, 0,
		0, 0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
		40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
	};

	if (in_len % 4)
		in_len += 4 - (in_len % 4);
	result = dst = xmalloc(in_len / 4 * 3 + 1);
	while (*src) {
		int a = base64_digits[(unsigned char) base64dec_getc(&src)];
		int b = base64_digits[(unsigned char) base64dec_getc(&src)];
		int c = base64_digits[(unsigned char) base64dec_getc(&src)];
		int d = base64_digits[(unsigned char) base64dec_getc(&src)];

		/* invalid input. 'a' can be -1, e.g. if src is "\n" (c-str) */
		if (a == -1 || b == -1)
			break;

		*dst++ = (a << 2) | ((b & 0x30) >> 4);
		if (c == -1)
			break;
		*dst++ = ((b & 0x0f) << 4) | ((c & 0x3c) >> 2);
		if (d == -1)
			break;
		*dst++ = ((c & 0x03) << 6) | d;
	}
	*dst = '\0';
	return result;
}

void
selinit(void)
{
	char error[256];
	int i, n;

	sel.mode = SEL_IDLE;
	sel.snap = 0;
	sel.ob.x = -1;

	/* Report configuration errors at startup rather than on a later click. */
	if (selpatterns)
		return;
	for (nselpatterns = 0; selectionpatterns[nselpatterns]; nselpatterns++)
		;
	selpatterns = xmalloc(MAX(1, nselpatterns) * sizeof(*selpatterns));
	for (i = 0; i < nselpatterns; i++) {
		n = regcomp(&selpatterns[i], selectionpatterns[i], REG_EXTENDED);
		if (n) {
			regerror(n, &selpatterns[i], error, sizeof(error));
			die("invalid selection pattern %d: %s\n", i, error);
		}
		if (!selpatterns[i].re_nsub)
			die("selection pattern %d needs a capture group\n", i);
	}
}

static int
tlinelen(Line line)
{
	int i = term.col;
	if (line[i - 1].mode & ATTR_WRAP)
		return i;
	while (i > 0 && line[i - 1].u == ' ')
		--i;
	return i;
}

static int
tlinelen_render(int y)
{
	return tlinelen(renderline(y));
}

int
selstart(int col, int row, int snap)
{
	int had = sel.ob.x != -1 && sel.alt == IS_SET(MODE_ALTSCREEN);
	int bx = sel.nb.x, by = sel.nb.y, ex = sel.ne.x, ey = sel.ne.y;

	/* Snapping reads the cell at these coordinates. */
	LIMIT(col, 0, term.col - 1);
	LIMIT(row, 0, term.row - 1);

	selclear();
	sel.mode = SEL_EMPTY;
	sel.type = SEL_REGULAR;
	sel.alt = IS_SET(MODE_ALTSCREEN);
	sel.snap = snap;
	sel.oe.x = sel.ob.x = col;
	sel.oe.y = sel.ob.y = row;
	selnormalize();

	if (sel.snap != 0)
		sel.mode = SEL_READY;
	tsetdirt(sel.nb.y, sel.ne.y);
	/* Report whether this replaced the previous selection with another range. */
	return !had || bx != sel.nb.x || by != sel.nb.y ||
	       ex != sel.ne.x || ey != sel.ne.y;
}

void
selextend(int col, int row, int type, int done)
{
	int oldey, oldex, oldsby, oldsey, oldtype;

	if (sel.mode == SEL_IDLE)
		return;
	if (done && sel.mode == SEL_EMPTY) {
		selclear();
		return;
	}

	LIMIT(col, 0, term.col - 1);
	LIMIT(row, 0, term.row - 1);

	oldey = sel.oe.y;
	oldex = sel.oe.x;
	oldsby = sel.nb.y;
	oldsey = sel.ne.y;
	oldtype = sel.type;

	sel.oe.x = col;
	sel.oe.y = row;
	selnormalize();
	sel.type = type;

	if (oldey != sel.oe.y || oldex != sel.oe.x || oldtype != sel.type || sel.mode == SEL_EMPTY)
		tsetdirt(MIN(sel.nb.y, oldsby), MAX(sel.ne.y, oldsey));

	sel.mode = done ? SEL_IDLE : SEL_READY;
}

void
selnormalize(void)
{
	int i;

	if (sel.type == SEL_REGULAR && sel.ob.y != sel.oe.y) {
		sel.nb.x = sel.ob.y < sel.oe.y ? sel.ob.x : sel.oe.x;
		sel.ne.x = sel.ob.y < sel.oe.y ? sel.oe.x : sel.ob.x;
	} else {
		sel.nb.x = MIN(sel.ob.x, sel.oe.x);
		sel.ne.x = MAX(sel.ob.x, sel.oe.x);
	}
	sel.nb.y = MIN(sel.ob.y, sel.oe.y);
	sel.ne.y = MAX(sel.ob.y, sel.oe.y);

	selsnap(&sel.nb.x, &sel.nb.y, -1);
	selsnap(&sel.ne.x, &sel.ne.y, +1);

	/* expand selection over line breaks */
	if (sel.type == SEL_RECTANGULAR)
		return;
	i = tlinelen_render(sel.nb.y);
	if (i < sel.nb.x)
		sel.nb.x = i;
	if (tlinelen_render(sel.ne.y) <= sel.ne.x)
		sel.ne.x = term.col - 1;
}

int
selected(int x, int y)
{
	if (sel.mode == SEL_EMPTY || sel.ob.x == -1 ||
			sel.alt != IS_SET(MODE_ALTSCREEN))
		return 0;

	if (sel.type == SEL_RECTANGULAR)
		return BETWEEN(y, sel.nb.y, sel.ne.y)
		    && BETWEEN(x, sel.nb.x, sel.ne.x);

	return BETWEEN(y, sel.nb.y, sel.ne.y)
	    && (y != sel.nb.y || x >= sel.nb.x)
	    && (y != sel.ne.y || x <= sel.ne.x);
}

static int
selmatchcmp(const void *a, const void *b)
{
	const SelMatch *ma = a, *mb = b;
	int alen = ma->last - ma->first, blen = mb->last - mb->first;

	if (alen != blen)
		return alen < blen ? -1 : 1;
	return (ma->first > mb->first) - (ma->first < mb->first);
}

/* Row of a cell index counted from the first row, rounding down if negative. */
static int
selrowof(int n)
{
	return n >= 0 ? n / term.col : -((term.col - 1 - n) / term.col);
}

/*
 * Match at most SELCONTEXT cells on either side of the click, mapping UTF-8
 * byte offsets back to cells. Matches touching a truncated end expand to the
 * whole logical line instead of selecting arbitrary fragments.
 */
static void
selregex(int *x, int *y, int direction)
{
	SelMatch *matches;
	regmatch_t match[2];
	Line line;
	char *text;
	int *cells;
	int first = *y, last = *y, row, col, width, n, i, count = 0;
	int snap = sel.snap, bx = *x, by = *y, ex = *x, ey = *y;
	int context = (SELCONTEXT + term.col - 1) / term.col, cuthead, cuttail, cut = 0;
	size_t capacity, length = 0, point = SIZE_MAX, offset, begin, end;
	char utf8[UTF_SIZ];

	while (first > *y - context && (line = viewline(first - 1)) &&
	       line[term.col - 1].mode & ATTR_WRAP)
		first--;
	while (last < *y + context && (line = viewline(last)) &&
	       line[term.col - 1].mode & ATTR_WRAP && viewline(last + 1))
		last++;
	/* Matches running into a cut end would select arbitrary fragments. */
	cuthead = (line = viewline(first - 1)) && line[term.col - 1].mode & ATTR_WRAP;
	cuttail = (line = viewline(last)) && line[term.col - 1].mode & ATTR_WRAP &&
	          viewline(last + 1);

	capacity = (size_t)(last - first + 1) * term.col * UTF_SIZ + 1;
	text = xmalloc(capacity);
	cells = xmalloc(capacity * sizeof(*cells));
	for (row = first; row <= last; row++) {
		line = renderline(row);
		width = row < last ? term.col : tlinelen(line);
		if (row == *y)
			width = MAX(width, *x + 1);
		for (col = 0; col < width; col++) {
			if (line[col].mode & (ATTR_WDUMMY | ATTR_WPAD))
				continue;
			if (row == *y && (col == *x ||
			    ((line[col].mode & ATTR_WIDE) && col + 1 == *x)))
				point = length;
			n = utf8encode(line[col].u ? line[col].u : ' ', utf8);
			for (i = 0; i < n; i++) {
				text[length] = utf8[i];
				cells[length++] = (row - first) * term.col + col;
			}
		}
	}
	text[length] = '\0';

	/* Keep phist's word selection as a candidate, including punctuation/Unicode. */
	matches = xmalloc((nselpatterns + 2) * sizeof(*matches));
	sel.snap = SNAP_WORD;
	selsnap(&bx, &by, -1);
	selsnap(&ex, &ey, +1);
	sel.snap = snap;
	matches[count++] = (SelMatch){ (by - first) * term.col + bx,
	                              (ey - first) * term.col + ex };
	for (i = 0; i < nselpatterns && point != SIZE_MAX; i++) {
		for (offset = 0; offset <= length; ) {
#ifdef REG_STARTEND
			/* Avoid regexec() measuring the remaining text on every call. */
			match[0].rm_so = offset;
			match[0].rm_eo = length;
			if (regexec(&selpatterns[i], text, 2, match,
			            REG_STARTEND | (offset ? REG_NOTBOL : 0)))
				break;
			match[0].rm_so -= offset;
			match[0].rm_eo -= offset;
			match[1].rm_so -= match[1].rm_so >= 0 ? offset : 0;
			match[1].rm_eo -= match[1].rm_eo >= 0 ? offset : 0;
#else
			if (regexec(&selpatterns[i], text + offset, 2, match,
			            offset ? REG_NOTBOL : 0))
				break;
#endif
			if (match[1].rm_so >= 0 && match[1].rm_eo > match[1].rm_so) {
				begin = offset + match[1].rm_so;
				end = offset + match[1].rm_eo;
				if (begin <= point && point < end) {
					if ((cuthead && begin == 0) || (cuttail && end == length)) {
						cut = 1;
						break;
					}
					bx = cells[begin];
					ex = cells[end - 1];
					line = renderline(first + ex / term.col);
					if (line[ex % term.col].mode & ATTR_WIDE)
						ex++;
					matches[count++] = (SelMatch){ bx, ex };
					break;
				}
			}
			if (offset + match[0].rm_so > point)
				break;
			offset += MAX(1, match[0].rm_eo);
		}
	}
	/* A match running past the examined text grows to the whole line. */
	if (cut) {
		bx = ex = *x;
		by = ey = *y;
		sel.snap = SNAP_LINE;
		selsnap(&bx, &by, -1);
		selsnap(&ex, &ey, +1);
		sel.snap = snap;
		/* End at its text, like a "^(.*)$" pattern would. */
		ex = MAX(0, tlinelen_render(ey) - 1);
		matches[count++] = (SelMatch){ (by - first) * term.col + bx,
		                              (ey - first) * term.col + ex };
	}
	qsort(matches, count, sizeof(*matches), selmatchcmp);
	for (i = 0, n = 0; i < count; i++)
		if (!n || matches[i].first != matches[n - 1].first ||
		    matches[i].last != matches[n - 1].last)
			matches[n++] = matches[i];
	i = MIN(snap - SNAP_REGEX, n - 1);
	n = direction < 0 ? matches[i].first : matches[i].last;
	*y = first + selrowof(n);
	*x = n - selrowof(n) * term.col;
	free(matches);
	free(cells);
	free(text);
}

void
selsnap(int *x, int *y, int direction)
{
	int newx, newy, len;
	int delim, prevdelim;
	const Glyph *gp, *prevgp;
	Line line, next;

	if (sel.snap >= SNAP_REGEX) {
		selregex(x, y, direction);
		return;
	}

	switch (sel.snap) {
	case SNAP_WORD:
		/*
		 * Snap around if the word wraps around at the end or
		 * beginning of a line.
		 */
		/* History rows are decoded per call, so fetch each row only once. */
		line = renderline(*y);
		len = tlinelen(line);
		prevgp = &line[*x];
		prevdelim = ISDELIM(prevgp->u);
		for (;;) {
			newx = *x + direction;
			newy = *y;
			if (!BETWEEN(newx, 0, term.col - 1)) {
				newy += direction;
				newx = (newx + term.col) % term.col;
				/* The wrap flag ends the upper of the two rows. */
				if (!(next = viewline(newy)) ||
				    !((direction > 0 ? line : next)[term.col - 1].mode & ATTR_WRAP))
					break;
				line = next;
				len = tlinelen(line);
			}

			if (newx >= len)
				break;

			gp = &line[newx];
			delim = ISDELIM(gp->u);
			if (!(gp->mode & (ATTR_WDUMMY | ATTR_WPAD)) && (delim != prevdelim
					|| (delim && gp->u != prevgp->u)))
				break;

			*x = newx;
			*y = newy;
			if (!(gp->mode & ATTR_WPAD)) {
				prevgp = gp;
				prevdelim = delim;
			}
		}
		break;
	case SNAP_LINE:
		/*
		 * Snap around if the the previous line or the current one
		 * has set ATTR_WRAP at its end. Then the whole next or
		 * previous line will be selected.
		 */
		*x = (direction < 0) ? 0 : term.col - 1;
		if (direction < 0) {
			for (; viewline(*y - 1); *y += direction) {
				if (!(renderline(*y-1)[term.col-1].mode
						& ATTR_WRAP)) {
					break;
				}
			}
		} else if (direction > 0) {
			for (; viewline(*y + 1); *y += direction) {
				if (!(renderline(*y)[term.col-1].mode
						& ATTR_WRAP)) {
					break;
				}
			}
		}
		break;
	}
}

/* Maximum buffer size for whole-line selections, or 0 on size_t overflow. */
static size_t
selbufsize(int cols, int lines)
{
	size_t perline = ((size_t)cols + 1) * UTF_SIZ;

	return (size_t)lines > SIZE_MAX / perline ? 0 : lines * perline;
}

char *
getsel(void)
{
	char *str, *ptr;
	size_t bufsize;
	int y, lastx, linelen, end_idx, insert_newline, is_wrapped;
	const Glyph *gp, *last;
	Line line;

	if (sel.ob.x == -1)
		return NULL;

	if (!(bufsize = selbufsize(term.col, sel.ne.y - sel.nb.y + 1)))
		return NULL;
	ptr = str = xmalloc(bufsize);

	/* append every set & selected glyph to the selection */
	for (y = sel.nb.y; y <= sel.ne.y; y++) {
		line = renderline(y);
		linelen = tlinelen_render(y);

		if (linelen == 0) {
			*ptr++ = '\n';
			continue;
		}

		if (sel.type == SEL_RECTANGULAR) {
			gp = &line[sel.nb.x];
			lastx = sel.ne.x;
		} else {
			gp = &line[sel.nb.y == y ? sel.nb.x : 0];
			lastx = (sel.ne.y == y) ? sel.ne.x : term.col-1;
		}
		end_idx = MIN(lastx, linelen-1);
		is_wrapped = (line[end_idx].mode & ATTR_WRAP) != 0;
		last = &line[end_idx];
		while (last >= gp && last->u == ' ' &&
		       (!is_wrapped || sel.type == SEL_RECTANGULAR)) {
			--last;
		}

		for ( ; gp <= last; ++gp) {
			if (gp->mode & (ATTR_WDUMMY | ATTR_WPAD))
				continue;
			ptr += utf8encode(gp->u, ptr);
		}
		/*
		 * Copy and pasting of line endings is inconsistent
		 * in the inconsistent terminal and GUI world.
		 * The best solution seems like to produce '\n' when
		 * something is copied from st and convert '\n' to
		 * '\r', when something to be pasted is received by
		 * st.
		 * FIXME: Fix the computer world.
		 */
		insert_newline = 0;
		if ((y < sel.ne.y || lastx >= linelen) &&
			(!is_wrapped || sel.type == SEL_RECTANGULAR)) {
			insert_newline = 1;
		}

		if (insert_newline)
			*ptr++ = '\n';
	}
	*ptr = 0;
	return str;
}

void
selclear(void)
{
	if (sel.ob.x == -1)
		return;
	sel.mode = SEL_IDLE;
	sel.ob.x = -1;
	tsetdirt(sel.nb.y, sel.ne.y);
}

void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

void
execsh(char *cmd, char **args)
{
	char *sh, *prog, *arg;
	const struct passwd *pw;

	errno = 0;
	if ((pw = getpwuid(getuid())) == NULL) {
		if (errno)
			die("getpwuid: %s\n", strerror(errno));
		else
			die("who are you?\n");
	}

	if ((sh = getenv("SHELL")) == NULL)
		sh = (pw->pw_shell[0]) ? pw->pw_shell : cmd;

	if (args) {
		prog = args[0];
		arg = NULL;
	} else if (scroll) {
		prog = scroll;
		arg = utmp ? utmp : sh;
	} else if (utmp) {
		prog = utmp;
		arg = NULL;
	} else {
		prog = sh;
		arg = NULL;
	}
	DEFAULT(args, ((char *[]) {prog, arg, NULL}));

	unsetenv("COLUMNS");
	unsetenv("LINES");
	unsetenv("TERMCAP");
	setenv("LOGNAME", pw->pw_name, 1);
	setenv("USER", pw->pw_name, 1);
	setenv("SHELL", sh, 1);
	setenv("HOME", pw->pw_dir, 1);
	setenv("TERM", termname, 1);

	signal(SIGCHLD, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGALRM, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);

	execvp(prog, args);
	_exit(1);
}

static volatile sig_atomic_t shelldone, shellstatus;
static int shellwake[2] = { -1, -1 };

static void
exitlikeshell(int stat)
{
	if ((WIFEXITED(stat) && WEXITSTATUS(stat)) || WIFSIGNALED(stat))
		_exit(1);
	_exit(0);
}

static void
shellexitnow(int a)
{
	exitlikeshell(shellstatus);
}

/*
 * The main loop finishes up, as the shell's last output is still in the pty and
 * this can't run the parser. The pipe wakes it if it was just about to wait. It
 * may be stuck instead, as in Xlib with the X server stopped, which must not
 * keep phist around, so the alarm ends things after the time finishing can take.
 */
void
sigchld(int a)
{
	struct sigaction sa = { .sa_handler = shellexitnow };
	int stat, olderrno;
	pid_t p;

	if (shelldone)
		return;
	olderrno = errno;
	do {
		p = waitpid(pid, &stat, WNOHANG);
	} while (p < 0 && errno == EINTR);

	if (p < 0)
		_exit(1);

	if (pid == p) {
		shellstatus = stat;
		shelldone = 1;
		if (write(shellwake[1], "", 1) < 0)
			; /* one byte in an empty pipe, and the flag is set anyway */
		sigemptyset(&sa.sa_mask);
		sigaction(SIGALRM, &sa, NULL);
		alarm(SHELL_FINISH_MAX);
	}
	errno = olderrno;
}

/* Run stty_args, split at blanks, and args without a shell to reparse them. */
void
stty(char **args)
{
	char *cmd, *s, **argv, **p;
	size_t n = 0;
	int status;
	pid_t child;

	for (p = args; p && *p; ++p)
		n++;
	cmd = xstrdup(stty_args);
	/* A word needs a blank after it, and there is room for the last one. */
	argv = xmalloc((strlen(cmd) / 2 + 1 + n + 1) * sizeof(*argv));
	n = 0;
	for (s = strtok(cmd, " \t"); s; s = strtok(NULL, " \t"))
		argv[n++] = s;
	if (!n)
		die("incorrect stty parameters\n");
	for (p = args; p && *p; ++p)
		argv[n++] = *p;
	argv[n] = NULL;

	switch (child = fork()) {
	case -1:
		perror("Couldn't call stty");
		break;
	case 0:
		signal(SIGPIPE, SIG_DFL);
		execvp(argv[0], argv);
		perror("Couldn't call stty");
		_exit(127);
	default:
		while (waitpid(child, &status, 0) < 0) {
			if (errno != EINTR) {
				perror("Couldn't call stty");
				status = 0;
				break;
			}
		}
		if (status)
			fprintf(stderr, "stty failed\n");
		break;
	}
	free(argv);
	free(cmd);
}

int
ttynew(const char *line, char *cmd, const char *out, char **args, const char *dir)
{
	sigset_t chld, mask;
	int m, s;

	/* An inherited SIG_IGN would prevent waiting for the child's status. */
	signal(SIGCHLD, SIG_DFL);

	/* A reader of -o or of stderr going away doesn't end the terminal. */
	signal(SIGPIPE, SIG_IGN);

	if (out) {
		term.mode |= MODE_PRINT;
		mcallowed = 1;
		printopen(out);
	}

	if (line) {
		if ((cmdfd = open(line, O_RDWR)) < 0)
			die("open line '%s' failed: %s\n",
			    line, strerror(errno));
		if (dir && chdir(dir) < 0)
			die("chdir '%s': %s\n", dir, strerror(errno));
		dup2(cmdfd, 0);
		stty(args);
		return cmdfd;
	}

	/* seems to work fine on linux, openbsd and freebsd */
	if (openpty(&m, &s, NULL, NULL, NULL) < 0)
		die("openpty failed: %s\n", strerror(errno));

	/*
	 * Install the handler before forking, so a shell exiting at once isn't
	 * missed, and block SIGCHLD until fork() has returned, so the handler
	 * knows which child is the shell.
	 */
	if (pipe(shellwake) < 0)
		die("pipe failed: %s\n", strerror(errno));
	fcntl(shellwake[0], F_SETFD, FD_CLOEXEC);
	fcntl(shellwake[1], F_SETFD, FD_CLOEXEC);
	fcntl(shellwake[1], F_SETFL, O_NONBLOCK);
	sigemptyset(&chld);
	sigaddset(&chld, SIGCHLD);
	sigprocmask(SIG_BLOCK, &chld, &mask);
	signal(SIGCHLD, sigchld);

	switch (pid = fork()) {
	case -1:
		die("fork failed: %s\n", strerror(errno));
		break;
	case 0:
		signal(SIGCHLD, SIG_DFL);
		/* The shell gets what phist was started with, minus SIGCHLD. */
		sigdelset(&mask, SIGCHLD);
		sigprocmask(SIG_SETMASK, &mask, NULL);
		close(iofd);
		close(m);
		setsid(); /* create a new process group */
		dup2(s, 0);
		dup2(s, 1);
		dup2(s, 2);
		if (ioctl(s, TIOCSCTTY, NULL) < 0)
			die("ioctl TIOCSCTTY failed: %s\n", strerror(errno));
		if (s > 2)
			close(s);
		/* Resolve phist's output and device paths in the caller's directory. */
		if (dir && chdir(dir) < 0)
			die("chdir '%s': %s\n", dir, strerror(errno));
#ifdef __OpenBSD__
		if (pledge("stdio getpw proc exec", NULL) == -1)
			die("pledge\n");
#endif
		execsh(cmd, args);
		break;
	default:
#ifdef __OpenBSD__
		if (pledge("stdio rpath tty proc", NULL) == -1)
			die("pledge\n");
#endif
		close(s);
		cmdfd = m;
		/* Here and not in sigchld(), whose mask ends with it: SIGALRM has to
		 * get through for a stuck main loop not to keep phist around. */
		sigaddset(&chld, SIGALRM);
		sigprocmask(SIG_UNBLOCK, &chld, NULL);
		break;
	}
	return cmdfd;
}

static char ttyin[BUFSIZ];
static int ttyinlen;

/* Read once and parse what it gave, keeping an incomplete UTF-8 sequence. */
static ssize_t
ttyreadsome(void)
{
	ssize_t ret;
	int written;

	/* append read bytes to unprocessed bytes */
	ret = read(cmdfd, ttyin + ttyinlen, LEN(ttyin) - ttyinlen);
	if (ret <= 0)
		return ret;
	ttyinlen += ret;
	written = twrite(ttyin, ttyinlen, 0);
	ttyinlen -= written;
	ttyflushprint();
	/* keep any incomplete UTF-8 byte sequence for the next call */
	if (ttyinlen > 0)
		memmove(ttyin, ttyin + written, ttyinlen);
	return ret;
}

/*
 * The milliseconds left of a second since start, as a job that outlives the
 * shell can keep writing to the pty, and a stalled reader can keep the queue
 * waiting.
 */
static int
ttyfinishleft(const struct timespec *start)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return MAX(0, 1000 - (int)MIN(TIMEDIFF(now, (*start)), 1000));
}

/* The shell is gone: take in its last output, and pass it on, before exiting. */
static void
ttyfinish(void)
{
	struct pollfd fd = { .fd = cmdfd, .events = POLLIN };
	struct timespec start;

	clock_gettime(CLOCK_MONOTONIC, &start);
	while (ttyfinishleft(&start) && poll(&fd, 1, 0) > 0 &&
	       (fd.revents & POLLIN) && ttyreadsome() > 0)
		;
	clock_gettime(CLOCK_MONOTONIC, &start);
	fd.events = POLLOUT;
	for (;;) {
		ttyflushprint();
		fd.fd = ttyprintfd();
		if (fd.fd < 0 || !ttyfinishleft(&start))
			break;
		poll(&fd, 1, ttyfinishleft(&start));
	}
	exitlikeshell(shellstatus);
}

int
ttywakefd(void)
{
	return shellwake[0];
}

/* Finish, without returning, if sigchld() has seen the shell exit. */
void
ttycheckshell(void)
{
	if (shelldone)
		ttyfinish();
}

/*
 * The shell's end of the pty closes just before it counts as exited, so EIO
 * usually comes first. Give sigchld() a second to see the exit. A shell still
 * running has only closed the terminal, and the read error is reported.
 */
static void
ttywaitshell(void)
{
	struct pollfd fd = { .fd = shellwake[0], .events = POLLIN };
	struct timespec start;

	clock_gettime(CLOCK_MONOTONIC, &start);
	while (!shelldone && ttyfinishleft(&start) &&
	       (poll(&fd, 1, ttyfinishleft(&start)) == 0 || errno == EINTR) && !fd.revents)
		;
	ttycheckshell();
	errno = EIO;
}

size_t
ttyread(void)
{
	ssize_t ret;

	/* Only an incomplete UTF-8 sequence is kept, so there is always room. */
	if (ttyinlen >= LEN(ttyin))
		return 0;
	ret = ttyreadsome();

	switch (ret) {
	case 0:
		exit(0);
	case -1:
		if (errno == EIO && pid > 0)
			ttywaitshell();
		die("couldn't read from shell: %s\n", strerror(errno));
	default:
		return ret;
	}
}

/* Queue query replies instead of writing from the parser: a blocking write
 * could reenter ttyread() and reparse its buffer. Drop replies that do not fit. */
static struct {
	char buf[REPLY_MAX];
	size_t len;
} replies;

/* Queue keys and pastes for the event loop so a non-reading child cannot
 * block drawing or output processing. Preserve order with earlier input. */
static struct {
	char *buf;
	size_t off, len, cap; /* len bytes from off wait to be written */
} input;

static void
ttyqueue(const char *s, size_t n)
{
	if (!n)
		return;
	if (n > input.cap - input.off - input.len) {
		if (input.off) {
			memmove(input.buf, input.buf + input.off, input.len);
			input.off = 0;
		}
		if (n > input.cap - input.len) {
			input.cap = MAX(input.len + n, MAX(BUFSIZ, input.cap * 2));
			input.buf = xrealloc(input.buf, input.cap);
		}
	}
	memcpy(input.buf + input.off + input.len, s, n);
	input.len += n;
}

/*
 * Whether n more bytes of user input may be queued. An empty queue takes any
 * amount, leaving the size of a single paste to its own limit. Otherwise a
 * child that stopped reading would have phist buffer pastes without end.
 */
int
ttyinputfits(size_t n)
{
	ttyflushwrites();
	if (IS_SET(MODE_CRLF))
		n *= 2; /* each CR may be followed by LF */
	return !n || !input.len || (input.len <= INPUT_MAX && n <= INPUT_MAX - input.len);
}

/* Keys are single actions, so one that does not fit is lost on its own. */
void
ttyinput(const char *s, size_t n, int may_echo)
{
	if (!ttyinputfits(n)) {
		erresc("erresc: input dropped, child is not reading "
		       "(%zu bytes pending)\n", input.len);
		return;
	}
	ttypaste(s, n, may_echo);
}

/* Queue input already found to fit, which may be one paste in several parts. */
void
ttypaste(const char *s, size_t n, int may_echo)
{
	uint64_t oldstart = sb_view_start();

	if (n && sb.view_offset > 0) {
		sb.view_offset = 0;
		/* Like other view changes, keep a selection with its text. */
		selscrollback(sb_startdelta(oldstart, sb_view_start()));
		sb_view_changed();
	}
	ttywrite(s, n, may_echo);
}

void
ttywrite(const char *s, size_t n, int may_echo)
{
	const char *next;

	if (may_echo && IS_SET(MODE_ECHO))
		twrite(s, n, 1);

	/* Pending replies precede this input; replies generated later follow it.
	 * Neither can split the other, and query floods cannot hold input back. */
	if (replies.len) {
		ttyqueue(replies.buf, replies.len);
		replies.len = 0;
	}
	if (!IS_SET(MODE_CRLF)) {
		ttyqueue(s, n);
	} else while (n > 0) {
		/* This is similar to how the kernel handles ONLCR for ttys */
		if (*s == '\r') {
			next = s + 1;
			ttyqueue("\r\n", 2);
		} else {
			next = memchr(s, '\r', n);
			DEFAULT(next, s + n);
			ttyqueue(s, next - s);
		}
		n -= next - s;
		s = next;
	}
	ttyflushwrites();
}

void
ttyreply(const char *s, size_t n)
{
	if (n > sizeof(replies.buf) - replies.len) {
		erresc("erresc: reply dropped, child is not reading "
		       "(%zu bytes pending)\n", replies.len);
		return;
	}
	memcpy(replies.buf + replies.len, s, n);
	replies.len += n;
	ttyflushwrites();
}

int
ttywritepending(void)
{
	return input.len > 0 || replies.len > 0;
}

/* Write what the child takes now of n bytes, without blocking. Return
 * how many it took. */
static size_t
ttywritesome(const char *s, size_t n)
{
	ssize_t r;
	int flags, saved;

	/* Require nonblocking mode before writing, and preserve the original flags. */
	if ((flags = fcntl(cmdfd, F_GETFL)) < 0 ||
	    fcntl(cmdfd, F_SETFL, flags | O_NONBLOCK) < 0)
		die("couldn't make tty nonblocking: %s\n", strerror(errno));
	r = write(cmdfd, s, n);
	saved = errno;
	if (fcntl(cmdfd, F_SETFL, flags) < 0)
		die("couldn't restore tty flags: %s\n", strerror(errno));
	if (r < 0) {
		if (saved == EAGAIN || saved == EWOULDBLOCK || saved == EINTR)
			return 0;
		/* Should the shell's end closing make this fail, which Linux doesn't
		 * do today, reading the pty finds that out after what is left there. */
		if (saved == EIO && pid > 0)
			return 0;
		die("write error on tty: %s\n", strerror(saved));
	}
	return r;
}

/* Write queued input, then replies once all input is written. */
void
ttyflushwrites(void)
{
	size_t r;

	while (input.len && (r = ttywritesome(input.buf + input.off, input.len))) {
		input.off += r;
		input.len -= r;
	}
	if (!input.len) {
		input.off = 0;
		if (input.cap > 1 << 20) {
			/* Give back the memory of a large paste. */
			free(input.buf);
			input.buf = NULL;
			input.cap = 0;
		}
	}
	if (!input.len && replies.len && (r = ttywritesome(replies.buf, replies.len))) {
		replies.len -= r;
		memmove(replies.buf, replies.buf + r, replies.len);
	}
}

void
ttyresize(int tw, int th)
{
	struct winsize w;

	w.ws_row = term.row;
	w.ws_col = term.col;
	w.ws_xpixel = tw;
	w.ws_ypixel = th;
	if (ioctl(cmdfd, TIOCSWINSZ, &w) < 0)
		fprintf(stderr, "Couldn't set window size: %s\n", strerror(errno));
}

void
ttyhangup(void)
{
	/* Send SIGHUP to shell */
	kill(pid, SIGHUP);
}

int
tattrset(int attr)
{
	int i, j;
	Line line;

	for (i = 0; i < term.row; i++) {
		line = renderline(i);
		for (j = 0; j < term.col; j++) {
			if (line[j].mode & attr)
				return 1;
		}
	}

	return 0;
}

void
tsetdirt(int top, int bot)
{
	int i;

	if (term.row < 1)
		return;
	LIMIT(top, 0, term.row-1);
	LIMIT(bot, 0, term.row-1);

	for (i = top; i <= bot; i++)
		term.dirty[i] = 1;
}

static int
screenrow(int y)
{
	return y + sb.view_offset;
}

static void
tdirtyscreen(int top, int bot)
{
	top = screenrow(top);
	bot = screenrow(bot);
	if (top < term.row && bot >= 0)
		tsetdirt(top, bot);
}

void
tsetdirtattr(int attr)
{
	int i, j;
	Line line;

	for (i = 0; i < term.row; i++) {
		line = renderline(i);
		for (j = 0; j < term.col; j++) {
			if (line[j].mode & attr) {
				tsetdirt(i, i);
				break;
			}
		}
	}
}

void
tfulldirt(void)
{
	tsetdirt(0, term.row-1);
}

void
tcursor(int mode)
{
	int alt = IS_SET(MODE_ALTSCREEN);

	if (mode == CURSOR_SAVE) {
		linkref(term.c.attr.link, 1);
		linkref(term.saved[alt].attr.link, -1);
		term.saved[alt] = term.c;
	} else if (mode == CURSOR_LOAD) {
		linkref(term.saved[alt].attr.link, 1);
		linkref(term.c.attr.link, -1);
		term.c = term.saved[alt];
		tmoveto(term.c.x, term.c.y);
	}
}

/* Reset modes, attributes, tabs and both screens, keeping the history. */
void
tresetstate(void)
{
	uint i;

	xsetcwd(NULL);
	linkref(term.c.attr.link, -1);
	term.c = (TCursor){{
		.mode = ATTR_NULL,
		.fg = defaultfg,
		.bg = defaultbg,
		.ucolor = UCOLOR_DEFAULT
	}, .x = 0, .y = 0, .state = CURSOR_DEFAULT};

	memset(term.tabs, 0, term.col * sizeof(*term.tabs));
	for (i = MAX(1, tabspaces); i < term.col; i += MAX(1, tabspaces))
		term.tabs[i] = 1;
	term.top = 0;
	term.bot = term.row - 1;
	term.mode = MODE_WRAP|MODE_UTF8;
	memset(term.trantbl, CS_USA, sizeof(term.trantbl));
	term.charset = 0;

	for (i = 0; i < 2; i++) {
		tmoveto(0, 0);
		tcursor(CURSOR_SAVE);
		if (term.col > 0 && term.row > 0 && term.line)
			tclearregion(0, 0, term.col-1, term.row-1);
		tswapscreen();
	}
}

void
treset(void)
{
	free(search.marks);
	free(search.newmarks);
	memset(&search, 0, sizeof(search));
	memset(&vim, 0, sizeof(vim));
	tresetstate();
	sb_clear();
	if (sel.ob.x != -1 && term.row > 0)
		selclear();
}


void
tnew(int col, int row)
{
	term = (Term){ .c = { .attr = { .fg = defaultfg, .bg = defaultbg } } };
	sb_init(scrollback_lines);
	tresize(col, row);
	treset();
}

void
tswapscreen(void)
{
	Line *tmp = term.line;
	int alt = IS_SET(MODE_ALTSCREEN), top;

	search.refresh = 1;
	/* Absolute rows mean other text on the other screen. */
	tvimend();
	top = sb_maxoffset() - sb.view_offset;

	if (!alt) {
		linkref(term.c.attr.link, 1);
		linkref(term.mainc.attr.link, -1);
		term.mainc = term.c;
	}
	/* Keep showing the same history rows; the main screen moves past them. */
	if (sb.view_offset > 0)
		sb.view_offset = IS_SET(MODE_ALTSCREEN) ?
			MAX(0, sb.view_offset - term.row) : sb.view_offset + term.row;
	urls.dirty = 1;
	term.line = term.alt;
	term.alt = tmp;
	term.mode ^= MODE_ALTSCREEN;
	tfulldirt();

	/*
	 * A selection keeps its rows, which stay visible beneath the alternate
	 * screen; only one on the alternate screen ends with it.
	 */
	if (sel.ob.x != -1 && sel.alt == alt) {
		if (alt && top + sel.ne.y >= sb.len + term.row) {
			selclear();
		} else {
			sel.alt = !alt;
			selscrollback(top - (sb_maxoffset() - sb.view_offset));
		}
	}
}

void
tscrolldown(int orig, int n)
{
	int i;
	Line temp;

	LIMIT(n, 0, term.bot-orig+1);
	if (!n)
		return;
	urls.dirty = 1;
	tdirtyscreen(orig, term.bot);

	for (i = term.bot; i >= orig+n; i--) {
		temp = term.line[i];
		term.line[i] = term.line[i-n];
		term.line[i-n] = temp;
	}

	selscroll(orig, n);
	tclearregion(0, orig, term.col-1, orig+n-1);
}

void
tscrollup(int orig, int n, int save)
{
	int i;
	uint64_t newstart;
	uint64_t oldstart;

	int attop, oldoffset = sb.view_offset;
	int pushed = save && !IS_SET(MODE_ALTSCREEN) && orig == 0 && sb.limit > 0;
	Line temp;

	oldstart = sb_view_start();
	LIMIT(n, 0, term.bot-orig+1);
	if (!n)
		return;

	if (pushed) {
		/* At top of history only if history exists */
		attop = (sb.len != 0 && sb.view_offset == sb.len);

		if (sb.view_offset > 0 && !attop)
			sb.view_offset += n;

		for (i = 0; i < n; i++)
			sb_push(term.line[orig + i]);

		/* if at the top, keep me there */
		if (attop)
			sb.view_offset = sb.len;
		/* otherwise clamp me */
		else if (sb.view_offset > sb.len)
			sb.view_offset = sb.len;
	}

	newstart = sb_view_start();
	urls.dirty = 1;
	if (newstart != oldstart && sb.view_offset > 0)
		tfulldirt();

	tdirtyscreen(orig, term.bot);

	for (i = orig; i <= term.bot-n; i++) {
		temp = term.line[i];
		term.line[i] = term.line[i+n];
		term.line[i+n] = temp;
	}

	if (pushed)
		selpushed(oldoffset, n, sb_startdelta(oldstart, newstart));
	else
		selscroll(orig, -n);
	tclearregion(0, term.bot-n+1, term.col-1, term.bot);
}

void
selscroll(int orig, int n)
{
	/* Screen rows orig..term.bot are these rows of the view. */
	int top = orig + sb.view_offset, bot = term.bot + sb.view_offset;
	uint64_t screen = sb.base + sb_maxoffset();

	vimscrollrows(screen + orig, screen + term.bot, n, 1);

	if (sel.ob.x == -1 || sel.alt != IS_SET(MODE_ALTSCREEN))
		return;

	if (sel.ne.y < top || sel.nb.y > bot)
		return;
	if (sel.nb.y < top || sel.ne.y > bot ||
	    sel.nb.y + n < top || sel.ne.y + n > bot) {
		selclear();
	} else {
		/* The selected text moved intact; do not re-run snapping or regexes. */
		sel.nb.y += n;
		sel.ne.y += n;
		sel.ob.y += n;
		sel.oe.y += n;
	}
}

/*
 * Rows 0..term.bot scrolled up by n into history: their text keeps its
 * place in the history, which the view moved delta rows towards, while rows
 * below a scrolling region stay on screen. oldoffset is the view offset
 * before the scroll.
 */
static void
selpushed(int oldoffset, int n, int delta)
{
	int bot = term.bot + oldoffset;
	uint64_t screen = sb.base + sb.len - n; /* live edge before the push */

	/* Scrolled rows keep their absolute positions; fixed rows below the
	 * region advance with the new live edge. */
	vimscrollrows(screen + term.bot + 1, screen + term.row - 1, n, 0);

	if (sel.ob.x == -1 || sel.alt != IS_SET(MODE_ALTSCREEN))
		return;
	if (sel.nb.y <= bot && sel.ne.y > bot)
		selclear();
	else
		selscrollback(delta + (sel.nb.y > bot ? n : 0));
}

void
tnewline(int first_col)
{
	int y = term.c.y;

	if (y == term.bot) {
		tscrollup(term.top, 1, 1);
	} else {
		y++;
	}
	tmoveto(first_col ? 0 : term.c.x, y);
}

int
readcolonargs(char **p, int cursor, int params[][CAR_PER_ARG])
{
	char *np;
	long value;
	int i;

	for (i = 0; i < CAR_PER_ARG; i++)
		params[cursor][i] = -1;

	i = 0;
	while (**p == ':') {
		(*p)++;
		value = strtol(*p, &np, 10);
		if (i < CAR_PER_ARG)
			params[cursor][i] = np != *p && BETWEEN(value, 0, INT_MAX) ? value : -1;
		i++;
		*p = np;
	}
	return i;
}

void
csiparse(void)
{
	char *p = csiescseq.buf, *np;
	long int v;

	csiescseq.narg = 0;
	if (*p == '?') {
		csiescseq.priv = 1;
		p++;
	}

	csiescseq.buf[csiescseq.len] = '\0';
	while (p < csiescseq.buf+csiescseq.len) {
		np = NULL;
		v = strtol(p, &np, 10);
		if (np == p)
			v = 0;
		/* Keep handler arithmetic like arg[0] - 1 or x + arg[0] in range. */
		if (v < 0)
			v = 0;
		else if (v > 65535)
			v = 65535;
		csiescseq.arg[csiescseq.narg++] = v;
		p = np;
		csiescseq.cargc[csiescseq.narg - 1] =
			readcolonargs(&p, csiescseq.narg - 1, csiescseq.carg);
		if (*p != ';' || csiescseq.narg == ESC_ARG_SIZ)
			break;
		p++;
	}
	csiescseq.mode[0] = *p++;
	csiescseq.mode[1] = (p < csiescseq.buf+csiescseq.len) ? *p : '\0';
}

/* for absolute user moves, when decom is set */
void
tmoveato(int x, int y)
{
	tmoveto(x, y + ((term.c.state & CURSOR_ORIGIN) ? term.top: 0));
}

void
tmoveto(int x, int y)
{
	int miny, maxy;

	if (term.c.state & CURSOR_ORIGIN) {
		miny = term.top;
		maxy = term.bot;
	} else {
		miny = 0;
		maxy = term.row - 1;
	}
	term.c.state &= ~CURSOR_WRAPNEXT;
	term.c.x = LIMIT(x, 0, term.col-1);
	term.c.y = LIMIT(y, miny, maxy);
}

void
tsetchar(Rune u, const Glyph *attr, int x, int y)
{
	static const char *vt100_0[62] = { /* 0x41 - 0x7e */
		"↑", "↓", "→", "←", "█", "▚", "☃", /* A - G */
		0, 0, 0, 0, 0, 0, 0, 0, /* H - O */
		0, 0, 0, 0, 0, 0, 0, 0, /* P - W */
		0, 0, 0, 0, 0, 0, 0, " ", /* X - _ */
		"◆", "▒", "␉", "␌", "␍", "␊", "°", "±", /* ` - g */
		"␤", "␋", "┘", "┐", "┌", "└", "┼", "⎺", /* h - o */
		"⎻", "─", "⎼", "⎽", "├", "┤", "┴", "┬", /* p - w */
		"│", "≤", "≥", "π", "≠", "£", "·", /* x - ~ */
	};

	/*
	 * The table is proudly stolen from rxvt.
	 */
	if (term.trantbl[term.charset] == CS_GRAPHIC0 &&
	   BETWEEN(u, 0x41, 0x7e) && vt100_0[u - 0x41])
		utf8decode(vt100_0[u - 0x41], &u, UTF_SIZ);

	if (term.line[y][x].mode & ATTR_WIDE) {
		if (x+1 < term.col) {
			term.line[y][x+1].u = ' ';
			term.line[y][x+1].mode &= ~ATTR_WDUMMY;
			linkset(&term.line[y][x+1], 0);
		}
	} else if (term.line[y][x].mode & ATTR_WDUMMY) {
		if (x > 0) {
			term.line[y][x-1].u = ' ';
			term.line[y][x-1].mode &= ~ATTR_WIDE;
			linkset(&term.line[y][x-1], 0);
		}
	}

	tdirtyscreen(y, y);
	linkref(attr->link, 1);
	linkref(term.line[y][x].link, -1);
	term.line[y][x] = *attr;
	term.line[y][x].u = u;
	if (attr == &term.c.attr)
		term.c.attr.mode &= ~ATTR_LINKSTART;
	urls.dirty = 1;
}

void
tclearregion(int x1, int y1, int x2, int y2)
{
	int x, y, temp;
	Glyph *gp;

	if (x1 > x2)
		temp = x1, x1 = x2, x2 = temp;
	if (y1 > y2)
		temp = y1, y1 = y2, y2 = temp;

	LIMIT(x1, 0, term.col-1);
	LIMIT(x2, 0, term.col-1);
	LIMIT(y1, 0, term.row-1);
	LIMIT(y2, 0, term.row-1);

	urls.dirty = 1;
	for (y = y1; y <= y2; y++) {
		tdirtyscreen(y, y);
		for (x = x1; x <= x2; x++) {
			gp = &term.line[y][x];
			if (selected(x, screenrow(y)))
				selclear();
			gp->fg = term.c.attr.fg;
			gp->bg = term.c.attr.bg;
			gp->mode = 0;
			gp->u = ' ';
			linkset(gp, 0);
		}
	}
}

void
tdeletechar(int n)
{
	int dst, src, size, x;
	Glyph *line;

	LIMIT(n, 0, term.col - term.c.x);

	dst = term.c.x;
	src = term.c.x + n;
	size = term.col - src;
	line = term.line[term.c.y];

	/* Keep the surviving suffix of an anonymous label separate from a
	 * preceding label sharing its target, even if its first glyph is cut. */
	if (size && line[src].link)
		for (x = dst; x < src; x++)
			if (line[x].link == line[src].link &&
			    (line[x].mode & ATTR_LINKSTART))
				line[src].mode |= ATTR_LINKSTART;
	linkmove(&line[dst], &line[src], size);
	tclearregion(term.col-n, term.c.y, term.col-1, term.c.y);
}

void
tinsertblank(int n)
{
	int dst, src, size;
	Glyph *line;

	LIMIT(n, 0, term.col - term.c.x);

	dst = term.c.x + n;
	src = term.c.x;
	size = term.col - dst;
	line = term.line[term.c.y];

	linkmove(&line[dst], &line[src], size);
	tclearregion(src, term.c.y, dst - 1, term.c.y);
}

void
tinsertblankline(int n)
{
	if (BETWEEN(term.c.y, term.top, term.bot))
		tscrolldown(term.c.y, n);
}

void
tdeleteline(int n)
{
	if (BETWEEN(term.c.y, term.top, term.bot))
		tscrollup(term.c.y, n, 0);
}

int32_t
tdefcolor(const int *attr, int *npar, int l)
{
	int32_t idx = -1;
	uint r, g, b;
	int params[5], subparam = 0, count, start, j;
	const int *sub;

	/* Keep colon-separated colors separate from subsequent SGR attributes. */
	if ((count = csiescseq.cargc[*npar]) > 0) {
		sub = csiescseq.carg[*npar];
		params[0] = attr[*npar];
		params[1] = sub[0];
		if (sub[0] == 2) {
			if (count == 4)
				start = 1;
			else if (count == 5 && (sub[1] == -1 || sub[1] == 0))
				start = 2;
			else
				return -1;
			for (j = 0; j < 3; j++)
				params[2 + j] = sub[start + j];
		} else if (sub[0] == 5 && count == 2) {
			params[2] = sub[1];
		} else {
			return -1;
		}
		attr = params;
		npar = &subparam;
		l = LEN(params);
	}
	if (*npar + 1 >= l)
		return -1;

	switch (attr[*npar + 1]) {
	case 2: /* direct color in RGB space */
		if (*npar + 4 >= l) {
			erresc(
				"erresc(%d): Incorrect number of parameters (%d)\n",
				attr[*npar], *npar);
			break;
		}
		r = attr[*npar + 2];
		g = attr[*npar + 3];
		b = attr[*npar + 4];
		*npar += 4;
		if (!BETWEEN(r, 0, 255) || !BETWEEN(g, 0, 255) || !BETWEEN(b, 0, 255))
			erresc("erresc: bad rgb color (%u,%u,%u)\n",
				r, g, b);
		else
			idx = TRUECOLOR(r, g, b);
		break;
	case 5: /* indexed color */
		if (*npar + 2 >= l) {
			erresc(
				"erresc(%d): Incorrect number of parameters (%d)\n",
				attr[*npar], *npar);
			break;
		}
		*npar += 2;
		if (!BETWEEN(attr[*npar], 0, 255))
			erresc("erresc: bad fgcolor %d\n", attr[*npar]);
		else
			idx = attr[*npar];
		break;
	case 0: /* implemented defined (only foreground) */
	case 1: /* transparent */
	case 3: /* direct color in CMY space */
	case 4: /* direct color in CMYK space */
	default:
		erresc("erresc(%d): gfx attr %d unknown\n",
		       attr[*npar], attr[*npar + 1]);
		break;
	}

	return idx;
}

void
tsetattr(const int *attr, int l)
{
	int i, style;
	int32_t idx;

	for (i = 0; i < l; i++) {
		switch (attr[i]) {
		case 0:
			term.c.attr.mode &= ~(
				ATTR_BOLD       |
				ATTR_FAINT      |
				ATTR_ITALIC     |
				ATTR_UNDERLINE  |
				ATTR_USTYLE     |
				ATTR_BLINK      |
				ATTR_REVERSE    |
				ATTR_INVISIBLE  |
				ATTR_STRUCK     );
			term.c.attr.fg = defaultfg;
			term.c.attr.bg = defaultbg;
			term.c.attr.ucolor = UCOLOR_DEFAULT;
			break;
		case 1:
			term.c.attr.mode |= ATTR_BOLD;
			break;
		case 2:
			term.c.attr.mode |= ATTR_FAINT;
			break;
		case 3:
			term.c.attr.mode |= ATTR_ITALIC;
			break;
		case 4:
			style = csiescseq.cargc[i] ? csiescseq.carg[i][0] : 1;
			if (!BETWEEN(style, 0, 5))
				break;
			term.c.attr.mode &= ~ATTR_USTYLE;
			term.c.attr.mode |= style << 13;
			MODBIT(term.c.attr.mode, style != 0, ATTR_UNDERLINE);

			break;
		case 5: /* slow blink */
			/* FALLTHROUGH */
		case 6: /* rapid blink */
			term.c.attr.mode |= ATTR_BLINK;
			break;
		case 7:
			term.c.attr.mode |= ATTR_REVERSE;
			break;
		case 8:
			term.c.attr.mode |= ATTR_INVISIBLE;
			break;
		case 9:
			term.c.attr.mode |= ATTR_STRUCK;
			break;
		case 22:
			term.c.attr.mode &= ~(ATTR_BOLD | ATTR_FAINT);
			break;
		case 23:
			term.c.attr.mode &= ~ATTR_ITALIC;
			break;
		case 24:
			term.c.attr.mode &= ~(ATTR_UNDERLINE | ATTR_USTYLE);
			break;
		case 25:
			term.c.attr.mode &= ~ATTR_BLINK;
			break;
		case 27:
			term.c.attr.mode &= ~ATTR_REVERSE;
			break;
		case 28:
			term.c.attr.mode &= ~ATTR_INVISIBLE;
			break;
		case 29:
			term.c.attr.mode &= ~ATTR_STRUCK;
			break;
		case 38:
			if ((idx = tdefcolor(attr, &i, l)) >= 0)
				term.c.attr.fg = idx;
			break;
		case 39: /* set foreground color to default */
			term.c.attr.fg = defaultfg;
			break;
		case 48:
			if ((idx = tdefcolor(attr, &i, l)) >= 0)
				term.c.attr.bg = idx;
			break;
		case 49: /* set background color to default */
			term.c.attr.bg = defaultbg;
			break;
		case 58:
			if ((idx = tdefcolor(attr, &i, l)) >= 0) {
				term.c.attr.ucolor = idx;
			}
			break;
		case 59:
			term.c.attr.ucolor = UCOLOR_DEFAULT;
			break;
		default:
			if (BETWEEN(attr[i], 30, 37)) {
				term.c.attr.fg = attr[i] - 30;
			} else if (BETWEEN(attr[i], 40, 47)) {
				term.c.attr.bg = attr[i] - 40;
			} else if (BETWEEN(attr[i], 90, 97)) {
				term.c.attr.fg = attr[i] - 90 + 8;
			} else if (BETWEEN(attr[i], 100, 107)) {
				term.c.attr.bg = attr[i] - 100 + 8;
			} else {
				erresc(
					"erresc(default): gfx attr %d unknown\n",
					attr[i]);
				csidump();
			}
			break;
		}
	}
}

void
tsetscroll(int t, int b)
{
	int temp;

	LIMIT(t, 0, term.row-1);
	LIMIT(b, 0, term.row-1);
	if (t > b) {
		temp = t;
		t = b;
		b = temp;
	}
	term.top = t;
	term.bot = b;
}

void
tsetmode(int priv, int set, const int *args, int narg)
{
	int alt; const int *lim;

	for (lim = args + narg; args < lim; ++args) {
		if (priv) {
			switch (*args) {
			case 1: /* DECCKM -- Cursor key */
				xsetmode(set, MODE_APPCURSOR);
				break;
			case 5: /* DECSCNM -- Reverse video */
				xsetmode(set, MODE_REVERSE);
				break;
			case 6: /* DECOM -- Origin */
				MODBIT(term.c.state, set, CURSOR_ORIGIN);
				tmoveato(0, 0);
				break;
			case 7: /* DECAWM -- Auto wrap */
				MODBIT(term.mode, set, MODE_WRAP);
				break;
			case 0:  /* Error (IGNORED) */
			case 2:  /* DECANM -- ANSI/VT52 (IGNORED) */
			case 3:  /* DECCOLM -- Column  (IGNORED) */
			case 4:  /* DECSCLM -- Scroll (IGNORED) */
			case 8:  /* DECARM -- Auto repeat (IGNORED) */
			case 18: /* DECPFF -- Printer feed (IGNORED) */
			case 19: /* DECPEX -- Printer extent (IGNORED) */
			case 42: /* DECNRCM -- National characters (IGNORED) */
			case 12: /* att610 -- Start blinking cursor (IGNORED) */
				break;
			case 25: /* DECTCEM -- Text Cursor Enable Mode */
				xsetmode(!set, MODE_HIDE);
				break;
			case 1034: /* 1034: enable 8-bit mode for keyboard input */
				xsetmode(set, MODE_8BIT);
				break;
			case 1049: /* swap screen & set/restore cursor as xterm */
				if (!allowaltscreen)
					break;
				tcursor((set) ? CURSOR_SAVE : CURSOR_LOAD);
				/* FALLTHROUGH */
			case 47: /* swap screen buffer */
			case 1047: /* swap screen buffer */
				if (!allowaltscreen)
					break;
				alt = IS_SET(MODE_ALTSCREEN);
				if (alt) {
					tclearregion(0, 0, term.col-1,
							term.row-1);
				}
				if (set ^ alt) /* set is always 1 or 0 */
					tswapscreen();
				if (*args != 1049)
					break;
				/* FALLTHROUGH */
			case 1048: /* save/restore cursor (like DECSC/DECRC) */
				tcursor((set) ? CURSOR_SAVE : CURSOR_LOAD);
				break;
			case 2004: /* 2004: bracketed paste mode */
				xsetmode(set, MODE_BRCKTPASTE);
				break;
			/* Mouse input belongs to phist; applications cannot enable reporting. */
			case 9:    /* X10 mouse */
			case 1000: /* button reporting */
			case 1001: /* highlight tracking */
			case 1002: /* button-motion reporting */
			case 1003: /* all-motion reporting */
			case 1005: /* UTF-8 coordinates */
			case 1006: /* SGR coordinates */
			case 1007: /* alternate-screen wheel-to-key translation */
			case 1015: /* urxvt coordinates */
			case 1016: /* SGR pixel coordinates */
			/*
			 * Nor focus reports: left enabled by a program that
			 * exits, or by printing a file, they would inject
			 * ESC [ I and ESC [ O into the shell on every focus change.
			 */
			case 1004:
				break;
			default:
				erresc(
					"erresc: unknown private set/reset mode %d\n",
					*args);
				break;
			}
		} else {
			switch (*args) {
			case 0:  /* Error (IGNORED) */
			case 2:  /* KAM -- Keyboard action (IGNORED): would lock out the user */
				break;
			case 4:  /* IRM -- Insertion-replacement */
				MODBIT(term.mode, set, MODE_INSERT);
				break;
			case 12: /* SRM -- Send/Receive (IGNORED): local echo could fake typed input */
				break;
			case 20: /* LNM -- Linefeed/new line */
				MODBIT(term.mode, set, MODE_CRLF);
				break;
			default:
				erresc(
					"erresc: unknown set/reset mode %d\n",
					*args);
				break;
			}
		}
	}
}

void
csihandle(void)
{
	char buf[40];
	int len;

	/* Vim's compatibility probe for CSI sequences with an intermediate. */
	if (csiescseq.len == 3 && !memcmp(csiescseq.buf, "0%m", 3))
		return;

	switch (csiescseq.mode[0]) {
	default:
	unknown:
		erresc("erresc: unknown csi ");
		csidump();
		/* die(""); */
		break;
	case '@': /* ICH -- Insert <n> blank char */
		DEFAULT(csiescseq.arg[0], 1);
		tinsertblank(csiescseq.arg[0]);
		break;
	case 'A': /* CUU -- Cursor <n> Up */
		DEFAULT(csiescseq.arg[0], 1);
		tmoveto(term.c.x, term.c.y-csiescseq.arg[0]);
		break;
	case 'B': /* CUD -- Cursor <n> Down */
	case 'e': /* VPR --Cursor <n> Down */
		DEFAULT(csiescseq.arg[0], 1);
		tmoveto(term.c.x, term.c.y+csiescseq.arg[0]);
		break;
	case 'i': /* MC -- Media Copy */
		/* Without -o, iofd is phist's stdout, possibly another terminal. */
		if (!mcallowed)
			break;
		switch (csiescseq.arg[0]) {
		case 0:
			tdump();
			break;
		case 1:
			tdumpline(term.line[term.c.y]);
			break;
		case 2:
			tdumpsel();
			break;
		case 4:
			term.mode &= ~MODE_PRINT;
			break;
		case 5:
			term.mode |= MODE_PRINT;
			break;
		}
		break;
	case 'c': /* DA -- Device Attributes */
		if (csiescseq.arg[0] == 0)
			ttyreply(vtiden, strlen(vtiden));
		break;
	case 'b': /* REP -- if last char is printable print it <n> more times */
		LIMIT(csiescseq.arg[0], 1, 65535);
		if (term.lastc)
			while (csiescseq.arg[0]-- > 0)
				tputc(term.lastc);
		break;
	case 'C': /* CUF -- Cursor <n> Forward */
	case 'a': /* HPR -- Cursor <n> Forward */
		DEFAULT(csiescseq.arg[0], 1);
		tmoveto(term.c.x+csiescseq.arg[0], term.c.y);
		break;
	case 'D': /* CUB -- Cursor <n> Backward */
		DEFAULT(csiescseq.arg[0], 1);
		tmoveto(term.c.x-csiescseq.arg[0], term.c.y);
		break;
	case 'E': /* CNL -- Cursor <n> Down and first col */
		DEFAULT(csiescseq.arg[0], 1);
		tmoveto(0, term.c.y+csiescseq.arg[0]);
		break;
	case 'F': /* CPL -- Cursor <n> Up and first col */
		DEFAULT(csiescseq.arg[0], 1);
		tmoveto(0, term.c.y-csiescseq.arg[0]);
		break;
	case 'g': /* TBC -- Tabulation clear */
		switch (csiescseq.arg[0]) {
		case 0: /* clear current tab stop */
			term.tabs[term.c.x] = 0;
			break;
		case 3: /* clear all the tabs */
			memset(term.tabs, 0, term.col * sizeof(*term.tabs));
			break;
		default:
			goto unknown;
		}
		break;
	case 'G': /* CHA -- Move to <col> */
	case '`': /* HPA */
		DEFAULT(csiescseq.arg[0], 1);
		tmoveto(csiescseq.arg[0]-1, term.c.y);
		break;
	case 'H': /* CUP -- Move to <row> <col> */
	case 'f': /* HVP */
		DEFAULT(csiescseq.arg[0], 1);
		DEFAULT(csiescseq.arg[1], 1);
		tmoveato(csiescseq.arg[1]-1, csiescseq.arg[0]-1);
		break;
	case 'I': /* CHT -- Cursor Forward Tabulation <n> tab stops */
		DEFAULT(csiescseq.arg[0], 1);
		tputtab(csiescseq.arg[0]);
		break;
	case 'J': /* ED -- Clear screen */
		switch (csiescseq.arg[0]) {
		case 0: /* below */
			if (!IS_SET(MODE_ALTSCREEN) && term.c.x == 0 && term.c.y == 0)
				sb_preserve_screen();
			tclearregion(term.c.x, term.c.y, term.col-1, term.c.y);
			if (term.c.y < term.row-1) {
				tclearregion(0, term.c.y+1, term.col-1,
						term.row-1);
			}
			break;
		case 1: /* above */
			if (term.c.y > 0)
				tclearregion(0, 0, term.col-1, term.c.y-1);
			tclearregion(0, term.c.y, term.c.x, term.c.y);
			break;
		case 2: /* all */
			if (!IS_SET(MODE_ALTSCREEN))
				sb_preserve_screen();
			tclearregion(0, 0, term.col-1, term.row-1);
			break;
		case 3: /* scrollback (IGNORED): would let output erase its own record */
			erresc("erresc: ED 3 (erase scrollback) ignored\n");
			break;
		default:
			goto unknown;
		}
		break;
	case 'K': /* EL -- Clear line */
		switch (csiescseq.arg[0]) {
		case 0: /* right */
			tclearregion(term.c.x, term.c.y, term.col-1,
					term.c.y);
			break;
		case 1: /* left */
			tclearregion(0, term.c.y, term.c.x, term.c.y);
			break;
		case 2: /* all */
			tclearregion(0, term.c.y, term.col-1, term.c.y);
			break;
		}
		break;
	case 'S': /* SU -- Scroll <n> line up */
		if (csiescseq.priv) break;
		DEFAULT(csiescseq.arg[0], 1);
		tscrollup(term.top, csiescseq.arg[0], 0);
		break;
	case 'T': /* SD -- Scroll <n> line down */
		DEFAULT(csiescseq.arg[0], 1);
		tscrolldown(term.top, csiescseq.arg[0]);
		break;
	case 'L': /* IL -- Insert <n> blank lines */
		DEFAULT(csiescseq.arg[0], 1);
		tinsertblankline(csiescseq.arg[0]);
		break;
	case 'l': /* RM -- Reset Mode */
		tsetmode(csiescseq.priv, 0, csiescseq.arg, csiescseq.narg);
		break;
	case 'M': /* DL -- Delete <n> lines */
		DEFAULT(csiescseq.arg[0], 1);
		tdeleteline(csiescseq.arg[0]);
		break;
	case 'X': /* ECH -- Erase <n> char */
		DEFAULT(csiescseq.arg[0], 1);
		tclearregion(term.c.x, term.c.y,
				term.c.x + csiescseq.arg[0] - 1, term.c.y);
		break;
	case 'P': /* DCH -- Delete <n> char */
		DEFAULT(csiescseq.arg[0], 1);
		tdeletechar(csiescseq.arg[0]);
		break;
	case 'Z': /* CBT -- Cursor Backward Tabulation <n> tab stops */
		DEFAULT(csiescseq.arg[0], 1);
		tputtab(-csiescseq.arg[0]);
		break;
	case 'd': /* VPA -- Move to <row> */
		DEFAULT(csiescseq.arg[0], 1);
		tmoveato(term.c.x, csiescseq.arg[0]-1);
		break;
	case 'h': /* SM -- Set terminal mode */
		tsetmode(csiescseq.priv, 1, csiescseq.arg, csiescseq.narg);
		break;
	case 'm': /* SGR -- Terminal attribute (color) */
		tsetattr(csiescseq.arg, csiescseq.narg);
		break;
	case 'n': /* DSR -- Device Status Report */
		switch (csiescseq.arg[0]) {
		case 5: /* Status Report "OK" `0n` */
			ttyreply("\033[0n", sizeof("\033[0n") - 1);
			break;
		case 6: /* Report Cursor Position (CPR) "<row>;<column>R" */
			len = snprintf(buf, sizeof(buf), "\033[%i;%iR",
			               term.c.y+1, term.c.x+1);
			ttyreply(buf, len);
			break;
		default:
			goto unknown;
		}
		break;
	case 'r': /* DECSTBM -- Set Scrolling Region */
		if (csiescseq.priv) {
			goto unknown;
		} else {
			DEFAULT(csiescseq.arg[0], 1);
			DEFAULT(csiescseq.arg[1], term.row);
			tsetscroll(csiescseq.arg[0]-1, csiescseq.arg[1]-1);
			tmoveato(0, 0);
		}
		break;
	case 's': /* DECSC -- Save cursor position (ANSI.SYS) */
		tcursor(CURSOR_SAVE);
		break;
	case 'u': /* DECRC -- Restore cursor position (ANSI.SYS) */
		if (csiescseq.priv) {
			goto unknown;
		} else {
			tcursor(CURSOR_LOAD);
		}
		break;
	case ' ':
		switch (csiescseq.mode[1]) {
		case 'q': /* DECSCUSR -- Set Cursor Style */
			if (xsetcursor(csiescseq.arg[0]))
				goto unknown;
			break;
		default:
			goto unknown;
		}
		break;
	}
}

static struct {
	double tokens;
	struct timespec last;
	unsigned long suppressed;
	int shown;
} errlimit = { .tokens = ERRESC_BURST };

/* Rate-limit input diagnostics to ERRESC_BURST initially, then ERRESC_RATE per
 * second. Report suppressed messages with the next diagnostic. Return whether
 * it was printed so csidump()/strdump() can follow the same limit. */
int
erresc(const char *fmt, ...)
{
	struct timespec now;
	va_list ap;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (errlimit.last.tv_sec || errlimit.last.tv_nsec)
		errlimit.tokens = MIN(ERRESC_BURST, errlimit.tokens +
		                      TIMEDIFF(now, errlimit.last) * ERRESC_RATE / 1000.0);
	errlimit.last = now;
	if (!(errlimit.shown = errlimit.tokens >= 1)) {
		errlimit.suppressed++;
		return 0;
	}
	errlimit.tokens--;
	if (errlimit.suppressed) {
		fprintf(stderr, "erresc: %lu messages suppressed\n",
		        errlimit.suppressed);
		errlimit.suppressed = 0;
	}
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	return 1;
}

void
csidump(void)
{
	size_t i;
	uint c;

	if (!errlimit.shown)
		return;
	fprintf(stderr, "ESC[");
	for (i = 0; i < csiescseq.len && i < DUMP_MAX; i++) {
		c = csiescseq.buf[i] & 0xff;
		if (isprint(c)) {
			putc(c, stderr);
		} else if (c == '\n') {
			fprintf(stderr, "(\\n)");
		} else if (c == '\r') {
			fprintf(stderr, "(\\r)");
		} else if (c == 0x1b) {
			fprintf(stderr, "(\\e)");
		} else {
			fprintf(stderr, "(%02x)", c);
		}
	}
	if (csiescseq.len > DUMP_MAX)
		fprintf(stderr, "...(%zu bytes)", csiescseq.len);
	putc('\n', stderr);
}

void
csireset(void)
{
	memset(&csiescseq, 0, sizeof(csiescseq));
}

void
osc_color_response(int num, int index, int is_osc4)
{
	int n;
	char buf[32];
	unsigned char r, g, b;

	if (xgetcolor(is_osc4 ? num : index, &r, &g, &b)) {
		erresc("erresc: failed to fetch %s color %d\n",
		       is_osc4 ? "osc4" : "osc",
		       is_osc4 ? num : index);
		return;
	}

	n = snprintf(buf, sizeof buf, "\033]%s%d;rgb:%02x%02x/%02x%02x/%02x%02x\007",
	             is_osc4 ? "4;" : "", num, r, r, g, g, b, b);
	if (n < 0 || n >= sizeof(buf)) {
		erresc("error: %s while printing %s response\n",
		       n < 0 ? "snprintf failed" : "truncation occurred",
		       is_osc4 ? "osc4" : "osc");
	} else {
		ttyreply(buf, n);
	}
}

/* Keep OSC 7 as metadata: never resolve its host or access its path. */
static void
osc_cwd(const char *uri, size_t len)
{
	static const char namechars[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
		"-._~!$&'()*+,;=";
	const char *path, *p;
	char host[INET6_ADDRSTRLEN];
	struct in6_addr address;
	size_t hostlen;
	int byte;

	if (!len) {
		xsetcwd(NULL);
		return;
	}
	if (len > CWDURI_MAX || len < 8 || strncasecmp(uri, "file://", 7))
		return;
	if (!(path = memchr(uri + 7, '/', len - 7)))
		return; /* The path must be absolute, even with an empty host. */
	if (uri[7] == '[') {
		hostlen = path - (uri + 7);
		if (hostlen < 3 || hostlen - 2 >= sizeof(host) || path[-1] != ']')
			return;
		memcpy(host, uri + 8, hostlen - 2);
		host[hostlen - 2] = '\0';
		if (inet_pton(AF_INET6, host, &address) != 1)
			return;
	}
	for (p = uri + 7; p < uri + len; p++) {
		if (*p == '%') {
			if (uri + len - p < 3 || !isxdigit((uchar)p[1]) ||
			    !isxdigit((uchar)p[2]))
				return;
			byte = (isdigit((uchar)p[1]) ? p[1] - '0' :
			        tolower((uchar)p[1]) - 'a' + 10) * 16 +
			       (isdigit((uchar)p[2]) ? p[2] - '0' :
			        tolower((uchar)p[2]) - 'a' + 10);
			if (byte < 0x20 || byte == 0x7f)
				return;
			p += 2;
		} else if (!*p || (!strchr(namechars, *p) &&
		           !(p >= path && strchr("/:@", *p)) &&
		           !(p < path && uri[7] == '[' && strchr("[]:", *p)))) {
			return;
		}
	}
	xsetcwd(uri);
}

/* OSC 8 changes a cell attribute independently of SGR. Invalid opens close
 * the old link, so rejected data cannot relabel subsequent text with it. */
static void
osc_link(char *text, size_t len)
{
	char *uri, *p, *name = NULL, *end;
	size_t urilen, bytes;
	uint64_t hash;
	int id, bucket;

	linkset(&term.c.attr, 0);
	if (memchr(text, '\0', len) || !(uri = memchr(text, ';', len)))
		return;
	*uri++ = '\0';
	if (!(urilen = len - (uri - text)) || urilen > URLTARGET_MAX)
		return;
	/* URI schemes and encoded targets are ASCII; never hide controls or
	 * whitespace inside the URL shown to the user. */
	if (!isalpha((uchar)*uri))
		return;
	for (p = uri + 1; *p && (isalnum((uchar)*p) || strchr("+.-", *p)); p++)
		;
	if (*p != ':')
		return;
	for (p = uri; *p; p++) {
		if (!BETWEEN((uchar)*p, 0x21, 0x7e))
			return;
		if (*p == '%' && (!p[1] || !p[2] ||
		                 !isxdigit((uchar)p[1]) || !isxdigit((uchar)p[2])))
			return;
	}
	for (p = text; *p; p = end) {
		end = strchr(p, ':');
		if (end)
			*end++ = '\0';
		else
			end = p + strlen(p);
		if (!strncmp(p, "id=", 3))
			name = p + 3;
	}
	if (name && strlen(name) > HLINK_ID_MAX)
		return;
	if (name && !*name)
		name = NULL;
	hash = linkhash(uri, name);
	if ((id = linkfind(uri, name, hash))) {
		linkset(&term.c.attr, id);
		if (!name)
			term.c.attr.mode |= ATTR_LINKSTART;
		return;
	}
	bytes = urilen + 1 + (name ? strlen(name) + 1 : 0);
	if (hlinks.bytes + bytes > HLINK_BYTES ||
	    (!hlinks.free && hlinks.allocated == HLINK_MAX))
		return;
	if ((id = hlinks.free))
		hlinks.free = hlinks.slots[id].next;
	else
		id = ++hlinks.allocated;
	hlinks.slots[id].uri = xstrdup(uri);
	hlinks.slots[id].name = name ? xstrdup(name) : NULL;
	hlinks.slots[id].hash = hash;
	bucket = hash & (HLINK_BUCKETS - 1);
	hlinks.slots[id].next = hlinks.buckets[bucket];
	hlinks.buckets[bucket] = id;
	hlinks.bytes += bytes;
	hlinks.used++;
	linkset(&term.c.attr, id);
	if (!name)
		term.c.attr.mode |= ATTR_LINKSTART;
}

/* The value of a whole field of decimal digits, or -1 if it is anything else or
 * too large. atoi() would make 4294967298 act as opcode 2. */
static int
strnumber(const char *s)
{
	int value = 0, digit;

	if (!*s)
		return -1;
	for (; *s; s++) {
		if (!BETWEEN(*s, '0', '9'))
			return -1;
		digit = *s - '0';
		if (value > (INT_MAX - digit) / 10)
			return -1;
		value = value * 10 + digit;
	}
	return value;
}

void
strhandle(void)
{
	char *p = NULL, *dec;
	int j, narg, par;
	const struct { int idx; char *str; } osc_table[] = {
		{ defaultfg, "foreground" },
		{ defaultbg, "background" },
		{ defaultcs, "cursor" }
	};

	term.esc &= ~(ESC_STR_END|ESC_STR);
	if (strescseq.overflow) {
		if (strescseq.type == ']' && strescseq.len >= 2 &&
		    !memcmp(strescseq.buf, "8;", 2))
			linkset(&term.c.attr, 0);
		erresc("erresc: ESC%c string longer than %d bytes ignored\n",
		       strescseq.type, STR_MAX_SIZ);
		return;
	}
	/* Semicolons belong to file paths too; consume OSC 7 before strparse()
	 * splits the other protocols' arguments in place. */
	if (strescseq.type == ']' && strescseq.len >= 2 &&
	    strescseq.buf[0] == '7' && strescseq.buf[1] == ';') {
		strescseq.buf[strescseq.len] = '\0';
		osc_cwd(strescseq.buf + 2, strescseq.len - 2);
		return;
	}
	if (strescseq.type == ']' && strescseq.len >= 2 &&
	    !memcmp(strescseq.buf, "8;", 2)) {
		strescseq.buf[strescseq.len] = '\0';
		osc_link(strescseq.buf + 2, strescseq.len - 2);
		return;
	}
	strparse();
	/* No number at all is 0, as in OSC ;title. */
	par = (narg = strescseq.narg) && *strescseq.args[0] ? strnumber(strescseq.args[0]) : 0;

	switch (strescseq.type) {
	case ']': /* OSC -- Operating System Command */
		switch (par) {
		case 7: /* malformed directory report without the OSC 7 separator */
			return;
		case 8: /* malformed hyperlink report without its separators */
			linkset(&term.c.attr, 0);
			return;
		case 0:
			if (narg > 1) {
				xsettitle(strtitle(strescseq.args[1]));
				xseticontitle(strescseq.args[1]);
			}
			return;
		case 1:
			if (narg > 1)
				xseticontitle(strtitle(strescseq.args[1]));
			return;
		case 2:
			if (narg > 1)
				xsettitle(strtitle(strescseq.args[1]));
			return;
		case 52: /* manipulate selection data */
			if (narg > 2 && allowwindowops) {
				dec = base64dec(strescseq.args[2]);
				if (dec) {
					xsetsel(dec);
					xclipcopy();
				} else {
					erresc("erresc: invalid base64\n");
				}
			}
			return;
		case 10: /* set dynamic VT100 text foreground color */
		case 11: /* set dynamic VT100 text background color */
		case 12: /* set dynamic text cursor color */
			if (narg < 2)
				break;
			p = strescseq.args[1];
			if ((j = par - 10) < 0 || j >= LEN(osc_table))
				break; /* shouldn't be possible */

			if (!strcmp(p, "?")) {
				osc_color_response(par, osc_table[j].idx, 0);
			} else if (!allowcolorchanges) {
				erresc("erresc: OSC %d color change ignored "
				       "(allowcolorchanges = 0)\n", par);
			} else if (xsetcolorname(osc_table[j].idx, p)) {
				erresc("erresc: invalid %s color: %s\n",
				       osc_table[j].str, p);
			} else {
				tfulldirt();
			}
			return;
		case 4: /* color set */
			if (narg < 3)
				break;
			p = strescseq.args[2];
			/* FALLTHROUGH */
		case 104: /* color reset */
			j = (narg > 1) ? strnumber(strescseq.args[1]) : -1;

			if (p && !strcmp(p, "?")) {
				osc_color_response(j, 0, 1);
			} else if (p && !allowcolorchanges) {
				erresc("erresc: OSC %d color change ignored "
				       "(allowcolorchanges = 0)\n", par);
			} else if (xsetcolorname(j, p)) {
				if (par == 104 && narg <= 1) {
					xloadcols();
					tfulldirt();
					return; /* color reset without parameter */
				}
				erresc("erresc: invalid color j=%d, p=%s\n",
				       j, p ? p : "(null)");
			} else {
				/*
				 * TODO if defaultbg color is changed, borders
				 * are dirty
				 */
				tfulldirt();
			}
			return;
		case 110: /* reset dynamic VT100 text foreground color */
		case 111: /* reset dynamic VT100 text background color */
		case 112: /* reset dynamic text cursor color */
			if (narg != 1)
				break;
			if ((j = par - 110) < 0 || j >= LEN(osc_table))
				break; /* shouldn't be possible */
			if (xsetcolorname(osc_table[j].idx, NULL)) {
				erresc("erresc: %s color not found\n", osc_table[j].str);
			} else {
				tfulldirt();
			}
			return;
		}
		break;
	case 'k': /* old title set compatibility */
		xsettitle(strtitle(strescseq.args[0]));
		return;
	case 'P': /* DCS -- Device Control String */
	case '_': /* APC -- Application Program Command */
	case '^': /* PM -- Privacy Message */
		return;
	}

	erresc("erresc: unknown str ");
	strdump();
}

void
strparse(void)
{
	int c;
	char *p = strescseq.buf;

	strescseq.narg = 0;
	strescseq.buf[strescseq.len] = '\0';

	if (*p == '\0')
		return;

	while (strescseq.narg < STR_ARG_SIZ) {
		strescseq.args[strescseq.narg++] = p;
		while ((c = *p) != ';' && c != '\0')
			++p;
		if (c == '\0')
			return;
		*p++ = '\0';
	}
}

/* Whether u is a combining mark, whatever the locale. */
static int
iscombining(Rune u)
{
	size_t lo = 0, hi = LEN(combiningtbl), mid;

	while (lo < hi) {
		mid = (lo + hi) / 2;
		if (u < combiningtbl[mid].first)
			hi = mid;
		else if (u > combiningtbl[mid].last)
			lo = mid + 1;
		else
			return 1;
	}
	return 0;
}

/* Characters removed from window titles. */
static int
titledrop(Rune u)
{
	/* Bidi controls: the window list would show the text reordered. */
	if (u == 0x061C || BETWEEN(u, 0x200E, 0x200F) ||
	    BETWEEN(u, 0x202A, 0x202E) || BETWEEN(u, 0x2066, 0x2069))
		return 1;
	/*
	 * Invisible characters (Default_Ignorable_Code_Point) that make the
	 * text differ from what is shown. The joiners U+200C/U+200D and the
	 * variation selectors are kept for emoji and scripts that need them.
	 */
	return u == 0x00AD || u == 0x034F || BETWEEN(u, 0x115F, 0x1160) ||
	       BETWEEN(u, 0x17B4, 0x17B5) || u == 0x180E || u == 0x200B ||
	       BETWEEN(u, 0x2060, 0x2065) || BETWEEN(u, 0x206A, 0x206F) ||
	       u == 0x3164 || u == 0xFEFF || u == 0xFFA0 ||
	       BETWEEN(u, 0xFFF0, 0xFFF8) || BETWEEN(u, 0x1BCA0, 0x1BCA3) ||
	       BETWEEN(u, 0x1D173, 0x1D17A) || BETWEEN(u, 0xE0000, 0xE00FF) ||
	       BETWEEN(u, 0xE01F0, 0xE0FFF);
}

/* Bound application titles, remove line breaks and filter explicit bidi
 * controls before publishing them in task bars and window lists. */
char *
strtitle(char *p)
{
	char *src, *dst, *end;
	size_t i, len;
	int marks = 0;
	Rune u;

	if (!p)
		return p;
	end = p + strlen(p);
	for (src = dst = p; src < end; src += len) {
		/* Copy undecodable bytes (only possible outside UTF-8 mode) as is. */
		if (!(len = utf8decode(src, &u, end - src)))
			len = end - src;
		if (ISCONTROLC0(u) || u == 0x2028 || u == 0x2029) {
			/* U+2028/U+2029 are mandatory line breaks too (UAX #14). */
			*dst++ = ' ';
			marks = 0;
			continue;
		}
		if (titledrop(u))
			continue;
		/* Limit stacked marks and joiners using Unicode categories, since
		 * wcwidth() does not recognize them in non-UTF-8 locales. */
		if (!iscombining(u) && u != 0x200C && u != 0x200D)
			marks = 0;
		else if (++marks > TITLE_MARKS)
			continue;
		memmove(dst, src, len);
		dst += len;
	}
	*dst = '\0';
	if ((i = dst - p) > TITLE_MAX) {
		/* Do not cut a UTF-8 sequence in half. */
		for (i = TITLE_MAX; i > 0 && ((uchar)p[i] & 0xC0) == 0x80; i--)
			;
		p[i] = '\0';
	}
	return p;
}

void
strdump(void)
{
	size_t i;
	uint c;

	if (!errlimit.shown)
		return;
	fprintf(stderr, "ESC%c", strescseq.type);
	for (i = 0; i < strescseq.len; i++) {
		if (i == DUMP_MAX) {
			fprintf(stderr, "...(%zu bytes)\n", strescseq.len);
			return;
		}
		c = strescseq.buf[i] & 0xff;
		if (c == '\0') {
			putc('\n', stderr);
			return;
		} else if (isprint(c)) {
			putc(c, stderr);
		} else if (c == '\n') {
			fprintf(stderr, "(\\n)");
		} else if (c == '\r') {
			fprintf(stderr, "(\\r)");
		} else if (c == 0x1b) {
			fprintf(stderr, "(\\e)");
		} else {
			fprintf(stderr, "(%02x)", c);
		}
	}
	fprintf(stderr, "ESC\\\n");
}

void
strreset(void)
{
	strescseq = (STREscape){
		.buf = xrealloc(strescseq.buf, STR_BUF_SIZ),
		.siz = STR_BUF_SIZ,
	};
}

/* Batch parser output and flush after each tty read. Regular files are
 * written directly; other destinations use a writer process through a
 * nonblocking pipe. poll() cannot guarantee room when other writers share the
 * destination, and setting O_NONBLOCK there would affect shared stdout flags
 * with -o -. Stop non-file printing if PRINT_MAX is exceeded, keeping the
 * record contiguous.
 */
static struct {
	char *buf;
	size_t off, len, cap;
} printq;

static void
printstop(void)
{
	close(iofd);
	iofd = -1;
	free(printq.buf);
	memset(&printq, 0, sizeof(printq));
}

static int
printdirect(void)
{
	struct stat st;

	return !fstat(iofd, &st) && S_ISREG(st.st_mode);
}

static const char *printfdlist = "/proc/self/fd";

/* The highest open descriptor, or -1 if the system doesn't say. No limit can
 * stand in for this, as a descriptor can stay open above any limit lowered later. */
static long
printwriterlastfd(void)
{
	struct dirent *entry;
	DIR *dir;
	long fd, last = -1;

	if (!(dir = opendir(printfdlist)))
		return -1;
	errno = 0;
	while ((entry = readdir(dir))) {
		fd = atol(entry->d_name);
		last = MAX(last, fd);
	}
	if (errno)
		last = -1;
	closedir(dir);
	return last;
}

/* Wait for a reader to open the FIFO, for as long as phist has output for it. */
static int
printwriterfifo(int in, const char *fifo)
{
	struct pollfd done = { .fd = in };
	int fd;

	while ((fd = open(fifo, O_WRONLY | O_NONBLOCK)) < 0) {
		if (errno != ENXIO && errno != EINTR) {
			perror("Error opening output file");
			_exit(1);
		}
		if (poll(&done, 1, 250) > 0 && (done.revents & POLLHUP))
			_exit(0);
	}
	return fd;
}

/* Copy to the output until phist is done with it. Only this process waits for
 * the reader. A descriptor left open here could keep phist's window up, and the
 * pipe's own write end would keep this process from seeing the end of its input,
 * so it goes first, also leaving room to list the others. */
static void
printwriterrun(int in, int wr, int out, const char *fifo)
{
	struct pollfd room = { .events = POLLOUT };
	char buf[BUFSIZ];
	long fd, last;
	ssize_t n, r, off;

	close(wr);
	if ((last = printwriterlastfd()) < 0) {
		fprintf(stderr, "Can't close inherited descriptors: stopped printing\n");
		_exit(1);
	}
	for (fd = 0; fd <= last; fd++) {
		if (fd != in && fd != out && fd != STDERR_FILENO)
			close(fd);
	}
	if (fifo)
		out = printwriterfifo(in, fifo);
	room.fd = out;
	while ((n = read(in, buf, sizeof(buf))) != 0) {
		if (n < 0) {
			if (errno == EINTR)
				continue;
			_exit(1);
		}
		for (off = 0; off < n; off += r) {
			r = write(out, buf + off, n - off);
			if (r >= 0)
				continue;
			r = 0;
			/* stdout can already be nonblocking for -o -. */
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				poll(&room, 1, -1);
			else if (errno != EINTR) {
				perror("Error writing to output file");
				_exit(1);
			}
		}
	}
	_exit(0);
}

/* Have a writer process take over any output but a regular file. It is orphaned,
 * so that it neither waits as a zombie nor signals phist's SIGCHLD handler. */
static void
printwriter(const char *fifo)
{
	int fds[2], failed, null, status;
	pid_t child;

	if (!fifo && (iofd < 0 || printdirect()))
		return;
	if (pipe(fds) < 0) {
		perror("Error starting output writer");
		printstop();
		return;
	}
	child = fork();
	if (!child) {
		child = fork();
		if (child)
			_exit(child < 0);
		printwriterrun(fds[0], fds[1], iofd, fifo);
	}
	close(fds[0]);
	status = 1;
	failed = child < 0;
	while (child > 0 && (failed = waitpid(child, &status, 0) < 0) && errno == EINTR)
		;
	/* errno only says why if the failure was here, and not in the child. */
	if (failed)
		perror("Error starting output writer");
	else if (status)
		fprintf(stderr, "Error starting output writer\n");
	if (failed || status) {
		close(fds[1]);
		printstop();
		return;
	}
	fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) | O_NONBLOCK);
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);
	/* Only the writer holds the output now, so its end reaches the reader.
	 * fd 1 stays open, or the next file opened would become stdout. */
	if (iofd == STDOUT_FILENO && (null = open("/dev/null", O_WRONLY)) >= 0) {
		dup2(null, STDOUT_FILENO);
		close(null);
	} else if (iofd >= 0) {
		close(iofd);
	}
	iofd = fds[1];
}

/* Open without waiting: a FIFO without a reader would hold up the terminal, and
 * the writer waits for one instead. Only the opening has to be nonblocking. */
static void
printopen(const char *out)
{
	const char *fifo = NULL;
	struct stat st;
	int err;

	if (!strcmp(out, "-")) {
		iofd = STDOUT_FILENO;
	} else if ((iofd = open(out, O_WRONLY | O_CREAT | O_NONBLOCK, 0666)) >= 0) {
		fcntl(iofd, F_SETFL, fcntl(iofd, F_GETFL) & ~O_NONBLOCK);
	} else {
		err = errno;
		if (err == ENXIO && !stat(out, &st) && S_ISFIFO(st.st_mode)) {
			fifo = out;
		} else {
			fprintf(stderr, "Error opening %s:%s\n", out, strerror(err));
		}
	}
	printwriter(fifo);
}

void
tprinter(char *s, size_t len)
{
	if (iofd == -1)
		return;
	if (len > PRINT_MAX - printq.len) {
		ttyflushprint();
		if (iofd == -1)
			return;
	}
	if (len > PRINT_MAX - printq.len) {
		/* A file takes this directly, after what an interrupted flush left. */
		if (printdirect()) {
			if (xwrite(iofd, printq.buf + printq.off, printq.len) < 0 ||
			    xwrite(iofd, s, len) < 0) {
				perror("Error writing to output file");
				printstop();
				return;
			}
			printq.off = printq.len = 0;
			return;
		}
		fprintf(stderr, "Output file not reading: stopped printing "
		        "after %d bytes waited\n", PRINT_MAX);
		printstop();
		return;
	}
	if (printq.off) {
		memmove(printq.buf, printq.buf + printq.off, printq.len);
		printq.off = 0;
	}
	if (printq.len + len > printq.cap) {
		printq.cap = MIN(MAX(4096, MAX(printq.cap * 2, printq.len + len)), PRINT_MAX);
		printq.buf = xrealloc(printq.buf, printq.cap);
	}
	memcpy(printq.buf + printq.len, s, len);
	printq.len += len;
}

int
ttyprintfd(void)
{
	return printq.len ? iofd : -1;
}

/* Pass on what the writer has room for. Without a writer, it reported why. */
void
ttyflushprint(void)
{
	ssize_t r;

	while (printq.len) {
		r = write(iofd, printq.buf + printq.off, printq.len);
		if (r < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			if (errno != EPIPE)
				perror("Error writing to output file");
			printstop();
			return;
		}
		printq.off += r;
		printq.len -= r;
	}
	printq.off = 0;
}

void
tdumpsel(void)
{
	char *ptr;

	if ((ptr = getsel())) {
		tprinter(ptr, strlen(ptr));
		free(ptr);
	}
}

void
tdumpline(Line line)
{
	char buf[UTF_SIZ];
	int x, len = tlinelen(line);

	for (x = 0; x < len; x++)
		if (!(line[x].mode & (ATTR_WDUMMY | ATTR_WPAD)))
			tprinter(buf, utf8encode(line[x].u, buf));
	tprinter("\n", 1);
}

void
tdump(void)
{
	int i;

	for (i = 0; i < term.row; ++i)
		tdumpline(renderline(i));
}

void
tputtab(int n)
{
	uint x = term.c.x;

	if (n > 0) {
		while (x < term.col && n--)
			for (++x; x < term.col && !term.tabs[x]; ++x)
				/* nothing */ ;
	} else if (n < 0) {
		while (x > 0 && n++)
			for (--x; x > 0 && !term.tabs[x]; --x)
				/* nothing */ ;
	}
	term.c.x = LIMIT(x, 0, term.col-1);
}

void
tdefutf8(Rune ascii)
{
	if (ascii == 'G')
		term.mode |= MODE_UTF8;
	else if (ascii == '@')
		term.mode &= ~MODE_UTF8;
}

void
tdeftran(Rune ascii)
{
	static char cs[] = "0B";
	static int vcs[] = {CS_GRAPHIC0, CS_USA};
	char *p;

	/* strchr() also matches the terminator; wider runes must not truncate. */
	if (!BETWEEN(ascii, 0x01, 0x7f) || (p = strchr(cs, ascii)) == NULL) {
		if (BETWEEN(ascii, 0x20, 0x7e))
			erresc("esc unhandled charset: ESC ( %c\n", (int)ascii);
		else
			erresc("esc unhandled charset: ESC ( U+%04X\n", (unsigned)ascii);
	} else {
		term.trantbl[term.icharset] = vcs[p - cs];
	}
}

static void
kscroll(const Arg *arg)
{
	uint64_t oldstart;
	uint64_t newstart;

	oldstart = sb_view_start();
	sb.view_offset += arg->i;
	LIMIT(sb.view_offset, 0, sb_maxoffset());
	urls.dirty = 1;
	newstart = sb_view_start();

	selscrollback(sb_startdelta(oldstart, newstart));
	/* The wheel moves the view; the keyboard cursor stays inside it. */
	if (vim.mode && !search.active) {
		vimvalidatesel();
		LIMIT(vim.pos.row, searchtop(), searchtop() + term.row - 1);
		vimupdate();
	}
	redraw();
}

void
kscrolldown(const Arg *arg)
{
	Arg a;

	/* Negative arguments scroll by the screen height minus that many lines. */
	a.i = -(arg->i < 0 ? MAX(1, term.row + arg->i) : arg->i);
	kscroll(&a);
}

void
kscrollup(const Arg *arg)
{
	Arg a;

	a.i = arg->i < 0 ? MAX(1, term.row + arg->i) : arg->i;
	kscroll(&a);
}

void
tdectest(Rune c)
{
	int x, y;

	if (c == '8') { /* DEC screen alignment test. */
		for (x = 0; x < term.col; ++x) {
			for (y = 0; y < term.row; ++y)
				tsetchar('E', &term.c.attr, x, y);
		}
	}
}

void
tstrsequence(uchar c)
{
	switch (c) {
	case 0x90:   /* DCS -- Device Control String */
		c = 'P';
		break;
	case 0x9f:   /* APC -- Application Program Command */
		c = '_';
		break;
	case 0x9e:   /* PM -- Privacy Message */
		c = '^';
		break;
	case 0x9d:   /* OSC -- Operating System Command */
		c = ']';
		break;
	}
	strreset();
	strescseq.type = c;
	term.esc |= ESC_STR;
}

void
tcontrolcode(uchar ascii)
{
	switch (ascii) {
	case '\t':   /* HT */
		tputtab(1);
		return;
	case '\b':   /* BS */
		tmoveto(term.c.x-1, term.c.y);
		return;
	case '\r':   /* CR */
		tmoveto(0, term.c.y);
		return;
	case '\f':   /* LF */
	case '\v':   /* VT */
	case '\n':   /* LF */
		/* go to first col if the mode is set */
		tnewline(IS_SET(MODE_CRLF));
		return;
	case '\a':   /* BEL */
		if (term.esc & ESC_STR_END) {
			/* backwards compatibility to xterm */
			strhandle();
		} else {
			xbell();
		}
		break;
	case '\033': /* ESC */
		csireset();
		term.esc &= ~(ESC_CSI|ESC_ALTCHARSET|ESC_TEST);
		term.esc |= ESC_START;
		return;
	case '\016': /* SO (LS1 -- Locking shift 1) */
	case '\017': /* SI (LS0 -- Locking shift 0) */
		term.charset = 1 - (ascii - '\016');
		return;
	case '\032': /* SUB */
		tsetchar('?', &term.c.attr, term.c.x, term.c.y);
		/* FALLTHROUGH */
	case '\030': /* CAN */
		csireset();
		break;
	case '\005': /* ENQ (IGNORED) */
	case '\000': /* NUL (IGNORED) */
	case '\021': /* XON (IGNORED) */
	case '\023': /* XOFF (IGNORED) */
	case 0177:   /* DEL (IGNORED) */
		return;
	case 0x80:   /* TODO: PAD */
	case 0x81:   /* TODO: HOP */
	case 0x82:   /* TODO: BPH */
	case 0x83:   /* TODO: NBH */
	case 0x84:   /* TODO: IND */
		break;
	case 0x85:   /* NEL -- Next line */
		tnewline(1); /* always go to first col */
		break;
	case 0x86:   /* TODO: SSA */
	case 0x87:   /* TODO: ESA */
		break;
	case 0x88:   /* HTS -- Horizontal tab stop */
		term.tabs[term.c.x] = 1;
		break;
	case 0x89:   /* TODO: HTJ */
	case 0x8a:   /* TODO: VTS */
	case 0x8b:   /* TODO: PLD */
	case 0x8c:   /* TODO: PLU */
	case 0x8d:   /* TODO: RI */
	case 0x8e:   /* TODO: SS2 */
	case 0x8f:   /* TODO: SS3 */
	case 0x91:   /* TODO: PU1 */
	case 0x92:   /* TODO: PU2 */
	case 0x93:   /* TODO: STS */
	case 0x94:   /* TODO: CCH */
	case 0x95:   /* TODO: MW */
	case 0x96:   /* TODO: SPA */
	case 0x97:   /* TODO: EPA */
	case 0x98:   /* TODO: SOS */
	case 0x99:   /* TODO: SGCI */
		break;
	case 0x9a:   /* DECID -- Identify Terminal */
		ttyreply(vtiden, strlen(vtiden));
		break;
	case 0x9b:   /* TODO: CSI */
	case 0x9c:   /* TODO: ST */
		break;
	case 0x90:   /* DCS -- Device Control String */
	case 0x9d:   /* OSC -- Operating System Command */
	case 0x9e:   /* PM -- Privacy Message */
	case 0x9f:   /* APC -- Application Program Command */
		tstrsequence(ascii);
		return;
	}
	/* only CAN, SUB, \a and C1 chars interrupt a sequence */
	term.esc &= ~(ESC_STR_END|ESC_STR);
}

/*
 * returns 1 when the sequence is finished and it hasn't to read
 * more characters for this sequence, otherwise 0
 */
int
eschandle(Rune ascii)
{
	switch (ascii) {
	case '[':
		term.esc |= ESC_CSI;
		return 0;
	case '#':
		term.esc |= ESC_TEST;
		return 0;
	case '%':
		term.esc |= ESC_UTF8;
		return 0;
	case 'P': /* DCS -- Device Control String */
	case '_': /* APC -- Application Program Command */
	case '^': /* PM -- Privacy Message */
	case ']': /* OSC -- Operating System Command */
	case 'k': /* old title set compatibility */
		tstrsequence(ascii);
		return 0;
	case 'n': /* LS2 -- Locking shift 2 */
	case 'o': /* LS3 -- Locking shift 3 */
		term.charset = 2 + (ascii - 'n');
		break;
	case '(': /* GZD4 -- set primary charset G0 */
	case ')': /* G1D4 -- set secondary charset G1 */
	case '*': /* G2D4 -- set tertiary charset G2 */
	case '+': /* G3D4 -- set quaternary charset G3 */
		term.icharset = ascii - '(';
		term.esc |= ESC_ALTCHARSET;
		return 0;
	case 'D': /* IND -- Linefeed */
		if (term.c.y == term.bot) {
			tscrollup(term.top, 1, 1);
		} else {
			tmoveto(term.c.x, term.c.y+1);
		}
		break;
	case 'E': /* NEL -- Next line */
		tnewline(1); /* always go to first col */
		break;
	case 'H': /* HTS -- Horizontal tab stop */
		term.tabs[term.c.x] = 1;
		break;
	case 'M': /* RI -- Reverse index */
		if (term.c.y == term.top) {
			tscrolldown(term.top, 1);
		} else {
			tmoveto(term.c.x, term.c.y-1);
		}
		break;
	case 'Z': /* DECID -- Identify Terminal */
		ttyreply(vtiden, strlen(vtiden));
		break;
	case 'c': /* RIS -- Reset to initial state */
		/*
		 * Like clear, move the main screen into the history rather than
		 * erasing it, so output cannot wipe its own record (reset sends RIS).
		 */
		if (IS_SET(MODE_ALTSCREEN))
			tswapscreen();
		sb_preserve_screen();
		tresetstate();
		if (sel.ob.x != -1 && term.row > 0)
			selclear();
		resettitle();
		xloadcols();
		xsetmode(0, MODE_HIDE);
		xsetmode(0, MODE_BRCKTPASTE);
		break;
	case '=': /* DECPAM -- Application keypad */
		xsetmode(1, MODE_APPKEYPAD);
		break;
	case '>': /* DECPNM -- Normal keypad */
		xsetmode(0, MODE_APPKEYPAD);
		break;
	case '7': /* DECSC -- Save Cursor */
		tcursor(CURSOR_SAVE);
		break;
	case '8': /* DECRC -- Restore Cursor */
		tcursor(CURSOR_LOAD);
		break;
	case '\\': /* ST -- String Terminator */
		if (term.esc & ESC_STR_END)
			strhandle();
		break;
	default:
		erresc("erresc: unknown sequence ESC 0x%02X '%c'\n",
			(unsigned)ascii, BETWEEN(ascii, 0x20, 0x7e) ? (int)ascii : '.');
		break;
	}
	return 1;
}

/* The cell last printed to, or NULL at the start of a line. */
static Glyph *
tprevcell(void)
{
	int x = term.c.x;

	if (!(term.c.state & CURSOR_WRAPNEXT)) {
		if (x == 0)
			return NULL;
		x--;
	}
	if (term.line[term.c.y][x].mode & ATTR_WDUMMY && x > 0)
		x--;
	return &term.line[term.c.y][x];
}

static void
tpresentation(int emoji)
{
	Glyph *gp;

	if (!(gp = tprevcell()))
		return;
	MODBIT(gp->mode, emoji != !!(gp->mode & ATTR_WIDE), ATTR_ALTPRES);
	tdirtyscreen(term.c.y, term.c.y);
}

/* The precomposed form of base followed by mark, or 0 if there is none. */
static Rune
compose(Rune base, Rune mark)
{
	enum { SBASE = 0xAC00, LBASE = 0x1100, VBASE = 0x1161, TBASE = 0x11A7,
	       LCOUNT = 19, VCOUNT = 21, TCOUNT = 28, NCOUNT = VCOUNT * TCOUNT };
	size_t lo = 0, hi = LEN(composetbl), mid;

	/* Hangul jamo combine arithmetically: L+V gives LV, LV+T gives LVT. */
	if (BETWEEN(base, LBASE, LBASE + LCOUNT - 1) &&
	    BETWEEN(mark, VBASE, VBASE + VCOUNT - 1))
		return SBASE + ((base - LBASE) * VCOUNT + mark - VBASE) * TCOUNT;
	if (BETWEEN(base, SBASE, SBASE + LCOUNT * NCOUNT - 1) &&
	    (base - SBASE) % TCOUNT == 0 &&
	    BETWEEN(mark, TBASE + 1, TBASE + TCOUNT - 1))
		return base + mark - TBASE;

	while (lo < hi) {
		mid = (lo + hi) / 2;
		if (composetbl[mid].base < base || (composetbl[mid].base == base &&
		    composetbl[mid].mark < mark))
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo < LEN(composetbl) && composetbl[lo].base == base &&
	    composetbl[lo].mark == mark)
		return composetbl[lo].comp;
	return 0;
}

/*
 * Cells hold one character, so a zero width character is merged into the
 * previous cell when a precomposed form of the same width exists, and is
 * dropped otherwise.
 */
static void
tcombine(Rune u)
{
	Glyph *gp;
	Rune c;
	int x, wide;

	if (!(gp = tprevcell()))
		return;
	wide = (gp->mode & ATTR_WIDE) != 0;
	if (!(c = compose(gp->u, u)) || wcwidth(c) != (wide ? 2 : 1))
		return;
	x = gp - term.line[term.c.y];
	if (selected(x, screenrow(term.c.y)) ||
	    (wide && selected(x + 1, screenrow(term.c.y))))
		selclear();
	gp->u = c;
	term.lastc = c;
	tdirtyscreen(term.c.y, term.c.y);
	urls.dirty = 1;
}

void
tputc(Rune u)
{
	char c[UTF_SIZ];
	int control;
	int width, len;
	Glyph *gp;

	control = ISCONTROL(u);
	if (u < 127 || !IS_SET(MODE_UTF8)) {
		c[0] = u;
		width = len = 1;
	} else {
		len = utf8encode(u, c);
		if (!control && (width = wcwidth(u)) == -1)
			width = 1;
	}

	if (IS_SET(MODE_PRINT))
		tprinter(c, len);

	/*
	 * STR sequence must be checked before anything else
	 * because it uses all following characters until it
	 * receives a ESC, a SUB, a ST or any other C1 control
	 * character.
	 */
	if (term.esc & ESC_STR) {
		if (u == '\a' || u == 030 || u == 032 || u == 033 ||
		   ISCONTROLC1(u)) {
			term.esc &= ~(ESC_START|ESC_STR);
			term.esc |= ESC_STR_END;
			goto check_control_code;
		}

		if (strescseq.overflow)
			return;
		if (strescseq.len+len >= strescseq.siz) {
			/* Consume through the terminator, but stop growing the buffer at the limit. */
			if (strescseq.siz >= STR_MAX_SIZ) {
				strescseq.overflow = 1;
				return;
			}
			strescseq.siz *= 2;
			strescseq.buf = xrealloc(strescseq.buf, strescseq.siz);
		}

		memmove(&strescseq.buf[strescseq.len], c, len);
		strescseq.len += len;
		return;
	}

check_control_code:
	/*
	 * Actions of control codes must be performed as soon they arrive
	 * because they can be embedded inside a control sequence, and
	 * they must not cause conflicts with sequences.
	 */
	if (control) {
		/* in UTF-8 mode ignore handling C1 control characters */
		if (IS_SET(MODE_UTF8) && ISCONTROLC1(u))
			return;
		tcontrolcode(u);
		/*
		 * control codes are not shown ever
		 */
		if (!term.esc)
			term.lastc = 0;
		return;
	} else if (term.esc & ESC_START) {
		if (term.esc & ESC_CSI) {
			/* A non-ASCII rune must not truncate into a valid byte. */
			csiescseq.buf[csiescseq.len++] = u < 0x80 ? u : 0xff;
			if (BETWEEN(u, 0x40, 0x7E)
					|| csiescseq.len >= \
					sizeof(csiescseq.buf)-1) {
				term.esc = 0;
				csiparse();
				csihandle();
			}
			return;
		} else if (term.esc & ESC_UTF8) {
			tdefutf8(u);
		} else if (term.esc & ESC_ALTCHARSET) {
			tdeftran(u);
		} else if (term.esc & ESC_TEST) {
			tdectest(u);
		} else {
			if (!eschandle(u))
				return;
			/* sequence already finished */
		}
		term.esc = 0;
		/*
		 * All characters which form part of a sequence are not
		 * printed
		 */
		return;
	}
	/* Variation selectors choose the presentation of the previous character. */
	if (BETWEEN(u, 0xFE0E, 0xFE0F) && IS_SET(MODE_UTF8)) {
		tpresentation(u == 0xFE0F);
		return;
	}
	if (width == 0) {
		tcombine(u);
		return;
	}

	gp = &term.line[term.c.y][term.c.x];
	if (IS_SET(MODE_WRAP) && (term.c.state & CURSOR_WRAPNEXT)) {
		gp->mode |= ATTR_WRAP;
		tnewline(1);
		gp = &term.line[term.c.y][term.c.x];
	}

	if (IS_SET(MODE_INSERT) && term.c.x+width < term.col) {
		linkmove(gp+width, gp, term.col - term.c.x - width);
		gp->mode &= ~ATTR_WIDE;
	}

	if (term.c.x+width > term.col) {
		if (IS_SET(MODE_WRAP)) {
			/* Skip an unused cell, but preserve text already at the margin. */
			if (term.c.x > 0) {
				gp = &term.line[term.c.y][term.col - 1];
				if (sb_ispadding(gp))
					gp->mode |= ATTR_WPAD;
				gp->mode |= ATTR_WRAP;
				tdirtyscreen(term.c.y, term.c.y);
			}
			tnewline(1);
		} else {
			tmoveto(term.col - width, term.c.y);
		}
		gp = &term.line[term.c.y][term.c.x];
	}

	if (selected(term.c.x, screenrow(term.c.y)) ||
	    (width == 2 && selected(term.c.x + 1, screenrow(term.c.y))))
		selclear();
	tsetchar(u, &term.c.attr, term.c.x, term.c.y);
	term.lastc = u;

	if (width == 2) {
		gp->mode |= ATTR_WIDE;
		if (term.c.x+1 < term.col) {
			if ((gp[1].mode & ATTR_WIDE) && term.c.x+2 < term.col) {
				gp[2].u = ' ';
				gp[2].mode &= ~ATTR_WDUMMY;
				linkset(&gp[2], 0);
			}
			gp[1].u = '\0';
			gp[1].mode = ATTR_WDUMMY;
			linkset(&gp[1], gp->link);
		}
	}
	if (term.c.x+width < term.col) {
		tmoveto(term.c.x+width, term.c.y);
	} else {
		term.c.state |= CURSOR_WRAPNEXT;
	}
}

int
twrite(const char *buf, int buflen, int show_ctrl)
{
	int charsize;
	Rune u;
	int n;

	for (n = 0; n < buflen; n += charsize) {
		if (IS_SET(MODE_UTF8)) {
			/* process a complete utf8 char */
			charsize = utf8decode(buf + n, &u, buflen - n);
			if (charsize == 0)
				break;
		} else {
			u = buf[n] & 0xFF;
			charsize = 1;
		}
		if (show_ctrl && ISCONTROL(u)) {
			if (u & 0x80) {
				u &= 0x7f;
				tputc('^');
				tputc('[');
			} else if (u != '\n' && u != '\r' && u != '\t') {
				u ^= 0x40;
				tputc('^');
			}
		}
		tputc(u);
	}
	return n;
}

void
tresize(int col, int row)
{
	int i, j;
	int min_limit;
	int minrow = MIN(row, term.row);
	int old_row = term.row;
	int old_col = term.col;
	int save_end = 0; /* Track effective pushed height */
	int loaded = 0;
	int pop_width = 0;
	int needs_reflow = 0;
	int is_alt = IS_SET(MODE_ALTSCREEN);
	int history_cap = sb.limit;
	TCursor *main_cursor = is_alt ? &term.mainc : &term.c;
	ReflowCursor marks[2] = {
		{ .cursor = main_cursor, .source_row = sb.len + main_cursor->y },
		{ .cursor = &term.saved[0], .source_row = sb.len + term.saved[0].y }
	};
	int nmarks = 2, screen_start = 0, cursor_row, altshift = 0, viewrow = 0;
	Line *tmp, *oldalt = NULL;

	urls.dirty = 1;
	if (col < 1 || row < 1) {
		fprintf(stderr,
			"tresize: error resizing to %dx%d\n", col, row);
		return;
	}
	if (col == term.col && row == term.row)
		return;
	search.refresh = 1;
	if (vim.mode)
		viewrow = vimy(vim.pos.row);
	/* Reflow moves text between rows, so a selection would cover other text. */
	selclear();

	/* Visible rows must not evict history before they are popped back out. */
	sb_setlimit(history_cap + MAX(old_row, row));

	/* Operate on the currently visible screen buffer. */
	if (is_alt) {
		tmp = term.line;
		term.line = term.alt;
		term.alt = tmp;
	}

	save_end = term.row;
	if (term.row != 0 && term.col != 0) {
		min_limit = main_cursor->y;

		for (i = term.row - 1; i > min_limit; i--) {
			if (sb_contentwidth(term.line[i], term.col) > 0)
				break;
		}
		save_end = i + 1;
		/* A saved cursor must not keep obsolete blank screen rows alive. */
		if (term.saved[0].y >= save_end)
			nmarks = 1;

		for (i = 0; i < save_end; i++) {
			sb_push(term.line[i]);
		}
		/* Reflow only when the width change affects wrapping. */
		if (col > term.col) {
			/* Unwrapped short lines can retain their existing allocation. */
			needs_reflow = sb.max_width >= term.col;
		} else if (col < term.col) {
			/* Shrinking: Only reflow if content is wider than new width. */
			if (sb.max_width > col)
				needs_reflow = 1;
		}
		/* A pending wrap after trailing blanks still needs cursor mapping. */
		if (col != term.col && ((main_cursor->state & CURSOR_WRAPNEXT) ||
		    (nmarks > 1 && term.saved[0].state & CURSOR_WRAPNEXT)))
			needs_reflow = 1;
		if (needs_reflow) {
			sb_resize(col, row, marks, nmarks);
		} else {
			/* If we don't reflow, we still need to reset the view
			 * because sb_pop_screen() might change the history length. */
			sb.view_offset = 0;
		}
	}

	/* term.alt is the alternate screen here; it is copied once resized. */
	if (term.line) {
		for (i = 0; i < term.row; i++) {
			linkcells(term.line[i], term.col, -1);
			free(term.line[i]);
		}
		free(term.line);
		free(term.dirty);
		oldalt = term.alt;
		/* Like the old resize, drop top rows to keep the cursor on screen. */
		if (is_alt)
			altshift = MAX(0, term.c.y - row + 1);
	}

	term.col = col;
	term.row = row;

	term.line  = xmalloc(term.row * sizeof(Line));
	term.alt   = xmalloc(term.row * sizeof(Line));
	term.dirty = xmalloc(term.row * sizeof(int));

	for (i = 0; i < term.row; i++) {
		term.line[i] = xmalloc(term.col * sizeof(Glyph));
		term.alt[i]  = xmalloc(term.col * sizeof(Glyph));
		term.dirty[i] = 1;

		for (j = 0; j < term.col; j++) {
			term.line[i][j] = term.c.attr;
			term.line[i][j].u = ' ';
			term.line[i][j].mode = 0;
			term.line[i][j].link = 0;

			term.alt[i][j] = term.c.attr;
			term.alt[i][j].u = ' ';
			term.alt[i][j].mode = 0;
			term.alt[i][j].link = 0;
		}
	}
	if (oldalt) {
		for (i = 0; i < old_row; i++) {
			if (BETWEEN(i - altshift, 0, row - 1)) {
				memcpy(term.alt[i - altshift], oldalt[i],
				       MIN(col, old_col) * sizeof(Glyph));
				linkcells(term.alt[i - altshift], MIN(col, old_col), 1);
				if (col > old_col)
					term.alt[i - altshift][old_col - 1].mode &= ~ATTR_WRAP;
			}
			linkcells(oldalt[i], old_col, -1);
			free(oldalt[i]);
		}
		free(oldalt);
		if (is_alt)
			term.c.y -= altshift;
	}

	/* Keep existing stops and continue the last one's spacing into new columns. */
	term.tabs = xrealloc(term.tabs, col * sizeof(*term.tabs));
	if (col > old_col) {
		memset(term.tabs + old_col, 0, sizeof(*term.tabs) * (col - old_col));
		for (i = MAX(old_col - 1, 0); i > 0 && !term.tabs[i]; i--)
			;
		for (i += MAX(1, tabspaces); i < col; i += MAX(1, tabspaces))
			if (i >= old_col)
				term.tabs[i] = 1;
	}

	tsetscroll(0, term.row - 1);

	if (minrow > 0) {
		cursor_row = needs_reflow ? marks[0].row : sb.len - save_end + main_cursor->y;
		screen_start = MAX(0, MIN(sb.len - term.row, cursor_row));
		loaded = MIN(term.row, sb.len - screen_start);
		pop_width = needs_reflow ? col : MIN(col, old_col);
		sb_pop_screen(screen_start, loaded, pop_width);
	}
	sb_setlimit(history_cap);
	if (is_alt) {
		tmp = term.line;
		term.line = term.alt;
		term.alt = tmp;
	}
	if (old_row > 0) {
		/* Follow content without retaining otherwise empty saved rows. */
		if (!needs_reflow)
			term.saved[0].y += cursor_row - main_cursor->y - screen_start;
		else if (nmarks == 1)
			term.saved[0].y += loaded - save_end;
		if (needs_reflow) {
			for (i = 0; i < nmarks; i++)
				marks[i].cursor->y = marks[i].row - screen_start;
		} else {
			main_cursor->y = cursor_row - screen_start;
		}
	}
	/* Cursors outside the reflowed main content follow the copied screen. */
	if (col != old_col) {
		term.saved[1].state &= ~CURSOR_WRAPNEXT;
		if (nmarks == 1)
			term.saved[0].state &= ~CURSOR_WRAPNEXT;
		if (is_alt)
			term.c.state &= ~CURSOR_WRAPNEXT;
	}
	for (i = 0; i < 2; i++) {
		LIMIT(term.saved[i].x, 0, term.col - 1);
		LIMIT(term.saved[i].y, 0, term.row - 1);
	}
	LIMIT(main_cursor->x, 0, term.col - 1);
	LIMIT(main_cursor->y, 0, term.row - 1);
	if (term.c.y >= term.row) {
		term.c.y = term.row - 1;
	}
	if (term.c.x >= term.col) {
		term.c.x = term.col - 1;
	}
	if (term.c.y < 0) {
		term.c.y = 0;
	}
	if (term.c.x < 0) {
		term.c.x = 0;
	}

	/* Reflow invalidates absolute rows, so stay at the same place on screen. */
	if (vim.mode) {
		vim.mode = VIM_NORMAL;
		vim.origin = searchtop();
		vim.pos.row = searchtop() + MIN(MAX(viewrow, 0), term.row - 1);
		if (!search.active)
			vimupdate();
	}

	tfulldirt();
	sb_view_changed();
}

void
resettitle(void)
{
	xsettitle(NULL);
}

void
drawregion(int x1, int y1, int x2, int y2)
{
	int y;

	Line line;
	for (y = y1; y < y2; y++) {
		if (!term.dirty[y])
			continue;
		term.dirty[y] = 0;
		line = renderline(y);
		xdrawline(line, x1, y, x2);
	}
}

void
draw(void)
{
	int cx = term.c.x, ocx = term.ocx, ocy = term.ocy;

	if (!xstartdraw())
		return;
	searchvalidate();
	searchmarks();
	vimvalidate();
	urlupdate();

	/* adjust cursor position */
	LIMIT(term.ocx, 0, term.col-1);
	LIMIT(term.ocy, 0, term.row-1);
	if (term.ocx > 0 && (term.line[term.ocy][term.ocx].mode & ATTR_WDUMMY))
		term.ocx--;
	if (cx > 0 && (term.line[term.c.y][cx].mode & ATTR_WDUMMY))
		cx--;

	drawregion(0, 0, term.col, term.row);
	if (sb.view_offset == 0 && !search.active && !vim.mode) {
		xdrawcursor(cx, term.c.y, term.line[term.c.y][cx],
		            term.ocx, term.ocy, term.line[term.ocy][term.ocx]);
		term.ocx = cx;
		term.ocy = term.c.y;
	}
	xfinishdraw();
	if (ocx != term.ocx || ocy != term.ocy)
		xximspot(term.ocx, term.ocy);
}

void
redraw(void)
{
	tfulldirt();
	draw();
}
