#include "../st.c"

/* Mark the matches in view, as drawing a frame does. */
void
testrender_marks(void)
{
	searchmarks();
}

void
testrender_clean(void)
{
	memset(term.dirty, 0, term.row * sizeof(*term.dirty));
}

int
testrender_alldirty(void)
{
	int y;

	for (y = 0; y < term.row; y++)
		if (!term.dirty[y])
			return 0;
	return 1;
}

Line
testrender_text(int cols, const char *text)
{
	treset();
	tresize(cols, 1);
	treset();
	twrite(text, strlen(text), 0);
	return term.line[0];
}
