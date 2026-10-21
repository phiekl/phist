#define TEST_DRAWING
#include "terminal.h"

static Rune painted[128];
static int draws[128];

void
xdrawline(Line line, int start, int y, int end)
{
	painted[y] = line[0].u;
	draws[y]++;
}

int xstartdraw(void) { return 1; }

int
main(void)
{
	Arg step = { .i = 1 };
	int i;

	reset(20, 4);
	put("A\r\nB\r\nC\r\nD\r\nE");
	kscrollup(&step);
	assert(painted[0] == 'A' && painted[3] == 'D');
	memset(draws, 0, sizeof(draws));
	put("\033[3;1HX");
	draw();
	assert(painted[3] == 'X' && draws[3] == 1 && draws[2] == 0);
	memset(draws, 0, sizeof(draws));
	put("\033[4;1HY");
	draw();
	for (i = 0; i < term.row; i++)
		assert(!draws[i]);

	selstart(0, 0, SNAP_WORD);
	put("\033[1;1H\033[K");
	assert(sel.ob.x == 0); /* Erasing live row 0 must not clear history row 0. */
	draw();
	assert(painted[0] == 'A' && painted[1] == ' ');
	put("\033[1;1HX");
	assert(sel.ob.x == 0); /* Writing live row 0 must leave history selected. */
	selstart(0, 2, SNAP_WORD);
	put("\033[2;1HY");
	assert(sel.ob.x == -1); /* Overwriting selected live text clears it. */
	reset(5, 4);
	put("ABCDEFGHIJ");
	selstart(4, 1, SNAP_WORD);
	put("K");
	assert(sel.ob.x >= 0 && term.line[2][0].u == 'K');

	reset(20, 4);
	for (i = 0; i < 70; i++)
		put("x\r\n");
	step.i = 1000;
	kscrollup(&step);
	selclear();
	draw();
	memset(draws, 0, sizeof(draws));
	put("next\r\n");
	draw();
	for (i = 0; i < term.row; i++)
		assert(draws[i]); /* Eviction moves the oldest visible history row. */

	reset(20, 4);
	put("\033[5m\033[4;20HX");
	draw();
	assert(tattrset(ATTR_BLINK));
	memset(draws, 0, sizeof(draws));
	tsetdirtattr(ATTR_BLINK);
	draw();
	assert(draws[3] == 1);
	reset(20, 4);
	put("\033[5m\033[1;20HX\033[0m\033[4;1H\n");
	assert(!tattrset(ATTR_BLINK));
	step.i = 1;
	kscrollup(&step);
	assert(tattrset(ATTR_BLINK));
	memset(draws, 0, sizeof(draws));
	tsetdirtattr(ATTR_BLINK);
	draw();
	assert(draws[0] == 1 && !draws[1]);
	puts("PASS: viewport dirty rows, hidden edits, history selection and ring eviction");
	return 0;
}
