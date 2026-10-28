/* Sending selections too large for one request in increments (ICCCM 2.7.2),
 * with the X protocol mocked. */
#include <assert.h>
#include <X11/Xproto.h>
#define VERSION "test"
#define main st_main
#include "../x.c"
#undef main

enum { UTF8 = 50, TARGETS, CLIPBOARD, INCR, PROP, PROP2, OTHER };
enum { REQUESTOR = 10, GONE = 11, OWN = 12, VANISHING = 13 };

/* The server's request size in 4-byte units, with BIG-REQUESTS or without. */
static long bigrequests = 4194303, requestsize = 65535;
static long masks[32];
static struct {
	Window w;
	Atom property, type;
	int format, n;
	char data[64];
	long value;
} writes[64];
static int nwrites, nsent, previouserrors;
static XSelectionEvent sent;

int XFlush(Display *d) { return 0; }
long XExtendedMaxRequestSize(Display *d) { return bigrequests; }
long XMaxRequestSize(Display *d) { return requestsize; }

Atom
XInternAtom(Display *d, const char *name, Bool only)
{
	return !strcmp(name, "TARGETS") ? TARGETS :
	       !strcmp(name, "CLIPBOARD") ? CLIPBOARD :
	       !strcmp(name, "INCR") ? INCR :
	       !strcmp(name, "UTF8_STRING") ? UTF8 : OTHER;
}

int
XChangeProperty(Display *d, Window w, Atom property, Atom type, int format,
                int mode, const unsigned char *data, int n)
{
	assert(mode == PropModeReplace && nwrites < LEN(writes));
	writes[nwrites].w = w;
	writes[nwrites].property = property;
	writes[nwrites].type = type;
	writes[nwrites].format = format;
	writes[nwrites].n = n;
	if (format == 32) {
		assert(n == 1);
		writes[nwrites].value = *(const long *)data;
	} else {
		assert(n < (int)sizeof(writes[0].data));
		memcpy(writes[nwrites].data, data, n);
	}
	nwrites++;
	return 0;
}

int
XSelectInput(Display *d, Window w, long mask)
{
	assert(w < LEN(masks) && w != GONE);
	masks[w] = mask;
	return 0;
}

Status
XGetWindowAttributes(Display *d, Window w, XWindowAttributes *wa)
{
	/* XGetWindowAttributes sends GetWindowAttributes, then GetGeometry. For a
	 * window already gone, Xlib itself discards the second request's error. */
	if (w == GONE) {
		XErrorEvent error = { .error_code = BadWindow, .resourceid = GONE,
		                      .request_code = X_GetWindowAttributes };
		xerror(d, &error);
		return 0;
	}
	/* It only gets through if the window goes between the two requests. */
	if (w == VANISHING) {
		XErrorEvent error = { .error_code = BadDrawable, .resourceid = VANISHING,
		                      .request_code = X_GetGeometry };
		xerror(d, &error);
		return 0;
	}
	wa->your_event_mask = masks[w];
	return 1;
}

Status
XSendEvent(Display *d, Window w, Bool propagate, long mask, XEvent *e)
{
	sent = e->xselection;
	nsent++;
	return 1;
}

static int
previous(Display *d, XErrorEvent *e)
{
	previouserrors++;
	return 0;
}

static void
requesttarget(Window w, Atom selection, Atom property, Atom target)
{
	XEvent e = { .xselectionrequest = { .type = SelectionRequest,
	             .requestor = w, .selection = selection, .target = target,
	             .property = property, .time = 7 } };

	nsent = 0;
	selrequest(&e);
	assert(nsent == 1 && sent.requestor == w && sent.time == 7);
}

static void
request(Window w, Atom selection, Atom property)
{
	requesttarget(w, selection, property, UTF8);
}

static void
propertyevent(Window w, Atom property, int state)
{
	XEvent e = { .xproperty = { .type = PropertyNotify, .window = w,
	             .atom = property, .state = state } };

	xpropnotify(&e);
}

/* Delete the property as the requestor would; return the chunk written. */
static int
next(Window w, Atom property)
{
	int before = nwrites;

	propertyevent(w, property, PropertyDelete);
	assert(nwrites <= before + 1);
	return nwrites == before ? -1 : before;
}

