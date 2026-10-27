/* Expose terminal state to the mouse event regression checks. */
#include "../st.c"

void
testmouse_init(int fd, int cols, int rows)
{
	cmdfd = fd;
	selinit();
	tnew(cols, rows);
}

void
testmouse_put(const char *text)
{
	twrite(text, strlen(text), 0);
}

int
testmouse_alldirty(void)
{
	int y;

	for (y = 0; y < term.row; y++)
		if (!term.dirty[y])
			return 0;
	return 1;
}

int
testmouse_offset(void)
{
	return sb.view_offset;
}
