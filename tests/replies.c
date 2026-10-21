#include <sys/socket.h>
#include "terminal.h"

int
main(void)
{
	int fds[2];
	Arg step = { .i = 1 };
	char reply[64];

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	cmdfd = fds[0];
	reset(20, 4);
	put("A\r\nB\r\nC\r\nD\r\nE");
	kscrollup(&step);
	selstart(0, 0, SNAP_WORD);
	put("\033[6n");
	assert(read(fds[1], reply, sizeof(reply)) > 0);
	assert(sb.view_offset == 1 && sel.ob.x == 0);
	ttywrite("\033[I", 3, 0);
	assert(read(fds[1], reply, sizeof(reply)) == 3);
	assert(sb.view_offset == 1 && sel.ob.x == 0);
	ttyinput("", 0, 1);
	assert(sb.view_offset == 1 && sel.ob.x == 0);
	ttyinput("x", 1, 1);
	assert(read(fds[1], reply, sizeof(reply)) == 1 && reply[0] == 'x');
	assert(sb.view_offset == 0 && sel.ob.x == 0 && sel.ob.y == -1);
	puts("PASS: automatic replies preserve history/selection; user input returns live, keeping the selection");
	return 0;
}
