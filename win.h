/* See LICENSE for license details. */

enum win_mode {
	MODE_VISIBLE     = 1 << 0,
	MODE_FOCUSED     = 1 << 1,
	MODE_APPKEYPAD   = 1 << 2,
	MODE_REVERSE     = 1 << 5,
	MODE_HIDE        = 1 << 7,
	MODE_APPCURSOR   = 1 << 8,
	MODE_8BIT        = 1 << 10,
	MODE_BLINK       = 1 << 11,
	MODE_FBLINK      = 1 << 12,
	MODE_BRCKTPASTE  = 1 << 16,
	MODE_NUMLOCK     = 1 << 17,
};

void xbell(void);
void xclipcopy(void);
void xdrawcursor(int, int, Glyph, int, int, Glyph);
void xdrawline(Line, int, int, int);
void xfinishdraw(void);
void xloadcols(void);
int xsetcolorname(int, const char *);
int xgetcolor(int, unsigned char *, unsigned char *, unsigned char *);
void xseticontitle(char *);
void xsettitle(char *);
void xsetcwd(const char *);
int xsetcursor(int);
void xsetmode(int, unsigned int);
void xsetsel(char *);
int xstartdraw(void);
void xximspot(int, int);
