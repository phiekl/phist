/* See LICENSE for license details. */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

#include "st.h"
#include "win.h"

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
#define STR_BUF_SIZ   ESC_BUF_SIZ
#define STR_ARG_SIZ   ESC_ARG_SIZ

/* macros */
#ifndef ROWVISIT
#define ROWVISIT() /* tests count row lookups to bound work per update */
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
	char *args[STR_ARG_SIZ];
	int narg;              /* nb of args */
} STREscape;

static void execsh(char *, char **);
static void stty(char **);
static void sigchld(int);
static void ttywriteraw(const char *, size_t);

static void csidump(void);
static void csihandle(void);
static int readcolonargs(char **, int, int[][CAR_PER_ARG]);
static void csiparse(void);
static void csireset(void);
static void osc_color_response(int, int, int);
static int eschandle(uchar);
static void strdump(void);
static void strhandle(void);
static void strparse(void);
static void strreset(void);

static void tprinter(char *, size_t);
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
static void treset(void);
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
static void tdectest(char );
static void tdefutf8(char);
static int32_t tdefcolor(const int *, int *, int);
static void tdeftran(char);
static void tstrsequence(uchar);

static void drawregion(int, int, int, int);

static void selnormalize(void);
static void selscroll(int, int);
static void selpushed(int, int, int);
static void selsnap(int *, int *, int);

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

typedef struct
{
	Line cells;
	int cols;
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
	return glyph->u == ' ' && glyph->mode == 0 && glyph->bg == defaultbg;
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
	free(history->buf[history->head].cells);
	history->buf[history->head] = (HistoryLine){0};
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
	Line copy;
	int tail;
	int width, stored;

	if (sb.limit <= 0)
		return;

	stored = sb_storedwidth(line, term.col);
	copy = xmalloc(stored * sizeof(Glyph));
	memcpy(copy, line, stored * sizeof(Glyph));

	if (sb.len == sb.limit)
		sb_evict(&sb);
	tail = sb_phys_index(sb.len);
	sb.buf[tail] = (HistoryLine){ copy, stored };
	sb.len++;
	width = sb_contentwidth(line, term.col);
	if (width > sb.max_width)
		sb.max_width = width;
}