/* Receive a whole transfer from its announcement, checking every chunk. */
static void
receive(Window w, Atom property, const char *want, size_t chunk)
{
	char got[256];
	size_t len = 0;
	int i;

	for (i = nwrites - 1; i >= 0; i--)
		if (writes[i].w == w && writes[i].property == property)
			break;
	assert(i >= 0 && writes[i].type == INCR && writes[i].format == 32);
	assert(writes[i].value == (long)strlen(want));
	assert(masks[w] & PropertyChangeMask);
	do {
		i = next(w, property);
		assert(i >= 0 && writes[i].type == UTF8 && writes[i].format == 8);
		assert((size_t)writes[i].n == MIN(chunk, strlen(want) - len));
		memcpy(got + len, writes[i].data, writes[i].n);
		len += writes[i].n;
	} while (writes[i].n);
	assert(len == strlen(want) && !memcmp(got, want, len));
}

static void
testreplacement(const char *large)
{
	static const struct {
		Atom target, property;
		const char *text;
		int format;
	} cases[] = {
		{ UTF8, PROP, "small", 8 },
		{ XA_STRING, PROP, "small", 8 },
		{ UTF8, PROP, "", 8 },
		{ UTF8, PROP, NULL, 0 },
		{ TARGETS, PROP, "small", 32 },
		{ OTHER, PROP, "small", 0 },
		{ UTF8, None, "small", 8 },
	};
	Atom property;
	int i, before;

	for (i = 0; i < LEN(cases); i++) {
		nwrites = 0;
		property = cases[i].property ? cases[i].property : cases[i].target;
		free(xsel.clipboard);
		xsel.clipboard = xstrdup(large);
		request(REQUESTOR, CLIPBOARD, property);
		assert(next(REQUESTOR, property) >= 0);
		free(xsel.clipboard);
		xsel.clipboard = cases[i].text ? xstrdup(cases[i].text) : NULL;
		before = nwrites;
		requesttarget(REQUESTOR, CLIPBOARD, cases[i].property, cases[i].target);
		assert(!selsends && masks[REQUESTOR] == KeyPressMask);
		assert(sent.property == (cases[i].format ? property : None));
		assert(nwrites == before + !!cases[i].format);
		if (cases[i].format) {
			assert(writes[before].format == cases[i].format);
			if (cases[i].format == 8) {
				assert(writes[before].type == cases[i].target);
				assert(writes[before].n == (int)strlen(cases[i].text));
				assert(!memcmp(writes[before].data, cases[i].text,
				               strlen(cases[i].text)));
			} else {
				assert(writes[before].type == XA_ATOM);
				assert(writes[before].value == UTF8);
			}
		}
		assert(next(REQUESTOR, property) == -1);
	}

	/* Replacing one property keeps another transfer's snapshot and watch. */
	nwrites = 0;
	free(xsel.clipboard);
	xsel.clipboard = xstrdup(large);
	request(REQUESTOR, CLIPBOARD, PROP);
	request(REQUESTOR, CLIPBOARD, PROP2);
	free(xsel.clipboard);
	xsel.clipboard = xstrdup("small");
	request(REQUESTOR, CLIPBOARD, PROP);
	assert(selsends && !selsends->next && selsends->property == PROP2);
	assert(masks[REQUESTOR] & PropertyChangeMask);
	assert(next(REQUESTOR, PROP) == -1);
	receive(REQUESTOR, PROP2, large, 12);
	assert(!selsends && masks[REQUESTOR] == KeyPressMask);
	free(xsel.clipboard);
	xsel.clipboard = NULL;
}

