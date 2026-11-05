/* Test the real terminal parser and scrollback without an X server. */
#include <assert.h>
#include <locale.h>
#include "../st.c"

char *utmp, *scroll, *stty_args, *vtiden, *termname;
wchar_t *worddelimiters = L" ";
const char *selectionpatterns[] = {
	"([0-9A-Za-z][0-9A-Za-z._@-]*[0-9A-Za-z]|[0-9A-Za-z])",
	"(((~|\\.{1,2})?/|[0-9A-Za-z_.-]+/)([0-9A-Za-z_.-]+/)*"
	"([0-9A-Za-z_.-]*[0-9A-Za-z_-]|\\.{1,2})/?)",
	"([A-Za-z_][0-9A-Za-z_]*=[^[:space:]'\"<>]*)",
	"([0-9A-Za-z][0-9A-Za-z._@,;=[:space:]-]*[0-9A-Za-z][.:]?)",
	"^(.*)$",
	NULL
};
int allowaltscreen = 1, allowwindowops, allowcolorchanges;
unsigned int tabspaces = 8, defaultfg = 7, defaultbg = 0, defaultcs = 7;
unsigned int scrollback_lines = 64;
const char *urlprefixes[] = { "http://", "https://", NULL };
const char urlchars[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
	"0123456789-._~:/?#@!$&'()*+,;=%[]";

void xbell(void) {}
void xclipcopy(void) {}
void xdrawcursor(int a, int b, Glyph c, int d, int e, Glyph f) {}
#ifndef TEST_DRAWING
void xdrawline(Line a, int b, int c, int d) {}
int xstartdraw(void) { return 0; }
#endif
void xfinishdraw(void) {}
void xloadcols(void) {}
static int colorsets, colorresets;
int xsetcolorname(int a, const char *b) { if (a < 0) return 1; b ? colorsets++ : colorresets++; return 0; }
int xgetcolor(int a, unsigned char *b, unsigned char *c, unsigned char *d) { return 0; }
static char lasttitle[1024], lasticontitle[1024];
void xseticontitle(char *s) { snprintf(lasticontitle, sizeof(lasticontitle), "%s", s ? s : ""); }
void xsettitle(char *s) { snprintf(lasttitle, sizeof(lasttitle), "%s", s ? s : ""); }
static char lastcwd[16385];
void xsetcwd(const char *s) { snprintf(lastcwd, sizeof(lastcwd), "%s", s ? s : ""); }
int xsetcursor(int a) { return 0; }
void xsetmode(int a, unsigned int b) {}
#ifndef XSETSEL
#define XSETSEL(s) /* tests may keep the text, which they then own */
#endif
void xsetsel(char *s) { XSETSEL(s); }
void xximspot(int a, int b) {}

static void
reset(int cols, int rows)
{
	static int initialized;

	if (!initialized) {
		selinit();
		tnew(cols, rows);
		initialized = 1;
	} else {
		treset();
		tresize(cols, rows);
		treset();
	}
}

static void
put(const char *text)
{
	twrite(text, strlen(text), 0);
}
