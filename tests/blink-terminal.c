#include "../st.c"

void
testblink_init(void)
{
	const char *text = "\033[5mblink\033[0m";

	selinit();
	tnew(20, 4);
	twrite(text, strlen(text), 0);
}

int
testblink_dirty(void)
{
	int dirty = term.dirty[0];

	memset(term.dirty, 0, term.row * sizeof(*term.dirty));
	return dirty;
}

void
testblink_output(void)
{
	const char *text = "\033[3;1Hclock";

	twrite(text, strlen(text), 0);
}

void testblink_fd(int fd) { cmdfd = fd; }
void testblink_write(const char *s) { twrite(s, strlen(s), 0); }
int testblink_offset(void) { return sb.view_offset; }
