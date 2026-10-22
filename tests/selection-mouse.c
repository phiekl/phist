#include <assert.h>
#define VERSION "test"
#define main st_main
#include "../x.c"
#undef main

int
main(void)
{
	struct timespec now = { .tv_sec = 100 };
	int i;

	assert(clicksnap(4, 2, 0, now) == 0);
	now.tv_nsec += 100000000;
	assert(clicksnap(4, 2, Button1Mask, now) == SNAP_REGEX);
	now.tv_nsec += 100000000;
	assert(clicksnap(4, 2, 0, now) == SNAP_REGEX + 1);
	now.tv_nsec += 100000000;
	assert(clicksnap(4, 2, LockMask, now) == SNAP_REGEX + 2);
	now.tv_nsec += 100000000;
	assert(clicksnap(4, 2, 0, now) == SNAP_REGEX + 3);
	for (i = 0; i < 100; i++)
		clicksnap(4, 2, 0, now);
	assert(xsel.clicks <= (int)LEN(selectionpatterns) + 2);

	/* Different cells, modifiers or elapsed time start a fresh selection. */
	assert(clicksnap(5, 2, 0, now) == 0);
	assert(clicksnap(5, 3, 0, now) == 0);
	assert(clicksnap(5, 3, ShiftMask, now) == 0);
	assert(clicksnap(5, 3, ShiftMask, now) == SNAP_REGEX);
	now.tv_sec += 2;
	assert(clicksnap(5, 3, ShiftMask, now) == 0);

	/* The initial double click and subsequent clicks honor their timeouts. */
	now.tv_nsec += (doubleclicktimeout + 1) * 1000000;
	assert(clicksnap(5, 3, ShiftMask, now) == 0);
	assert(clicksnap(5, 3, ShiftMask, now) == SNAP_REGEX);
	now.tv_sec += 1;
	assert(clicksnap(5, 3, ShiftMask, now) == 0);

	puts("PASS: multi-click timing, position, modifiers, expansion and saturation");
	return 0;
}
