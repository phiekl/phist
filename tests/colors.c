#include "terminal.h"

int
main(void)
{
	const char *forms[] = { "38;2;255;128;64", "38:2:255:128:64",
		"38:2::255:128:64", "38:2:0:255:128:64" };
	char text[128];
	int i;

	assert(sizeof(Glyph) == 16);
	/* Packing must preserve the highest Unicode and RGB values independently. */
	reset(80, 4);
	put("\033[38;2;255;255;255;48;2;255;255;255;58;2;255;255;255;4:5m\364\217\277\277");
	assert(term.line[0][0].u == 0x10FFFF && USTYLE(term.line[0][0]) == 5);
	assert(term.line[0][0].fg == TRUECOLOR(255, 255, 255));
	assert(term.line[0][0].bg == TRUECOLOR(255, 255, 255));
	assert(term.line[0][0].ucolor == TRUECOLOR(255, 255, 255));
	put("\033[0mX");
	assert(term.line[0][1].fg == defaultfg && term.line[0][1].bg == defaultbg);
	assert(term.line[0][1].ucolor == UCOLOR_DEFAULT);
	reset(80, 4);
	for (i = 0; i < LEN(forms); i++) {
		snprintf(text, sizeof(text), "\033[%s;1mX\033[0m", forms[i]);
		put(text);
		assert(term.line[0][i].fg == TRUECOLOR(255, 128, 64));
		assert(term.line[0][i].mode & ATTR_BOLD);
	}
	put("\033[48:2:0:1:2:3;58:2::4:5:6;4:3mX");
	assert(term.line[0][i].bg == TRUECOLOR(1, 2, 3));
	assert(term.line[0][i].ucolor == TRUECOLOR(4, 5, 6));
	assert(USTYLE(term.line[0][i]) == 3);
	put("\033[0m\033[38:5:123mX");
	assert(term.line[0][++i].fg == 123);
	put("\033[38:2:1:255:128:64;3mX");
	assert(term.line[0][++i].fg == 123 && term.line[0][i].mode & ATTR_ITALIC);
	put("\033[38:2::1:2:;1mX");
	assert(term.line[0][++i].fg == 123 && term.line[0][i].mode & ATTR_BOLD);
	put("\033[38:2:0:1:2:3:4:5:6:7;31mX");
	assert(term.line[0][++i].fg == 1);
	put("\033[0m\033[2;1HA\033[4:3m\033[0mB");
	assert(term.line[1][0].mode == 0 && term.line[1][1].mode == 0);
	assert(!ATTRCMP(term.line[1][0], term.line[1][1]));
	reset(80, 4);
	for (i = 1; i <= 5; i++) {
		snprintf(text, sizeof(text), "\033[4:%d;58:5:%dmX", i, i);
		put(text);
		assert(USTYLE(term.line[0][i - 1]) == i);
		assert(term.line[0][i - 1].ucolor == i);
	}
	put("\033[4:0;59mX");
	assert(!(term.line[0][5].mode & (ATTR_UNDERLINE | ATTR_USTYLE)));
	assert(term.line[0][5].ucolor == UCOLOR_DEFAULT);
	put("\033[4;1H\n");
	for (i = 1; i <= 5; i++)
		assert(USTYLE(sb_get(0)[i - 1]) == i && sb_get(0)[i - 1].ucolor == i);
	puts("PASS: semicolon/colon RGB, color-space fields, indexed colors and malformed input");
	return 0;
}
