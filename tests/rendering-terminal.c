#include "../st.c"

Line
testrender_text(int cols, const char *text)
{
	treset();
	tresize(cols, 1);
	treset();
	twrite(text, strlen(text), 0);
	return term.line[0];
}