/*
 * Lines narrower than the screen are padded into scratch rows instead of
 * growing in place, so history storage stays trimmed.  A returned row is
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
	if (line->cols >= term.col)
		return line->cells;
	if (scratchcols != term.col) {
		for (x = 0; x < LEN(scratch); x++) {
			free(scratch[x]);
			scratch[x] = xmalloc(term.col * sizeof(Glyph));
		}
		scratchcols = term.col;
	}
	row = scratch[next++ % LEN(scratch)];
	memcpy(row, line->cells, line->cols * sizeof(Glyph));
	for (x = line->cols; x < term.col; x++)
		row[x] = sb_padglyph();
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
			free(sb.buf[p].cells);
			sb.buf[p] = (HistoryLine){0};
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

	for (i = 0; i < sb.len; i++) {
		/* Unwrap: Accumulate physical lines into one logical line. */
		ROWVISIT();
		source = &sb.buf[sb_phys_index(i)];
		line = source->cells;
		/* Only the stored cells are needed; wrapped lines are full width. */
		stored = MIN(source->cols, term.col);
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
			nl = xmalloc(col * sizeof(Glyph));
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
			if (stored < col)
				nl = xrealloc(nl, stored * sizeof(Glyph));
			tail = (next.head + next.len) % next.cap;
			next.buf[tail] = (HistoryLine){ nl, stored };
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
	int i, p, x, n;

	loaded = MIN(loaded, sb.len - start);
	if (loaded > 0)
		urls.dirty = 1;
	new_cols = MIN(new_cols, term.col);
	for (i = 0; i < sb.len - start; i++) {
		p = sb_phys_index(start + i);
		if (i < loaded) {
			n = MIN(new_cols, sb.buf[p].cols);
			memcpy(term.line[i], sb.buf[p].cells, n * sizeof(Glyph));
			for (x = n; x < new_cols; x++)
				term.line[i][x] = sb_padglyph();
		}

		free(sb.buf[p].cells);
		sb.buf[p] = (HistoryLine){0};
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
			text[length++] = !(line[x].mode & ATTR_WDUMMY) &&
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

ssize_t
xwrite(int fd, const char *s, size_t len)
{
	size_t aux = len;
	ssize_t r;

	while (len > 0) {
		r = write(fd, s, len);
		if (r < 0)
			return r;
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
	static const char base64_digits[256] = {
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
	sel.mode = SEL_IDLE;
	sel.snap = 0;
	sel.ob.x = -1;
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

void
selstart(int col, int row, int snap)
{
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

void
selsnap(int *x, int *y, int direction)
{
	int newx, newy, xt, yt;
	int delim, prevdelim;
	const Glyph *gp, *prevgp;
	Line line;

	switch (sel.snap) {
	case SNAP_WORD:
		/*
		 * Snap around if the word wraps around at the end or
		 * beginning of a line.
		 */
		prevgp = &renderline(*y)[*x];
		prevdelim = ISDELIM(prevgp->u);
		for (;;) {
			newx = *x + direction;
			newy = *y;
			if (!BETWEEN(newx, 0, term.col - 1)) {
				newy += direction;
				newx = (newx + term.col) % term.col;
				if (!viewline(newy))
					break;

				if (direction > 0)
					yt = *y, xt = *x;
				else
					yt = newy, xt = newx;
				line = renderline(yt);
				if (!(line[xt].mode & ATTR_WRAP))
					break;
			}

			if (newx >= tlinelen_render(newy))
				break;

			gp = &renderline(newy)[newx];
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

char *
getsel(void)
{
	char *str, *ptr;
	int y, bufsize, lastx, linelen, end_idx, insert_newline, is_wrapped;
	const Glyph *gp, *last;
	Line line;

	if (sel.ob.x == -1)
		return NULL;

	bufsize = (term.col+1) * (sel.ne.y-sel.nb.y+1) * UTF_SIZ;
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

	execvp(prog, args);
	_exit(1);
}

void
sigchld(int a)
{
	int stat, olderrno;
	pid_t p;

	olderrno = errno;
	do {
		p = waitpid(pid, &stat, WNOHANG);
	} while (p < 0 && errno == EINTR);

	if (p < 0)
		_exit(1);

	if (pid != p) {
		errno = olderrno;
		return;
	}

	if ((WIFEXITED(stat) && WEXITSTATUS(stat)) || WIFSIGNALED(stat))
		_exit(1);
	_exit(0);
}

void
stty(char **args)
{
	char cmd[_POSIX_ARG_MAX], **p, *q, *s;
	size_t n, siz;

	if ((n = strlen(stty_args)) > sizeof(cmd)-1)
		die("incorrect stty parameters\n");
	memcpy(cmd, stty_args, n);
	q = cmd + n;
	siz = sizeof(cmd) - n;
	for (p = args; p && (s = *p); ++p) {
		if ((n = strlen(s)) > siz-1)
			die("stty parameter length too long\n");
		*q++ = ' ';
		memcpy(q, s, n);
		q += n;
		siz -= n + 1;
	}
	*q = '\0';
	if (system(cmd) != 0)
		perror("Couldn't call stty");
}

int
ttynew(const char *line, char *cmd, const char *out, char **args)
{
	int m, s;

	if (out) {
		term.mode |= MODE_PRINT;
		iofd = (!strcmp(out, "-")) ?
			  1 : open(out, O_WRONLY | O_CREAT, 0666);
		if (iofd < 0) {
			fprintf(stderr, "Error opening %s:%s\n",
				out, strerror(errno));
		}
	}

	if (line) {
		if ((cmdfd = open(line, O_RDWR)) < 0)
			die("open line '%s' failed: %s\n",
			    line, strerror(errno));
		dup2(cmdfd, 0);
		stty(args);
		return cmdfd;
	}

	/* seems to work fine on linux, openbsd and freebsd */
	if (openpty(&m, &s, NULL, NULL, NULL) < 0)
		die("openpty failed: %s\n", strerror(errno));

	switch (pid = fork()) {
	case -1:
		die("fork failed: %s\n", strerror(errno));
		break;
	case 0:
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
		signal(SIGCHLD, sigchld);
		break;
	}
	return cmdfd;
}

size_t
ttyread(void)
{
	static char buf[BUFSIZ];
	static int buflen = 0;
	int ret, written;

	/* append read bytes to unprocessed bytes */
	ret = read(cmdfd, buf+buflen, LEN(buf)-buflen);

	switch (ret) {
	case 0:
		exit(0);
	case -1:
		die("couldn't read from shell: %s\n", strerror(errno));
	default:
		buflen += ret;
		written = twrite(buf, buflen, 0);
		buflen -= written;
		/* keep any incomplete UTF-8 byte sequence for the next call */
		if (buflen > 0)
			memmove(buf, buf + written, buflen);
		return ret;
	}
}

void
ttyinput(const char *s, size_t n, int may_echo)
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

	if (!IS_SET(MODE_CRLF)) {
		ttywriteraw(s, n);
		return;
	}

	/* This is similar to how the kernel handles ONLCR for ttys */
	while (n > 0) {
		if (*s == '\r') {
			next = s + 1;
			ttywriteraw("\r\n", 2);
		} else {
			next = memchr(s, '\r', n);
			DEFAULT(next, s + n);
			ttywriteraw(s, next - s);
		}
		n -= next - s;
		s = next;
	}
}

void
ttywriteraw(const char *s, size_t n)
{
	fd_set wfd, rfd;
	ssize_t r;
	size_t lim = 256;

	/*
	 * Remember that we are using a pty, which might be a modem line.
	 * Writing too much will clog the line. That's why we are doing this
	 * dance.
	 * FIXME: Migrate the world to Plan 9.
	 */
	while (n > 0) {
		FD_ZERO(&wfd);
		FD_ZERO(&rfd);
		FD_SET(cmdfd, &wfd);
		FD_SET(cmdfd, &rfd);

		/* Check if we can write. */
		if (pselect(cmdfd+1, &rfd, &wfd, NULL, NULL, NULL) < 0) {
			if (errno == EINTR)
				continue;
			die("select failed: %s\n", strerror(errno));
		}
		if (FD_ISSET(cmdfd, &wfd)) {
			/*
			 * Only write the bytes written by ttywrite() or the
			 * default of 256. This seems to be a reasonable value
			 * for a serial line. Bigger values might clog the I/O.
			 */
			if ((r = write(cmdfd, s, (n < lim)? n : lim)) < 0)
				goto write_error;
			if (r < n) {
				/*
				 * We weren't able to write out everything.
				 * This means the buffer is getting full
				 * again. Empty it.
				 */
				if (n < lim)
					lim = ttyread();
				n -= r;
				s += r;
			} else {
				/* All bytes have been written. */
				break;
			}
		}
		if (FD_ISSET(cmdfd, &rfd))
			lim = ttyread();
	}
	return;

write_error:
	die("write error on tty: %s\n", strerror(errno));
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
		term.saved[alt] = term.c;
	} else if (mode == CURSOR_LOAD) {
		term.c = term.saved[alt];
		tmoveto(term.c.x, term.c.y);
	}
}

void
treset(void)
{
	uint i;

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
	int alt = IS_SET(MODE_ALTSCREEN), top = sb_maxoffset() - sb.view_offset;

	if (!alt)
		term.mainc = term.c;
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

	if (sel.ob.x == -1 || sel.alt != IS_SET(MODE_ALTSCREEN))
		return;

	if (BETWEEN(sel.nb.y, top, bot) != BETWEEN(sel.ne.y, top, bot)) {
		selclear();
	} else if (BETWEEN(sel.nb.y, top, bot)) {
		sel.ob.y += n;
		sel.oe.y += n;
		if (sel.ob.y < top || sel.ob.y > bot ||
		    sel.oe.y < top || sel.oe.y > bot) {
			selclear();
		} else {
			selnormalize();
		}
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
		if (v == LONG_MAX || v == LONG_MIN)
			v = -1;
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
		}
	} else if (term.line[y][x].mode & ATTR_WDUMMY) {
		if (x > 0) {
			term.line[y][x-1].u = ' ';
			term.line[y][x-1].mode &= ~ATTR_WIDE;
		}
	}

	tdirtyscreen(y, y);
	term.line[y][x] = *attr;
	term.line[y][x].u = u;
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
		}
	}
}

void
tdeletechar(int n)
{
	int dst, src, size;
	Glyph *line;

	LIMIT(n, 0, term.col - term.c.x);

	dst = term.c.x;
	src = term.c.x + n;
	size = term.col - src;
	line = term.line[term.c.y];

	memmove(&line[dst], &line[src], size * sizeof(Glyph));
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

	memmove(&line[dst], &line[src], size * sizeof(Glyph));
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
			fprintf(stderr,
				"erresc(%d): Incorrect number of parameters (%d)\n",
				attr[*npar], *npar);
			break;
		}
		r = attr[*npar + 2];
		g = attr[*npar + 3];
		b = attr[*npar + 4];
		*npar += 4;
		if (!BETWEEN(r, 0, 255) || !BETWEEN(g, 0, 255) || !BETWEEN(b, 0, 255))
			fprintf(stderr, "erresc: bad rgb color (%u,%u,%u)\n",
				r, g, b);
		else
			idx = TRUECOLOR(r, g, b);
		break;
	case 5: /* indexed color */
		if (*npar + 2 >= l) {
			fprintf(stderr,
				"erresc(%d): Incorrect number of parameters (%d)\n",
				attr[*npar], *npar);
			break;
		}
		*npar += 2;
		if (!BETWEEN(attr[*npar], 0, 255))
			fprintf(stderr, "erresc: bad fgcolor %d\n", attr[*npar]);
		else
			idx = attr[*npar];
		break;
	case 0: /* implemented defined (only foreground) */
	case 1: /* transparent */
	case 3: /* direct color in CMY space */
	case 4: /* direct color in CMYK space */
	default:
		fprintf(stderr, "erresc(%d): gfx attr %d unknown\n",
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
				fprintf(stderr,
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
			case 9:    /* X10 mouse compatibility mode */
				xsetpointermotion(0);
				xsetmode(0, MODE_MOUSE);
				xsetmode(set, MODE_MOUSEX10);
				break;
			case 1000: /* 1000: report button press */
				xsetpointermotion(0);
				xsetmode(0, MODE_MOUSE);
				xsetmode(set, MODE_MOUSEBTN);
				break;
			case 1002: /* 1002: report motion on button press */
				xsetpointermotion(0);
				xsetmode(0, MODE_MOUSE);
				xsetmode(set, MODE_MOUSEMOTION);
				break;
			case 1003: /* 1003: enable all mouse motions */
				xsetpointermotion(set);
				xsetmode(0, MODE_MOUSE);
				xsetmode(set, MODE_MOUSEMANY);
				break;
			case 1004: /* 1004: send focus events to tty */
				xsetmode(set, MODE_FOCUS);
				break;
			case 1006: /* 1006: extended reporting mode */
				xsetmode(set, MODE_MOUSESGR);
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
			/* Not implemented mouse modes. See comments there. */
			case 1001: /* mouse highlight mode; can hang the
				      terminal by design when implemented. */
			case 1005: /* UTF-8 mouse mode; will confuse
				      applications not supporting UTF-8
				      and luit. */
			case 1015: /* urxvt mangled mouse mode; incompatible
				      and can be mistaken for other control
				      codes. */
				break;
			default:
				fprintf(stderr,
					"erresc: unknown private set/reset mode %d\n",
					*args);
				break;
			}
		} else {
			switch (*args) {
			case 0:  /* Error (IGNORED) */
				break;
			case 2:
				xsetmode(set, MODE_KBDLOCK);
				break;
			case 4:  /* IRM -- Insertion-replacement */
				MODBIT(term.mode, set, MODE_INSERT);
				break;
			case 12: /* SRM -- Send/Receive */
				MODBIT(term.mode, !set, MODE_ECHO);
				break;
			case 20: /* LNM -- Linefeed/new line */
				MODBIT(term.mode, set, MODE_CRLF);
				break;
			default:
				fprintf(stderr,
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

	switch (csiescseq.mode[0]) {
	default:
	unknown:
		fprintf(stderr, "erresc: unknown csi ");
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
			ttywrite(vtiden, strlen(vtiden), 0);
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
			tclearregion(0, 0, term.col-1, term.row-1);
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
			ttywrite("\033[0n", sizeof("\033[0n") - 1, 0);
			break;
		case 6: /* Report Cursor Position (CPR) "<row>;<column>R" */
			len = snprintf(buf, sizeof(buf), "\033[%i;%iR",
			               term.c.y+1, term.c.x+1);
			ttywrite(buf, len, 0);
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

void
csidump(void)
{
	size_t i;
	uint c;

	fprintf(stderr, "ESC[");
	for (i = 0; i < csiescseq.len; i++) {
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
		fprintf(stderr, "erresc: failed to fetch %s color %d\n",
		        is_osc4 ? "osc4" : "osc",
		        is_osc4 ? num : index);
		return;
	}

	n = snprintf(buf, sizeof buf, "\033]%s%d;rgb:%02x%02x/%02x%02x/%02x%02x\007",
	             is_osc4 ? "4;" : "", num, r, r, g, g, b, b);
	if (n < 0 || n >= sizeof(buf)) {
		fprintf(stderr, "error: %s while printing %s response\n",
		        n < 0 ? "snprintf failed" : "truncation occurred",
		        is_osc4 ? "osc4" : "osc");
	} else {
		ttywrite(buf, n, 0);
	}
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
	strparse();
	par = (narg = strescseq.narg) ? atoi(strescseq.args[0]) : 0;

	switch (strescseq.type) {
	case ']': /* OSC -- Operating System Command */
		switch (par) {
		case 0:
			if (narg > 1) {
				xsettitle(strescseq.args[1]);
				xseticontitle(strescseq.args[1]);
			}
			return;
		case 1:
			if (narg > 1)
				xseticontitle(strescseq.args[1]);
			return;
		case 2:
			if (narg > 1)
				xsettitle(strescseq.args[1]);
			return;
		case 52: /* manipulate selection data */
			if (narg > 2 && allowwindowops) {
				dec = base64dec(strescseq.args[2]);
				if (dec) {
					xsetsel(dec);
					xclipcopy();
				} else {
					fprintf(stderr, "erresc: invalid base64\n");
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
			} else if (xsetcolorname(osc_table[j].idx, p)) {
				fprintf(stderr, "erresc: invalid %s color: %s\n",
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
			j = (narg > 1) ? atoi(strescseq.args[1]) : -1;

			if (p && !strcmp(p, "?")) {
				osc_color_response(j, 0, 1);
			} else if (xsetcolorname(j, p)) {
				if (par == 104 && narg <= 1) {
					xloadcols();
					return; /* color reset without parameter */
				}
				fprintf(stderr, "erresc: invalid color j=%d, p=%s\n",
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
				fprintf(stderr, "erresc: %s color not found\n", osc_table[j].str);
			} else {
				tfulldirt();
			}
			return;
		}
		break;
	case 'k': /* old title set compatibility */
		xsettitle(strescseq.args[0]);
		return;
	case 'P': /* DCS -- Device Control String */
	case '_': /* APC -- Application Program Command */
	case '^': /* PM -- Privacy Message */
		return;
	}

	fprintf(stderr, "erresc: unknown str ");
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

void
strdump(void)
{
	size_t i;
	uint c;

	fprintf(stderr, "ESC%c", strescseq.type);
	for (i = 0; i < strescseq.len; i++) {
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

void
sendbreak(const Arg *arg)
{
	if (tcsendbreak(cmdfd, 0))
		perror("Error sending break");
}

void
tprinter(char *s, size_t len)
{
	if (iofd != -1 && xwrite(iofd, s, len) < 0) {
		perror("Error writing to output file");
		close(iofd);
		iofd = -1;
	}
}

void
toggleprinter(const Arg *arg)
{
	term.mode ^= MODE_PRINT;
}

void
printscreen(const Arg *arg)
{
	tdump();
}

void
printsel(const Arg *arg)
{
	tdumpsel();
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
tdefutf8(char ascii)
{
	if (ascii == 'G')
		term.mode |= MODE_UTF8;
	else if (ascii == '@')
		term.mode &= ~MODE_UTF8;
}

void
tdeftran(char ascii)
{
	static char cs[] = "0B";
	static int vcs[] = {CS_GRAPHIC0, CS_USA};
	char *p;

	if ((p = strchr(cs, ascii)) == NULL) {
		fprintf(stderr, "esc unhandled charset: ESC ( %c\n", ascii);
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
tdectest(char c)
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
		ttywrite(vtiden, strlen(vtiden), 0);
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
eschandle(uchar ascii)
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
		ttywrite(vtiden, strlen(vtiden), 0);
		break;
	case 'c': /* RIS -- Reset to initial state */
		treset();
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
		fprintf(stderr, "erresc: unknown sequence ESC 0x%02X '%c'\n",
			(uchar) ascii, isprint(ascii)? ascii:'.');
		break;
	}
	return 1;
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

		if (strescseq.len+len >= strescseq.siz) {
			/*
			 * Here is a bug in terminals. If the user never sends
			 * some code to stop the str or esc command, then st
			 * will stop responding. But this is better than
			 * silently failing with unknown characters. At least
			 * then users will report back.
			 *
			 * In the case users ever get fixed, here is the code:
			 */
			/*
			 * term.esc = 0;
			 * strhandle();
			 */
			if (strescseq.siz > (SIZE_MAX - UTF_SIZ) / 2)
				return;
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
			csiescseq.buf[csiescseq.len++] = u;
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

	gp = &term.line[term.c.y][term.c.x];
	if (IS_SET(MODE_WRAP) && (term.c.state & CURSOR_WRAPNEXT)) {
		gp->mode |= ATTR_WRAP;
		tnewline(1);
		gp = &term.line[term.c.y][term.c.x];
	}

	if (IS_SET(MODE_INSERT) && term.c.x+width < term.col) {
		memmove(gp+width, gp, (term.col - term.c.x - width) * sizeof(Glyph));
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
			if (gp[1].mode == ATTR_WIDE && term.c.x+2 < term.col) {
				gp[2].u = ' ';
				gp[2].mode &= ~ATTR_WDUMMY;
			}
			gp[1].u = '\0';
			gp[1].mode = ATTR_WDUMMY;
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
	int nmarks = 2, screen_start = 0, cursor_row, altshift = 0;
	Line *tmp, *oldalt = NULL;

	urls.dirty = 1;
	if (col < 1 || row < 1) {
		fprintf(stderr,
			"tresize: error resizing to %dx%d\n", col, row);
		return;
	}
	if (col == term.col && row == term.row)
		return;
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
		for (i = 0; i < term.row; i++)
			free(term.line[i]);
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

			term.alt[i][j] = term.c.attr;
			term.alt[i][j].u = ' ';
			term.alt[i][j].mode = 0;
		}
	}
	if (oldalt) {
		for (i = 0; i < old_row; i++) {
			if (BETWEEN(i - altshift, 0, row - 1)) {
				memcpy(term.alt[i - altshift], oldalt[i],
				       MIN(col, old_col) * sizeof(Glyph));
				if (col > old_col)
					term.alt[i - altshift][old_col - 1].mode &= ~ATTR_WRAP;
			}
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
	urlupdate();

	/* adjust cursor position */
	LIMIT(term.ocx, 0, term.col-1);
	LIMIT(term.ocy, 0, term.row-1);
	if (term.ocx > 0 && (term.line[term.ocy][term.ocx].mode & ATTR_WDUMMY))
		term.ocx--;
	if (cx > 0 && (term.line[term.c.y][cx].mode & ATTR_WDUMMY))
		cx--;

	drawregion(0, 0, term.col, term.row);
	if (sb.view_offset == 0) {
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
