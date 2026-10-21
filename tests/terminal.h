/* Test the real terminal parser and scrollback without an X server. */
#include <assert.h>
#include <locale.h>
#include "../st.c"

char *utmp, *scroll, *stty_args, *vtiden, *termname;
wchar_t *worddelimiters = L" ";
const char *selectionpatterns[] = {
	"([0-9A-Za-z][0-9A-Za-z._@-]*[0-9A-Za-z]|[0-9A-Za-z])",
	"(\\.?(/[0-9A-Za-z][0-9A-Za-z._-]*[0-9A-Za-z])+)",
	"([0-9A-Za-z][0-9A-Za-z._@,;[:space:]-]*[0-9A-Za-z][.:]?)",
	"^(.*)$",
	NULL
};
int allowaltscreen = 1, allowwindowops;
unsigned int tabspaces = 8, defaultfg = 7, defaultbg = 0, defaultcs = 7;
unsigned int scrollback_lines = 64;

void xbell(void) {}
void xclipcopy(void) {}
void xdrawcursor(int a, int b, Glyph c, int d, int e, Glyph f) {}
#ifndef TEST_DRAWING
void xdrawline(Line a, int b, int c, int d) {}
int xstartdraw(void) { return 0; }
#endif
void xfinishdraw(void) {}
void xloadcols(void) {}
int xsetcolorname(int a, const char *b) { return 0; }
int xgetcolor(int a, unsigned char *b, unsigned char *c, unsigned char *d) { return 0; }
void xseticontitle(char *s) {}
void xsettitle(char *s) {}
int xsetcursor(int a) { return 0; }
void xsetmode(int a, unsigned int b) {}
void xsetpointermotion(int a) {}
void xsetsel(char *s) {}
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