/* Limits on transfers at once, by window and in total, and on their bytes. */
static void
testlimits(const char *large)
{
	enum { FIRST = 16, WINDOWS = 5 };
	struct timespec later;
	Atom property;
	Window w;
	int i, count;

	assert(FIRST + WINDOWS <= LEN(masks) && SELSEND_MAX < WINDOWS * SELSEND_MAXPERWINDOW);
	xsel.clipboard = xstrdup(large);

	/* A window at its limit is refused, and its other transfers go on. */
	nwrites = 0;
	for (i = 0; i < SELSEND_MAXPERWINDOW; i++) {
		request(FIRST, CLIPBOARD, PROP + i);
		assert(sent.property == PROP + i);
	}
	request(FIRST, CLIPBOARD, 100);
	assert(sent.property == None && nwrites == SELSEND_MAXPERWINDOW);
	/* Requesting a property again replaces its transfer, so it still fits. */
	request(FIRST, CLIPBOARD, PROP);
	assert(sent.property == PROP);
	/* Others are not affected. */
	request(FIRST + 1, CLIPBOARD, PROP);
	assert(sent.property == PROP);

	/* Nor can many windows go past the total, and a refused one is not watched. */
	count = SELSEND_MAXPERWINDOW + 1;
	for (w = FIRST + 1; w < FIRST + WINDOWS; w++) {
		for (i = w == FIRST + 1; i < SELSEND_MAXPERWINDOW; i++) {
			nwrites = 0;
			property = PROP + i;
			request(w, CLIPBOARD, property);
			if (count < SELSEND_MAX) {
				assert(sent.property == property && nwrites == 1);
				count++;
			} else {
				assert(sent.property == None && !nwrites);
			}
		}
	}
	assert(count == SELSEND_MAX);
	masks[FIRST + WINDOWS] = KeyPressMask;
	request(FIRST + WINDOWS, CLIPBOARD, PROP);
	assert(sent.property == None && masks[FIRST + WINDOWS] == KeyPressMask);

	/* Finished transfers free their places. */
	clock_gettime(CLOCK_MONOTONIC, &later);
	later.tv_sec += SELSEND_TIMEOUT / 1000 + 1;
	assert(selsendremaining(later) == -1 && !selsends);
	request(FIRST + WINDOWS, CLIPBOARD, PROP);
	assert(sent.property == PROP);

	/* One transfer alone may be of any size; others share the byte limit. */
	assert(selsendfits(OTHER, SELSEND_MAXBYTES - selsends->len));
	assert(!selsendfits(OTHER, SELSEND_MAXBYTES - selsends->len + 1));
	assert(selsendremaining(later) == -1 && !selsends);
	assert(selsendfits(OTHER, SELSEND_MAXBYTES + 1));
	/* A copy there is no memory for is refused, without ending the terminal. */
	{
		XSelectionRequestEvent huge = { .requestor = FIRST, .property = PROP };

		assert(!selsendstart(&huge, "", (size_t)1 << 62) && !selsends);
	}
	for (w = FIRST; w <= FIRST + WINDOWS; w++)
		assert(!(masks[w] & PropertyChangeMask));
	free(xsel.clipboard);
	xsel.clipboard = NULL;
}

