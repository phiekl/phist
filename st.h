/* See LICENSE for license details. */

#include <stdint.h>
#include <sys/types.h>

/* macros */
#define MIN(a, b)		((a) < (b) ? (a) : (b))
#define MAX(a, b)		((a) < (b) ? (b) : (a))
#define LEN(a)			(sizeof(a) / sizeof(a)[0])
#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))
#define DIVCEIL(n, d)		(((n) + ((d) - 1)) / (d))
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
#define LIMIT(x, a, b)		(x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)
#define ATTRCMP(a, b)		((a).mode != (b).mode || (a).fg != (b).fg || \
				(a).bg != (b).bg || (a).ucolor != (b).ucolor || \
				(a).link != (b).link)
#define TIMEDIFF(t1, t2)	((t1.tv_sec-t2.tv_sec)*1000 + \
				(t1.tv_nsec-t2.tv_nsec)/1E6)
#define MODBIT(x, set, bit)	((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))

#define TRUECOLOR(r,g,b)	(1 << 24 | (r) << 16 | (g) << 8 | (b))
#define IS_TRUECOL(x)		(1 << 24 & (x))
/* Outside the 25-bit indexed/truecolor range, fitting Glyph.ucolor. */
#define UCOLOR_DEFAULT ((1U << 26) - 1)
#define USTYLE(g) (((g).mode & ATTR_USTYLE) >> 13)
/* Wide cells default to emoji presentation, narrow ones to text. */
#define EMOJIPRES(m) (!((m) & ATTR_WIDE) != !((m) & ATTR_ALTPRES))

enum glyph_attribute {
	ATTR_NULL       = 0,
	ATTR_BOLD       = 1 << 0,
	ATTR_FAINT      = 1 << 1,
	ATTR_ITALIC     = 1 << 2,
	ATTR_UNDERLINE  = 1 << 3,
	ATTR_BLINK      = 1 << 4,
	ATTR_REVERSE    = 1 << 5,
	ATTR_INVISIBLE  = 1 << 6,
	ATTR_STRUCK     = 1 << 7,
	ATTR_WRAP       = 1 << 8,
	ATTR_WIDE       = 1 << 9,
	ATTR_WDUMMY     = 1 << 10,
	ATTR_ALTPRES    = 1 << 11, /* U+FE0E/U+FE0F flipped the width's default */
	ATTR_URL        = 1 << 12,
	ATTR_USTYLE     = 7 << 13,
	ATTR_WPAD       = 1 << 16, /* padding before a wide glyph on the next row */
	ATTR_LINKSTART  = 1 << 17, /* first glyph of an anonymous OSC 8 label */
	ATTR_BOLD_FAINT = ATTR_BOLD | ATTR_FAINT,
};

enum selection_mode {
	SEL_IDLE = 0,
	SEL_EMPTY = 1,
	SEL_READY = 2
};

enum selection_type {
	SEL_REGULAR = 1,
	SEL_RECTANGULAR = 2
};

enum selection_snap {
	SNAP_WORD = 1,
	SNAP_LINE = 2,
	SNAP_REGEX = 3 /* higher values select progressively larger matches */
};

typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned short ushort;

typedef uint_least32_t Rune;

#define Glyph Glyph_
typedef struct {
	/* Unicode needs 21 bits; colors need RGB plus the truecolor flag. */
	uint64_t u : 21;
	uint64_t mode : 18;   /* attribute flags */
	uint64_t fg : 25;
	uint64_t bg : 25;
	uint64_t ucolor : 26; /* indexed/RGB underline color or UCOLOR_DEFAULT */
	uint64_t link : 13; /* explicit OSC 8 target, zero for ordinary text */
} Glyph;

/* Keep underline colors, reflow flags and links within the original cell
 * budget. */
typedef char glyph_size_must_be_16[(sizeof(Glyph) == 16) ? 1 : -1];

typedef Glyph *Line;

typedef union {
	int i;
	uint ui;
	float f;
	const void *v;
	const char *s;
} Arg;

void die(const char *, ...);
void redraw(void);
void tfulldirt(void);
void draw(void);

int tattrset(int);
void tnew(int, int);
void tresize(int, int);
void tsetdirtattr(int);
void ttyhangup(void);
int ttynew(const char *, char *, const char *, char **, const char *);
size_t ttyread(void);
void ttyreply(const char *, size_t);
int ttywritepending(void);
void ttyflushwrites(void);
int ttyprintfd(void);
void ttyflushprint(void);
int ttywakefd(void);
void ttycheckshell(void);
void ttyresize(int, int);
void ttywrite(const char *, size_t, int);
int ttyinputfits(size_t);
void ttyinput(const char *, size_t, int);
void ttypaste(const char *, size_t, int);

void resettitle(void);

void selclear(void);
void selinit(void);
int selstart(int, int, int);
void selextend(int, int, int, int);
int selected(int, int);
char *getsel(void);
int turl(int, int);
char *tgeturl(int, int);
int thyperlink(int, int);
void thyperlinkbounds(int *, int *, int *, int *);
void tflashurl(int, int, const char *);
int turlflashed(int, int);
void tclearurlflash(void);

size_t utf8encode(Rune, char *);

void *xmalloc(size_t);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);

void kscrollup(const Arg *arg);
void kscrolldown(const Arg *arg);

/* Search queries contain at most this many UTF-8 bytes, including NUL. */
#define SEARCH_MAX 1024
void tsearchstart(void);
void tsearchend(int);
void tsearchset(const char *);
void tsearchnext(int);
void tsearchedge(int);
void tsearchpage(int);
int tsearchactive(void);
int tsearchfound(void);
int tsearchcount(int *); /* -1 when output invalidated the last result */
/* What tsearchmatch() returns for a cell in a match. */
enum { SEARCH_OTHER = 1, SEARCH_CURRENT = 2 };
int tsearchmatch(int, int);
/* The modes of keyboard selection, and the key that leaves each. */
enum { VIM_NORMAL = 1, VIM_VISUAL, VIM_LINE, VIM_BLOCK, VIM_ESCAPE = 0x1b };
/* Named keys are motions, never characters for counts or f/t commands. */
enum {
	VIM_LEFT = 0x110000, VIM_DOWN, VIM_UP, VIM_RIGHT, VIM_HOME, VIM_END,
	VIM_PAGEUP, VIM_PAGEDOWN, VIM_ENTER
};
void tvimstart(void);
void tvimend(void);
int tvimkey(Rune, int);
int tvimactive(void);
int tvimrow(void);
int tvimcursor(int, int);

/* config.h globals */
extern char *utmp;
extern char *scroll;
extern char *stty_args;
extern char *vtiden;
extern wchar_t *worddelimiters;
extern const char *selectionpatterns[];
extern int allowaltscreen;
extern int allowwindowops;
extern int allowcolorchanges;
extern char *termname;
extern unsigned int tabspaces;
extern unsigned int defaultfg;
extern unsigned int defaultbg;
extern unsigned int defaultcs;
extern unsigned int scrollback_lines;
extern const char *urlprefixes[];
extern const char urlchars[];
