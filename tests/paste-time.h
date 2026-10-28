/* Mock the requestor windows and server timestamp round trip shared by
 * paste and mouse tests. Windows count up from 1000. */
static Window testpastenextwindow = 1000;
static int testpasteunwatched;

Window XRootWindow(Display *d, int screen) { return 2; }
/* Abandoned paste windows are kept: an owner may still write to them. */
int XDestroyWindow(Display *d, Window w) { assert(w < 1000); return 0; }

int
XSelectInput(Display *d, Window w, long mask)
{
	assert(w >= 1000 && w < testpastenextwindow && !mask);
	testpasteunwatched++;
	return 0;
}

Window
XCreateWindow(Display *d, Window parent, int x, int y, unsigned int width,
              unsigned int height, unsigned int border, int depth,
              unsigned int class, Visual *visual, unsigned long mask,
              XSetWindowAttributes *attrs)
{
	assert(parent == 2 && class == InputOnly && !depth);
	assert(mask == CWEventMask && attrs->event_mask == PropertyChangeMask);
	return testpastenextwindow++;
}

static Time testpastetime = 100;
static int testpastetimequeries;
static Atom testpastetimeproperty;

int
XChangeProperty(Display *d, Window w, Atom property, Atom type, int format,
                int mode, const unsigned char *data, int count)
{
	assert(w == pastewin && type == XA_INTEGER && format == 8);
	assert(mode == PropModeAppend && !data && !count);
	testpastetimeproperty = property;
	return 0;
}

int
XIfEvent(Display *d, XEvent *e, Bool (*predicate)(Display *, XEvent *, XPointer),
         XPointer arg)
{
	*e = (XEvent){ .xproperty = { .type = PropertyNotify, .window = pastewin,
	              .atom = testpastetimeproperty, .state = PropertyNewValue,
	              .time = testpastetime } };
	testpastetime = (uint32_t)(testpastetime + 1);
	testpastetimequeries++;
	assert(predicate(d, e, arg));
	/* Unrelated events must stay queued, including old INCR chunks. */
	e->xproperty.atom++;
	assert(!predicate(d, e, arg));
	e->xproperty.atom--;
	e->xproperty.state = PropertyDelete;
	assert(!predicate(d, e, arg));
	e->xproperty.state = PropertyNewValue;
	e->type = SelectionNotify;
	assert(!predicate(d, e, arg));
	e->type = PropertyNotify;
	return 0;
}