int
main(void)
{
	const char *small = "twelve bytes", *large = "abcdefghijklmnopqrstuvwxyz0123";
	struct timespec now = {0};
	XErrorEvent error = {0};
	int i, before, passes = 0;

	xsel.xtarget = UTF8;
	masks[REQUESTOR] = KeyPressMask;
	/* 10 units leave 12 bytes, BIG-REQUESTS or not: 28 bytes of a request
	 * go to its header and a big request's length. */
	for (i = 0; i < 2; i++) {
		bigrequests = i ? 10 : 0;
		requestsize = i ? 65535 : 10;
		assert(selmaxbytes() == 12);
		/* What fits is written at once, as before. */
		xsel.primary = xstrdup(small);
		nwrites = 0;
		request(REQUESTOR, XA_PRIMARY, PROP);
		assert(nwrites == 1 && writes[0].type == UTF8 && writes[0].n == 12);
		assert(sent.property == PROP && masks[REQUESTOR] == KeyPressMask);
		assert(next(REQUESTOR, PROP) == -1);
		/* More is sent in chunks, each once the requestor deleted the
		 * one before, ending with an empty one, from a copy of the text
		 * taken when requested. */
		free(xsel.primary);
		xsel.primary = xstrdup(large);
		request(REQUESTOR, XA_PRIMARY, PROP);
		assert(sent.property == PROP);
		free(xsel.primary);
		xsel.primary = xstrdup(small);
		/* Other properties, windows and new values do not advance it. */
		before = nwrites;
		propertyevent(REQUESTOR, PROP2, PropertyDelete);
		propertyevent(OWN, PROP, PropertyDelete);
		propertyevent(REQUESTOR, PROP, PropertyNewValue);
		assert(nwrites == before);
		receive(REQUESTOR, PROP, large, 12);
		/* The requestor's events are as they were, and it is over. */
		assert(masks[REQUESTOR] == KeyPressMask && !selsends);
		assert(next(REQUESTOR, PROP) == -1);
		free(xsel.primary);
		xsel.primary = NULL;
		passes++;
	}
	assert(passes == 2);
	testreplacement(large);

	/* CLIPBOARD too, and two transfers to one window at once: its events
	 * are restored only after both. */
	xsel.clipboard = xstrdup(large);
	request(REQUESTOR, CLIPBOARD, PROP);
	request(REQUESTOR, CLIPBOARD, PROP2);
	assert(sent.property == PROP2);
	receive(REQUESTOR, PROP2, large, 12);
	assert(masks[REQUESTOR] & PropertyChangeMask);
	receive(REQUESTOR, PROP, large, 12);
	assert(masks[REQUESTOR] == KeyPressMask && !selsends);

	/* A window phist already watches property changes on, e.g. one of its
	 * own paste windows, keeps watching them. */
	masks[OWN] = PropertyChangeMask;
	request(OWN, CLIPBOARD, PROP);
	receive(OWN, PROP, large, 12);
	assert(masks[OWN] == PropertyChangeMask);

	/* A new request for the same property starts over. */
	request(REQUESTOR, CLIPBOARD, PROP);
	assert(next(REQUESTOR, PROP) >= 0);
	request(REQUESTOR, CLIPBOARD, PROP);
	assert(selsends && !selsends->next);
	receive(REQUESTOR, PROP, large, 12);
	assert(masks[REQUESTOR] == KeyPressMask);

	/* A transfer making no progress is dropped after SELSEND_TIMEOUT. */
	assert(selsendremaining(now) == -1);
	request(REQUESTOR, CLIPBOARD, PROP);
	now = selsends->progress;
	assert(selsendremaining(now) == SELSEND_TIMEOUT);
	now.tv_sec += SELSEND_TIMEOUT / 1000;
	assert(selsendremaining(now) == -1 && !selsends);
	assert(masks[REQUESTOR] == KeyPressMask);
	assert(next(REQUESTOR, PROP) == -1);

	/* A requestor that is gone is refused, without watching it. */
	xerrorprev = previous;
	before = nwrites;
	request(GONE, CLIPBOARD, PROP);
	assert(sent.property == None && nwrites == before && !selsends);
	assert(previouserrors == 0);
	request(VANISHING, CLIPBOARD, PROP);
	assert(sent.property == None && nwrites == before && !selsends);
	assert(previouserrors == 0);

	/* Errors from vanished requestors do not end phist;
	 * other errors still go to the previous handler. */
	xerrorprev = previous;
	xw.win = OWN;
	error.error_code = BadWindow;
	error.resourceid = GONE;
	assert(xerror(NULL, &error) == 0 && previouserrors == 0);
	/* That includes a request naming no window, without -w. */
	error.resourceid = None;
	assert(xerror(NULL, &error) == 0 && previouserrors == 0);
	/* A bad window of phist's own, such as a stale -w ID, must end it. */
	error.resourceid = OWN;
	xerror(NULL, &error);
	assert(previouserrors == 1);
	embedparent = REQUESTOR;
	error.resourceid = REQUESTOR;
	xerror(NULL, &error);
	assert(previouserrors == 2);
	embedparent = 0;
	previouserrors = 0;
	error.resourceid = GONE;
	error.error_code = BadDrawable;
	error.request_code = X_GetGeometry;
	assert(xerror(NULL, &error) == 0 && previouserrors == 0);
	/* A missing drawable in phist's own rendering must remain visible. */
	error.request_code = X_PolyFillRectangle;
	xerror(NULL, &error);
	assert(previouserrors == 1);
	error.error_code = BadAlloc;
	xerror(NULL, &error);
	assert(previouserrors == 2);

	testlimits(large);
	puts("PASS: transfers limited by count, window and bytes");

	puts("PASS: large selections sent in increments, requestor events restored, stalled and vanished requestors, geometry errors ignored and rendering and own window errors forwarded");
	return 0;
}
