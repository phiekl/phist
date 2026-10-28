/* See LICENSE for license details. */
/*
 * Paste confirmation: phist collects a whole selection before any of it
 * reaches the tty, and asks in a dialog drawn over the terminal what to do
 * with newlines, tabs, control characters and invalid UTF-8. See
 * doc/paste-spec.md.
 *
 * Included by x.c, whose window, drawing context and helpers it uses; it
 * is not compiled on its own.
 */


enum { PASTE_IDLE, PASTE_WAIT, PASTE_INCR, PASTE_CONFIRM, PASTE_SEND };
/* Choices, by key: s joins everything into one line, p keeps lines with tabs
 * as spaces, t keeps lines and tabs, k keeps the original. Keys
 * work in either case and are shown in uppercase. */
enum { PASTE_SAFE, PASTE_PLAIN, PASTE_TABBED, PASTE_RAW, PASTE_MODES };
static const char pastekeys[] = "sptk";
/* Character classes; DANGER is every other control, lone CR or bad byte. */
enum { PASTE_ISLF, PASTE_ISCRLF, PASTE_ISTAB, PASTE_ISDANGER, PASTE_ISSPACE,
       PASTE_ISTEXT };
/* Changes a transformation made, for the labels. */
enum {
	PASTE_DANGER = 1,   /* DANGER became a space or was dropped */
	PASTE_CRLF = 2,     /* CR+LF became LF */
	PASTE_TABS = 4,     /* tabs became spaces (p) or were kept (t) */
	PASTE_TRIM = 8,     /* blanks were trimmed */
	PASTE_SQUEEZE = 16, /* runs of spaces became one (s) */
	PASTE_KEEPLF = 32,  /* LF was kept */
};
typedef struct {
	char key; /* one of pastekeys */
	int mode;
	char label[128];
} PasteOption;
static struct {
	int state, finished;
	/* The owner's reply was read, so if the paste is abandoned, the rest
	 * of an INCR transfer is drained, or the owner of a direct reply is
	 * done with the window. */
	int answered;
	Window window;
	Atom selection, property, type;
	Time time;
	char *data;
	size_t len, cap;
	size_t lf, tabs, crlf, danger; /* LF counts only outside CR+LF */
	size_t chars; /* characters, each invalid byte as one */
	struct timespec progress, shown;
	PasteOption options[PASTE_MODES];
	int noptions, selected; /* -1 selects the preview */
	int viewing, viewcols, viewrows; /* full view and its last layout */
	int viewx, viewy; /* where the full view's text starts */
	/* Selection anchor and pointer, each expressed as a character's byte range.
	 * A position past the row's text has an empty range at its end. */
	int marking, marked;
	size_t markanchor[2], markpoint[2];
	int copied; /* the full view's text was just copied */
	int asking; /* whether to paste unmodified, after choosing k */
	int askyes; /* Yes rather than No is selected in the question */
	size_t viewtop; /* offset of the first row in the full view */
	/* Starts of the rows of the line from rowsline to rowsend, wrapped at
	 * rowscols, so that scrolling up a long line need not wrap it again
	 * from its start at every step. */
	size_t *rows, nrows, rowscap, rowsline, rowsend;
	int rowscols;
} paste;
/* Conversions are requested into the property _ST_PASTE of a window of
 * phist's own, which watches its property changes. The window is reused while
 * every conversion to it finishes. An abandoned owner may still write a
 * late reply or INCR chunks to it, even over a later reply before phist reads
 * it, so later requests use a new window. Abandoned windows are kept, as an
 * owner writing to a destroyed one could fail with BadWindow; unlike atoms,
 * windows are freed when phist exits. */
static Window pastewin;
static Atom pasteproperty;
/* An INCR transfer abandoned while its owner still sends chunks, e.g. one
 * past pastemaxbytes. Owners such as xclip ignore further requests from phist
 * until it ends, so its chunks are deleted unread until the empty last one,
 * and a new request waits for that, with property None meanwhile. An owner
 * that never ends must not hold later pastes back, so a drain has a total limit
 * besides the timeout between chunks. */
#define PASTE_DRAINMS (4.0 * pastetimeout)
static struct {
	Window window;
	struct timespec started, progress;
} pastedrain;

static Bool
pastetimeevent(Display *dpy, XEvent *e, XPointer arg)
{
	Atom property = *(Atom *)arg;

	(void)dpy;
	return e->type == PropertyNotify && e->xproperty.window == pastewin &&
	       e->xproperty.atom == property && e->xproperty.state == PropertyNewValue;
}

/* Obtain a real server timestamp, also for pastes without an input event.
 * Never synthesize a future time: owners may refuse it. If two requests
 * fall in one server millisecond, wait for the next tick so even refusals
 * (which name no property) can be matched unambiguously. */
static Time
pastetime(void)
{
	static Atom property;
	static Time last;
	XEvent e;

	if (!property)
		property = XInternAtom(xw.dpy, "_ST_PASTE_TIME", False);
	for (;;) {
		XChangeProperty(xw.dpy, pastewin, property, XA_INTEGER, 8,
		                PropModeAppend, NULL, 0);
		XIfEvent(xw.dpy, &e, pastetimeevent, (XPointer)&property);
		if (e.xproperty.time != CurrentTime && e.xproperty.time != last)
			break;
		nanosleep(&(struct timespec){ .tv_nsec = 1000000 }, NULL);
	}
	return last = e.xproperty.time;
}

/* Why the last paste failed, shown in a red box for PASTE_FAILMS unless
 * dismissed; empty while there is none. It outlives the paste state. */
enum { PASTE_FAILMS = 5000 };
static struct {
	char text[64];
	struct timespec shown;
} pastefail;

static void
pastefailed(const char *text)
{
	fprintf(stderr, "Paste failed: %s\n", text);
	snprintf(pastefail.text, sizeof(pastefail.text), "%s", text);
	clock_gettime(CLOCK_MONOTONIC, &pastefail.shown);
}

static void
pastefailedsize(void)
{
	char text[64];

	snprintf(text, sizeof(text), "The paste is larger than %zu bytes", pastemaxbytes);
	pastefailed(text);
}

static void
pastefailclear(void)
{
	pastefail.text[0] = '\0';
}

/* Whether the dialog or the failure box takes all keys and clicks. */
static int
pastemodal(void)
{
	return paste.state == PASTE_CONFIRM || pastefail.text[0];
}

/* Whether a binding starts a paste, which may replace the failure box. */
static int
pasteaction(void (*func)(const Arg *))
{
	return func == clippaste || func == selpaste;
}

/* Whether a mouse binding matches the event, by the rules of mouseaction(). */
static int
pastemousematch(const MouseShortcut *ms, XEvent *e)
{
	uint state = e->xbutton.state & ~buttonmask(e->xbutton.button);

	return ms->button == e->xbutton.button &&
	       (match(ms->mod, state) || match(ms->mod, state & ~forcemousemod));
}

/* Whether a mouse press matches a paste binding, on press or release, so
 * that it may retry through the failure box. */
static int
pastemousebinding(XEvent *e)
{
	MouseShortcut *ms;

	for (ms = mshortcuts; ms < mshortcuts + LEN(mshortcuts); ms++)
		if (pasteaction(ms->func) && pastemousematch(ms, e))
			return 1;
	return 0;
}

/* Run the paste binding, and only a paste binding, matching a retrying
 * click's press or release. */
static void
pastemouseaction(XEvent *e, uint release)
{
	MouseShortcut *ms;

	for (ms = mshortcuts; ms < mshortcuts + LEN(mshortcuts); ms++) {
		if (ms->release == release && pasteaction(ms->func) &&
		    pastemousematch(ms, e)) {
			ms->func(&ms->arg);
			return;
		}
	}
}

/* Use a new window for later requests, after one was abandoned. */
static void
pasteretire(void)
{
	XSelectInput(xw.dpy, pastewin, NoEventMask);
	pastewin = None;
}

static void
pasterequest(void)
{
	XSetWindowAttributes attrs = { .event_mask = PropertyChangeMask };

	if (!pasteproperty)
		pasteproperty = XInternAtom(xw.dpy, "_ST_PASTE", 0);
	if (!pastewin)
		pastewin = XCreateWindow(xw.dpy, XRootWindow(xw.dpy, xw.scr),
		                         -1, -1, 1, 1, 0, 0, InputOnly,
		                         CopyFromParent, CWEventMask, &attrs);
	paste.window = pastewin;
	paste.property = pasteproperty;
	paste.time = pastetime();
	clock_gettime(CLOCK_MONOTONIC, &paste.progress);
	XConvertSelection(xw.dpy, paste.selection, xsel.xtarget, paste.property,
			paste.window, paste.time);
}

void
pastestart(Atom selection)
{
	if (paste.state != PASTE_IDLE)
		return;
	pastefailclear(); /* a new paste replaces the box */
	paste.state = PASTE_WAIT;
	paste.selection = selection;
	clock_gettime(CLOCK_MONOTONIC, &paste.progress);
	if (!pastedrain.window)
		pasterequest();
}

/* End a drain, after its last chunk or without one, and send a request
 * that waited for it. */
static void
pastedrainend(int finished)
{
	if (!finished)
		pasteretire();
	pastedrain.window = None;
	if (paste.state == PASTE_WAIT && !paste.property)
		pasterequest();
}

static void
pastedrainchunk(void)
{
	ulong nitems, rem;
	int format, error;
	uchar *data = NULL;
	Atom type;

	error = XGetWindowProperty(xw.dpy, pastedrain.window, pasteproperty, 0, 0,
	                           False, AnyPropertyType, &type, &format,
	                           &nitems, &rem, &data);
	XFree(data);
	if (error) {
		pastedrainend(0);
		return;
	}
	if (type == None) /* deleted already */
		return;
	XDeleteProperty(xw.dpy, pastedrain.window, pasteproperty);
	clock_gettime(CLOCK_MONOTONIC, &pastedrain.progress);
	if (!nitems && !rem)
		pastedrainend(1);
}

void
pasteclear(void)
{
	if (paste.state == PASTE_INCR && paste.answered) {
		/* Deleting the chunk read last asks for the next one. */
		pastedrain.window = paste.window;
		clock_gettime(CLOCK_MONOTONIC, &pastedrain.progress);
		pastedrain.started = pastedrain.progress;
		paste.finished = 1;
	} else if (paste.state == PASTE_WAIT && paste.answered) {
		/* The owner wrote its whole reply and is done with it. */
		paste.finished = 1;
	}
	if ((paste.state == PASTE_WAIT || paste.state == PASTE_INCR) &&
	    paste.property)
		XDeleteProperty(xw.dpy, paste.window, paste.property);
	if (paste.property && !paste.finished)
		pasteretire();
	free(paste.data);
	free(paste.rows);
	memset(&paste, 0, sizeof(paste));
}

double
pasteremaining(struct timespec now)
{
	double drain = -1, remaining;

	if (pastedrain.window) {
		drain = MIN(pastetimeout - TIMEDIFF(now, pastedrain.progress),
		            PASTE_DRAINMS - TIMEDIFF(now, pastedrain.started));
		if (drain <= 0) {
			pastedrainend(0);
			paste.progress = now; /* of a request it sent */
			drain = -1;
		}
	}
	if (pastefail.text[0]) {
		remaining = PASTE_FAILMS - TIMEDIFF(now, pastefail.shown);
		if (remaining > 0)
			return drain < 0 ? remaining : MIN(drain, remaining);
		pastefailclear();
		return drain;
	}
	/* A request waiting for a drain is sent when the drain ends. */
	if ((paste.state != PASTE_WAIT && paste.state != PASTE_INCR) ||
	    !paste.property)
		return drain;
	remaining = pastetimeout - TIMEDIFF(now, paste.progress);
	if (remaining > 0)
		return remaining;
	pasteclear();
	pastefailed("The selection owner stopped answering");
	return -1;
}

static int
pastecontrol(uchar c)
{
	return c < 0x20 || c == 0x7f;
}

/* Decode one UTF-8 character independently of the locale, rejecting
 * overlong forms, surrogates and code points above U+10FFFF. Return its
 * length, or 0 for an invalid or incomplete sequence. */
static size_t
pasteutf8(const char *s, size_t n, Rune *u)
{
	const uchar *b = (const uchar *)s;
	size_t i, len;
	Rune min;

	if (b[0] < 0x80) {
		*u = b[0];
		return 1;
	} else if (b[0] >= 0xc2 && b[0] <= 0xdf) {
		len = 2, min = 0x80, *u = b[0] & 0x1f;
	} else if (b[0] >= 0xe0 && b[0] <= 0xef) {
		len = 3, min = 0x800, *u = b[0] & 0x0f;
	} else if (b[0] >= 0xf0 && b[0] <= 0xf4) {
		len = 4, min = 0x10000, *u = b[0] & 0x07;
	} else {
		return 0;
	}
	if (n < len)
		return 0;
	for (i = 1; i < len; i++) {
		if ((b[i] & 0xc0) != 0x80)
			return 0;
		*u = *u << 6 | (b[i] & 0x3f);
	}
	if (*u < min || *u > 0x10ffff || (*u >= 0xd800 && *u <= 0xdfff))
		return 0;
	return len;
}

/* Classify the character at offset i and store its length in *n. DANGER
 * is a C0 control other than tab and newline, a CR outside CR+LF (it acts
 * like Enter), DEL, a C1 control or any byte outside a valid UTF-8 sequence,
 * e.g. a raw 0x9b CSI. */
static int
pasteclass(size_t i, size_t *n)
{
	const char *s = paste.data + i;
	Rune u;

	if (!(*n = pasteutf8(s, paste.len - i, &u))) {
		*n = 1;
		return PASTE_ISDANGER;
	}
	if (u == '\r' && i + 1 < paste.len && s[1] == '\n') {
		*n = 2;
		return PASTE_ISCRLF;
	}
	if (u == '\n')
		return PASTE_ISLF;
	if (u == '\t')
		return PASTE_ISTAB;
	if (u == ' ')
		return PASTE_ISSPACE;
	if (u < 0x20 || (u >= 0x7f && u <= 0x9f))
		return PASTE_ISDANGER;
	return PASTE_ISTEXT;
}

/* Drop spaces and tabs at the end of out. */
static size_t
pastetrim(char *out, size_t len, int *done)
{
	size_t n = len;

	while (n && (out[n - 1] == ' ' || out[n - 1] == '\t'))
		n--;
	if (n < len)
		*done |= PASTE_TRIM;
	return n;
}

static int
pasteblank(int class)
{
	return class != PASTE_ISTEXT && class != PASTE_ISDANGER;
}

/* Apply a choice to the paste, writing at most 4 * paste.len bytes to out.
 * Return the length of the result; *done is set to the changes made. */
static size_t
pastetransform(int mode, char *out, int *done)
{
	size_t i, n, m, len = 0;
	int class, squeezed = 0;

	*done = 0;
	for (i = 0; i < paste.len; i += n) {
		class = pasteclass(i, &n);
		if (mode == PASTE_RAW || class == PASTE_ISTEXT ||
		    (class == PASTE_ISSPACE && mode != PASTE_SAFE)) {
			memcpy(out + len, paste.data + i, n);
			len += n;
			/* Squeezing at the end only counts as trimming. */
			if (squeezed)
				*done |= PASTE_SQUEEZE;
			squeezed = 0;
		} else if (mode == PASTE_SAFE) {
			/* One line: every blank and DANGER becomes a space, runs of
			 * them become one and none is left at either end. */
			if (!len)
				*done |= PASTE_TRIM;
			else if (out[len - 1] == ' ')
				squeezed = 1;
			else
				out[len++] = ' ';
		} else if (class == PASTE_ISLF || class == PASTE_ISCRLF) {
			len = pastetrim(out, len, done);
			out[len++] = '\n';
			*done |= class == PASTE_ISCRLF ? PASTE_CRLF : PASTE_KEEPLF;
		} else if (class == PASTE_ISTAB) {
			*done |= PASTE_TABS;
			if (mode == PASTE_PLAIN) {
				memset(out + len, ' ', 4);
				len += 4;
			} else {
				out[len++] = '\t';
			}
		} else {
			/* A run of DANGER becomes one space, unless it is next to a
			 * blank or at either end of the paste. */
			*done |= PASTE_DANGER;
			while (i + n < paste.len && pasteclass(i + n, &m) == PASTE_ISDANGER)
				n += m;
			if (len && out[len - 1] != ' ' && out[len - 1] != '\t' &&
			    out[len - 1] != '\n' && i + n < paste.len &&
			    !pasteblank(pasteclass(i + n, &m)))
				out[len++] = ' ';
		}
	}
	if (mode == PASTE_SAFE && len && out[len - 1] == ' ') {
		len--;
		*done |= PASTE_TRIM;
	} else if (mode != PASTE_RAW) {
		/* At most one newline is left at the end; lines before LF are
		 * already trimmed, so blank lines end in LF alone. */
		len = pastetrim(out, len, done);
		for (; len > 1 && out[len - 1] == '\n' && out[len - 2] == '\n'; len--)
			*done |= PASTE_TRIM;
	}
	return len;
}

static void
pastesend(int mode)
{
	int bracketed = IS_SET(MODE_BRCKTPASTE), done;
	size_t i, j, len;
	char *text;

	text = xmalloc(MAX(paste.len, 1) * 4);
	paste.state = PASTE_SEND;
	len = pastetransform(mode, text, &done);
	/* Search owns input until it closes, including confirmed clipboard data. */
	if (tsearchactive()) {
		searchinsert(text, len);
		free(text);
		pasteclear();
		return;
	}
	for (i = j = 0; i < len; i++) {
		/* ESC could terminate a bracketed paste early. */
		if (bracketed && text[i] == '\033')
			continue;
		text[j++] = text[i] == '\n' ? '\r' : text[i];
	}
	/* All of it or none: a shell must not run the start of a paste. */
	if (!ttyinputfits(j + (bracketed ? 12 : 0))) {
		free(text);
		pasteclear();
		pastefailed("The program is not reading its input");
		return;
	}
	blinkreset();
	/* The mode is decided once, before any of it is queued, for both markers. */
	if (bracketed)
		ttypaste("\033[200~", 6, 0);
	if (j)
		ttypaste(text, j, 1);
	if (bracketed)
		ttypaste("\033[201~", 6, 0);
	free(text);
	pasteclear();
}

/* Label a choice that pastes the original with what it keeps that others
 * change, in the order of their labels, e.g. "Keep DANGER, keep TAB".
 * trims tells whether p trims trailing space. */
static void
pastekeeps(char *label, int trims)
{
	const struct { int kept; const char *what; } kinds[] = {
		{ paste.danger > 0, "DANGER" }, { paste.lf > 0, "LF" },
		{ paste.crlf > 0, "CRLF" }, { trims, "trailing space" },
		{ paste.tabs > 0, "TAB" },
	};
	size_t i;

	label[0] = '\0';
	for (i = 0; i < LEN(kinds); i++) {
		if (!kinds[i].kept)
			continue;
		if (label[0])
			strcat(label, ", ");
		strcat(label, "keep ");
		strcat(label, kinds[i].what);
	}
	label[0] = toupper((uchar)label[0]);
}

/* Offer s and p, t when there are tabs, and k unless p or t would paste the
 * original anyway. A choice that pastes the original says what it keeps.
 * The first choice, the safest, is selected initially. */
static void
pasteoptions(void)
{
	/* Steps p and t share come first, so only the tab step differs. */
	static const struct { int change; const char *p, *t; } steps[] = {
		{ PASTE_DANGER, "DANGER → SPACE", "DANGER → SPACE" },
		{ PASTE_KEEPLF, "keep LF", "keep LF" },
		{ PASTE_CRLF, "CRLF → LF", "CRLF → LF" },
		{ PASTE_TRIM, "trim trailing space", "trim trailing space" },
		{ PASTE_TABS, "TAB → 4x SPACE", "keep TAB" },
	};
	const struct { size_t count; const char *name; } kinds[] = {
		{ paste.lf, "LF" }, { paste.tabs, "TAB" },
		{ paste.crlf, "CRLF" }, { paste.danger, "DANGER" },
	};
	char *text = xmalloc(MAX(paste.len, 1) * 4), *label;
	int mode, done, unmodified = 0, trims = 0;
	size_t len, i;
	PasteOption *o;

	paste.noptions = 0;
	for (mode = 0; mode < PASTE_MODES; mode++) {
		if ((mode == PASTE_TABBED && !paste.tabs) ||
		    (mode == PASTE_RAW && unmodified))
			continue;
		o = &paste.options[paste.noptions++];
		o->key = pastekeys[mode];
		o->mode = mode;
		label = o->label;
		label[0] = '\0';
		if (mode == PASTE_RAW) {
			pastekeeps(label, trims);
			continue;
		}
		len = pastetransform(mode, text, &done);
		if (mode == PASTE_PLAIN)
			trims = (done & PASTE_TRIM) != 0;
		if (len == paste.len && !memcmp(text, paste.data, len)) {
			pastekeeps(label, trims);
			unmodified = 1;
		} else if (mode == PASTE_SAFE) {
			for (i = 0; i < LEN(kinds); i++) {
				if (!kinds[i].count)
					continue;
				if (label[0])
					strcat(label, "|");
				strcat(label, kinds[i].name);
			}
			strcat(label, " → SPACE");
			if (done & PASTE_SQUEEZE)
				strcat(label, ", squeeze all space");
			if (done & PASTE_TRIM)
				strcat(label, ", trim surrounding space");
		} else {
			for (i = 0; i < LEN(steps); i++) {
				if (!(done & steps[i].change))
					continue;
				if (label[0])
					strcat(label, ", ");
				strcat(label, mode == PASTE_PLAIN ? steps[i].p : steps[i].t);
			}
		}
		label[0] = toupper((uchar)label[0]);
	}
	free(text);
	paste.selected = 0;
	paste.viewing = 0;
	clock_gettime(CLOCK_MONOTONIC, &paste.shown);
}

static void
pastecomplete(void)
{
	size_t i, n;

	paste.finished = 1;

	/* Classify whole characters, which may span INCR chunks. */
	for (i = 0; i < paste.len; i += n) {
		paste.chars++;
		switch (pasteclass(i, &n)) {
		case PASTE_ISLF: paste.lf++; break;
		case PASTE_ISCRLF: paste.crlf++; paste.chars++; break;
		case PASTE_ISTAB: paste.tabs++; break;
		case PASTE_ISDANGER: paste.danger++; break;
		}
	}
	if (!paste.len) {
		/* No markers for an empty selection or an INCR terminator. */
		paste.state = PASTE_SEND;
		pasteclear();
	} else if (paste.lf || paste.tabs || paste.crlf || paste.danger) {
		pasteoptions();
		paste.state = PASTE_CONFIRM;
	} else {
		pastesend(PASTE_RAW);
	}
}

/* Escape one character of the paste for display: controls, invalid UTF-8
 * and zero-width characters. Decode complete characters so UTF-8
 * continuation bytes are never treated as C1 controls. Return the bytes
 * consumed; out receives the text and *width its columns. */
static size_t
pasteunit(size_t i, char out[16], int *width)
{
	const char *s = paste.data + i;
	Rune u;
	size_t n = pasteutf8(s, paste.len - i, &u);
	uchar c = *s;

	if (c == '\n' || c == '\r' || c == '\t' || c == '\\') {
		out[0] = '\\';
		out[1] = c == '\n' ? 'n' : c == '\r' ? 'r' : c == '\t' ? 't' : '\\';
		out[2] = '\0';
		*width = 2;
		return 1;
	}
	if (!n || pastecontrol(c)) {
		snprintf(out, 16, "\\x%02x", c);
		*width = 4;
		return 1;
	}
	if ((u >= 0x80 && u <= 0x9f) || !iswprint(u) || (*width = wcwidth(u)) <= 0) {
		snprintf(out, 16, "\\u%04x", (unsigned int)u);
		*width = strlen(out);
		return n;
	}
	memcpy(out, s, n);
	out[n] = '\0';
	return n;
}

/* One line of escaped text, leaving room for an explicit truncation marker. */
static void
pastepreview(char *buf, int cols)
{
	size_t i = 0, out = 0, n;
	int width, used = 0, limit = cols >= 3 ? cols - 3 : cols;
	char unit[16];

	while (i < paste.len) {
		n = pasteunit(i, unit, &width);
		if (used + width > limit)
			break;
		out += sprintf(buf + out, "%s", unit);
		used += width;
		i += n;
	}
	if (i < paste.len) {
		n = MIN(3, cols - used);
		memcpy(buf + out, "...", n);
		out += n;
	}
	buf[out] = '\0';
}

/* Fill one row of the full view starting at offset i, wrapping at cols and
 * after each newline. Return where the next row starts. buf may be NULL. */
static size_t
pasterow(size_t i, int cols, char *buf)
{
	size_t n, out = 0;
	int width, used = 0;
	char unit[16];

	while (i < paste.len) {
		n = pasteunit(i, unit, &width);
		if (used && used + width > cols)
			break;
		if (buf)
			out += sprintf(buf + out, "%s", unit);
		used += width;
		i += n;
		if (paste.data[i - n] == '\n')
			break;
	}
	if (buf)
		buf[out] = '\0';
	return i;
}

/* Return the start of the row before the one starting at top, from the
 * rows of the line holding it, which are wrapped once per line and width. */
static size_t
pasterowbefore(size_t top, int cols)
{
	size_t line, row, lo, hi, mid;

	if (!top)
		return 0;
	if (!paste.rows || paste.rowscols != cols || top <= paste.rowsline ||
	    top > paste.rowsend) {
		for (line = top - 1; line > 0 && paste.data[line - 1] != '\n'; line--)
			;
		paste.nrows = 0;
		for (row = line;;) {
			if (paste.nrows == paste.rowscap) {
				paste.rowscap = MAX(64, paste.rowscap * 2);
				paste.rows = xrealloc(paste.rows,
				                      paste.rowscap * sizeof(*paste.rows));
			}
			paste.rows[paste.nrows++] = row;
			row = pasterow(row, cols, NULL);
			if (row >= paste.len || paste.data[row - 1] == '\n')
				break;
		}
		paste.rowsline = line;
		paste.rowsend = row;
		paste.rowscols = cols;
	}
	/* The last row starting before top; the first always does. */
	for (lo = 0, hi = paste.nrows; hi - lo > 1;) {
		mid = lo + (hi - lo) / 2;
		if (paste.rows[mid] < top)
			lo = mid;
		else
			hi = mid;
	}
	return paste.rows[lo];
}

static void
pastescroll(int rows)
{
	size_t next;

	for (; rows < 0 && paste.viewtop; rows++)
		paste.viewtop = pasterowbefore(paste.viewtop, paste.viewcols);
	for (; rows > 0; rows--) {
		next = pasterow(paste.viewtop, paste.viewcols, NULL);
		if (next >= paste.len)
			break;
		paste.viewtop = next;
	}
}

/* The escaped text from i to end as in the full view, or with newlines as
 * they are if lines is set, for copying. buf needs room for 5 bytes per
 * byte of the paste and a terminator. Return the columns it takes. */
static int
pastetext(size_t i, size_t end, char *buf, int lines)
{
	size_t out = 0;
	int width, used = 0;

	while (i < end) {
		if (lines && paste.data[i] == '\n') {
			buf[out++] = paste.data[i++];
			continue;
		}
		i += pasteunit(i, buf + out, &width);
		out += strlen(buf + out);
		used += width;
	}
	buf[out] = '\0';
	return used;
}

/* The selected range of the full view's text, if any. */
static int
pastemarkrange(size_t *lo, size_t *hi)
{
	*lo = MIN(paste.markanchor[0], paste.markpoint[0]);
	*hi = MAX(paste.markanchor[1], paste.markpoint[1]);
	return paste.marked && *lo < *hi;
}

/* The text from lo to hi for copying: escaped, but with its newlines. */
static char *
pastemarktext(size_t lo, size_t hi)
{
	char *text = xmalloc((hi - lo) * 5 + 1);

	pastetext(lo, hi, text, 1);
	return text;
}

/* Find the character of the full view at a cell, clamped to the rows of
 * text, as its start and end in at[], or the end of the row's text twice
 * beyond it. If dragging, rows above or below the text scroll by one. */
static void
pastehit(int col, int row, int dragging, size_t at[2])
{
	size_t i, next, n;
	char unit[16];
	int r, x = 0, width;

	if (dragging && row < paste.viewy)
		pastescroll(-1);
	else if (dragging && row >= paste.viewy + paste.viewrows)
		pastescroll(1);
	row = MAX(0, MIN(row - paste.viewy, paste.viewrows - 1));
	col -= paste.viewx;
	for (i = paste.viewtop, r = 0; r < row && i < paste.len; r++)
		i = pasterow(i, paste.viewcols, NULL);
	next = pasterow(i, paste.viewcols, NULL);
	for (; col >= 0 && i < next; i += n, x += width) {
		n = pasteunit(i, unit, &width);
		if (col < x + width) {
			at[0] = i;
			at[1] = i + n;
			return;
		}
	}
	at[0] = at[1] = col < 0 ? i : next;
}

/* Keep coordinates outside the window so edge dragging can scroll.
 * Any drag selects the initial character, even within its displayed width.
 * Releasing over another character also selects without a motion event. */
static void
pastemarkto(XEvent *e, int dragging)
{
	int x = e->xbutton.x - borderpx, y = e->xbutton.y - borderpx;

	pastehit(x < 0 ? -1 : x / win.cw, y < 0 ? -1 : y / win.ch, dragging,
	         paste.markpoint);
	paste.marked |= dragging ||
	                paste.markpoint[0] != paste.markanchor[0] ||
	                paste.markpoint[1] != paste.markanchor[1];
}

/* Button1 selects text in the full view, as long as it is open; return
 * whether the view took the event. On release, the selection becomes
 * PRIMARY like the terminal's; a click without a drag leaves PRIMARY. */
static int
pastemouse(XEvent *e)
{
	size_t lo, hi;

	if (paste.state != PASTE_CONFIRM || !paste.viewing)
		return 0;
	if (e->type == ButtonPress) {
		if (e->xbutton.button != Button1)
			return 0;
		paste.marking = 1;
		paste.marked = 0;
		paste.copied = 0;
		pastehit(evcol(e), evrow(e), 0, paste.markanchor);
		memcpy(paste.markpoint, paste.markanchor, sizeof(paste.markpoint));
		return 1;
	}
	if (!paste.marking)
		return 0;
	if (e->type == MotionNotify) {
		pastemarkto(e, 1);
		return 1;
	}
	if (e->xbutton.button != Button1)
		return 0;
	/* Motion need not have reported where the button was released. */
	pastemarkto(e, 0);
	paste.marking = 0;
	if (pastemarkrange(&lo, &hi)) {
		selclear();
		setsel(pastemarktext(lo, hi), e->xbutton.time);
	}
	return 1;
}

/* Copy the selected text of the full view, or all of it, to CLIPBOARD. */
static void
pastecopy(void)
{
	size_t lo, hi;

	if (!pastemarkrange(&lo, &hi)) {
		lo = 0;
		hi = paste.len;
	}
	free(xsel.clipboard);
	xsel.clipboard = pastemarktext(lo, hi);
	XSetSelectionOwner(xw.dpy, XInternAtom(xw.dpy, "CLIPBOARD", 0), xw.win,
	                   CurrentTime);
	paste.copied = 1;
}

/* Scroll the full view by rows for the wheel; return whether it is open,
 * as otherwise the wheel scrolls history. */
static int
pastewheel(int rows)
{
	if (paste.state != PASTE_CONFIRM || !paste.viewing)
		return 0;
	pastescroll(rows);
	return 1;
}

/* Whether a key could still have been meant for the terminal: choices are
 * ignored for pasteenterdelay after the dialog, the question or the
 * failure box appears. */
static int
pastetooearly(struct timespec shown)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return TIMEDIFF(now, shown) < pasteenterdelay;
}

/* Paste with a choice; the original only after asking again. */
static void
pastechoose(int mode)
{
	int i;

	if (pastetooearly(paste.shown))
		return;
	if (mode != PASTE_RAW) {
		pastesend(mode);
		return;
	}
	for (i = 0; i < paste.noptions; i++)
		if (paste.options[i].mode == mode)
			paste.selected = i;
	paste.asking = 1;
	paste.askyes = 0;
	clock_gettime(CLOCK_MONOTONIC, &paste.shown);
}

/* Ctrl+C, which cancels, but not Ctrl+Shift+C, which copies in phist. */
static int
pastectrlc(KeySym key, uint state)
{
	return (key == XK_c || key == XK_C) && (state & ControlMask) &&
	       !(state & ShiftMask);
}

int
pastekey(KeySym key, uint state)
{
	int i, page = MAX(1, paste.viewrows - 1);
	Shortcut *bp;

	if (pastefail.text[0]) {
		/* Paste shortcuts retry, replacing the box. */
		for (bp = shortcuts; bp < shortcuts + LEN(shortcuts); bp++)
			if (key == bp->keysym && match(bp->mod, state) &&
			    pasteaction(bp->func))
				return 0;
		/* Esc, Ctrl+C or Enter close the failure box; it takes all
		 * other keys while it is shown. */
		if (key == XK_Escape || key == XK_Return || key == XK_KP_Enter ||
		    pastectrlc(key, state)) {
			if (!pastetooearly(pastefail.shown))
				pastefailclear();
		}
		return 1;
	}
	if (paste.state != PASTE_CONFIRM)
		return 0;
	/* The binding to clipcopy, Ctrl+Shift+C, copies in the full view
	 * as c does, and does nothing elsewhere; it never cancels. */
	for (bp = shortcuts; bp < shortcuts + LEN(shortcuts); bp++) {
		if (key == bp->keysym && match(bp->mod, state) &&
		    bp->func == clipcopy) {
			if (paste.viewing)
				pastecopy();
			return 1;
		}
	}
	if (key >= XK_A && key <= XK_Z)
		key += XK_a - XK_A;
	if ((key == XK_Escape && !paste.viewing && !paste.asking) ||
	    pastectrlc(key, state)) {
		pasteclear();
		return 1;
	}
	/* Other Ctrl or Alt combinations do nothing, also in the full view. */
	if (state & ~(ignoremod | LockMask | ShiftMask))
		return 1;
	if (paste.asking) {
		/* No or Yes, No selected first: y pastes the original, n and Esc
		 * go back, Enter does what is selected. */
		switch (key) {
		case XK_Left: case XK_KP_Left: case XK_Up: case XK_KP_Up:
		case XK_Home: case XK_KP_Home:
			paste.askyes = 0;
			break;
		case XK_Right: case XK_KP_Right: case XK_Down: case XK_KP_Down:
		case XK_End: case XK_KP_End:
			paste.askyes = 1;
			break;
		case XK_Tab: case XK_ISO_Left_Tab:
			paste.askyes = !paste.askyes;
			break;
		case XK_Return: case XK_KP_Enter:
			if (!paste.askyes)
				paste.asking = 0;
			else if (!pastetooearly(paste.shown))
				pastesend(PASTE_RAW);
			break;
		case XK_y:
			if (!pastetooearly(paste.shown))
				pastesend(PASTE_RAW);
			break;
		case XK_n: case XK_Escape:
			paste.asking = 0;
			break;
		}
		return 1;
	}
	if (paste.viewing) {
		paste.copied = 0;
		switch (key) {
		case XK_Escape: case XK_Return: case XK_KP_Enter: case XK_q: case XK_v:
			paste.viewing = 0;
			paste.marking = paste.marked = 0;
			break;
		case XK_c: pastecopy(); break;
		case XK_Up: case XK_KP_Up: pastescroll(-1); break;
		case XK_Down: case XK_KP_Down: pastescroll(1); break;
		case XK_Prior: case XK_KP_Prior: pastescroll(-page); break;
		case XK_Next: case XK_KP_Next: case XK_space: pastescroll(page); break;
		case XK_Home: case XK_KP_Home: paste.viewtop = 0; break;
		case XK_End: case XK_KP_End:
			paste.viewtop = pasterowbefore(paste.len, paste.viewcols);
			pastescroll(-page);
			break;
		}
		return 1;
	}
	switch (key) {
	case XK_Tab:
		if (!(state & ShiftMask))
			goto down;
		/* FALLTHROUGH */
	case XK_Up:
	case XK_KP_Up:
	case XK_ISO_Left_Tab:
		paste.selected = MAX(-1, paste.selected - 1);
		return 1;
	case XK_Down:
	case XK_KP_Down:
	down:
		paste.selected = MIN(paste.noptions - 1, paste.selected + 1);
		return 1;
	case XK_Home:
	case XK_KP_Home:
		paste.selected = 0;
		return 1;
	case XK_End:
	case XK_KP_End:
		paste.selected = paste.noptions - 1;
		return 1;
	case XK_v:
		paste.selected = -1;
		paste.viewing = 1;
		paste.viewtop = 0;
		return 1;
	case XK_Return:
	case XK_KP_Enter:
		if (pastetooearly(paste.shown))
			return 1;
		if (paste.selected < 0) {
			paste.viewing = 1;
			paste.viewtop = 0;
		} else {
			pastechoose(paste.options[paste.selected].mode);
		}
		return 1;
	}
	for (i = 0; i < paste.noptions; i++) {
		if (key == (KeySym)paste.options[i].key) {
			pastechoose(paste.options[i].mode);
			break;
		}
	}
	return 1;
}

enum { PASTE_BLACK, PASTE_WHITE, PASTE_YELLOW, PASTE_BLUE, PASTE_RED, PASTE_GOLD,
       PASTE_DARKYELLOW, PASTE_DARKBLUE, PASTE_DARKRED, PASTE_PALE, PASTE_COLORS };

static const XRenderColor pastevalues[] = {
	{ .alpha = 0xffff },
	{ .red = 0xffff, .green = 0xffff, .blue = 0xffff, .alpha = 0xffff },
	{ .red = 0xffff, .green = 0xffff, .alpha = 0xffff },
	{ .red = 0x2525, .green = 0x6363, .blue = 0xebeb, .alpha = 0xffff },
	{ .red = 0xb0b0, .alpha = 0xffff },
	{ .red = 0xffff, .green = 0xd7d7, .blue = 0x5f5f, .alpha = 0xffff },
	{ .red = 0xc8c8, .green = 0xb0b0, .alpha = 0xffff },
	{ .red = 0x1e1e, .green = 0x3a3a, .blue = 0x8a8a, .alpha = 0xffff },
	{ .red = 0x6e6e, .alpha = 0xffff },
	{ .red = 0xb4b4, .green = 0xc6c6, .blue = 0xf4f4, .alpha = 0xffff },
};

/* Pixels to move text and key caps down by, for a box such as the search
 * bar that does not sit on the cell grid. */
static int pasteshift;
#define pasteblack pastevalues[PASTE_BLACK]
#define pastewhite pastevalues[PASTE_WHITE]

static Color *
pastecolor(int index)
{
	static Color colors[PASTE_COLORS];
	static int loaded[PASTE_COLORS];

	return xlazycolor(&colors[index], &loaded[index], &pastevalues[index],
	                  xnearestcolor(&pastevalues[index]));
}

static int
pastebackground(void)
{
	return paste.danger || pastefail.text[0] ? PASTE_RED :
	       paste.tabs || paste.crlf ? PASTE_YELLOW : PASTE_BLUE;
}

/* Gold rather than glaring white on red. */
static int
pasteforeground(void)
{
	return paste.danger || pastefail.text[0] ? PASTE_GOLD :
	       paste.tabs || paste.crlf ? PASTE_BLACK : PASTE_WHITE;
}

/* A darker shade, so the title bar does not look like the selection. */
static int
pastetitlecolor(void)
{
	return paste.danger || pastefail.text[0] ? PASTE_DARKRED :
	       paste.tabs || paste.crlf ? PASTE_DARKYELLOW : PASTE_DARKBLUE;
}

/* What made the dialog appear, in the order its colors take precedence. */
static const char *
pastetitle(void)
{
	if (paste.danger)
		return "Paste contains dangerous characters";
	if (paste.tabs && paste.crlf)
		return "Paste contains tabs and CRLF line endings";
	if (paste.tabs)
		return "Paste contains tabs";
	if (paste.crlf)
		return "Paste contains CRLF line endings";
	return "Paste contains multiple lines";
}

/* Blend a translucent color over everything drawn so far; values are
 * premultiplied by alpha, as XRender expects. */
static void
pasteblend(const XRenderColor *base, unsigned short alpha, int x, int y, int w, int h)
{
	XRenderColor c = {
		.red = (unsigned)base->red * alpha / 0xffff,
		.green = (unsigned)base->green * alpha / 0xffff,
		.blue = (unsigned)base->blue * alpha / 0xffff,
		.alpha = alpha,
	};

	if (x < 0)
		w += x, x = 0;
	if (y < 0)
		h += y, y = 0;
	w = MIN(w, win.w - x);
	h = MIN(h, win.h - y);
	if (w > 0 && h > 0)
		XRenderFillRectangle(xw.dpy, PictOpOver, XftDrawPicture(xw.draw),
		                     &c, x, y, w, h);
}

static void
pastedrawline(const char *s, int col, int row, int width, Color *color)
{
	Glyph g = {0};
	GlyphFontSpec spec;
	XRectangle clip = { .x = borderpx + col * win.cw,
	                    .y = borderpx + row * win.ch + pasteshift,
	                    .width = width * win.cw, .height = win.ch };
	Rune u;
	size_t n, len = strlen(s);
	int x = 0, w, numspecs;

	/* Glyphs that can't be clipped could paint out of the box. */
	if (!XftDrawSetClipRectangles(xw.draw, 0, 0, &clip, 1))
		return;
	while (len && (n = pasteutf8(s, len, &u))) {
		/* The preview escapes what has no width, so anything else
		 * unknown, e.g. → and ▸ in the C locale, takes a column. */
		w = MAX(1, wcwidth(u));
		if (x + w > width)
			break;
		g.u = u;
		g.mode = w == 2 ? ATTR_WIDE : 0;
		numspecs = xmakeglyphfontspecs(&spec, &g, 1, col + x, row);
		spec.y += pasteshift;
		XftDrawGlyphFontSpec(xw.draw, color, &spec, numspecs);
		x += w;
		s += n;
		len -= n;
	}
	XftDrawSetClip(xw.draw, 0);
}

/* Columns taken by s, which is valid UTF-8 without wide characters. */
static int
pastewidth(const char *s)
{
	int n = 0;

	for (; *s; s++)
		n += ((uchar)*s & 0xc0) != 0x80;
	return n;
}

static void
pastedrawrect(Color *color, int col, int row, int width, int height)
{
	XftDrawRect(xw.draw, color, borderpx + col * win.cw, borderpx + row * win.ch,
	            width * win.cw, height * win.ch);
}

/* How far a field or the title bar reaches up so that the space above
 * digits and capitals matches the font's descent below them. */
static int
pastefieldtop(void)
{
	XGlyphInfo digit;
	int top;

	XftTextExtentsUtf8(xw.dpy, dc.font.match, (const FcChar8 *)"0", 1, &digit);
	top = (win.ch - dc.font.ascent) - (dc.font.ascent - digit.y);
	return MAX(0, MIN(top, win.ch / 2));
}

enum { PASTE_NOTITLE, PASTE_TITLE, PASTE_TITLEROOM };

/* Tint the terminal behind the dialog, or darken it for confirmation.
 * Draw a raised box with a shadow, bevel and optional darker title bar;
 * extend the title bar into adjacent blank space to balance text padding. */
static void
pastedrawframe(int col, int row, int width, int height, int title, int modal)
{
	const XRenderColor *bar = &pastevalues[pastetitlecolor()];
	int t = title ? MAX(1, win.ch / 4) : 0, up = title ? t + pastefieldtop() : 0;
	int x = borderpx + col * win.cw, y = borderpx + row * win.ch - up;
	int w = width * win.cw, h = height * win.ch + up;
	int b = MAX(1, MIN(win.cw, win.ch) / 6), s = 2 * b + 2, i;

	if (modal)
		pasteblend(&pasteblack, 0xa000, 0, 0, win.w, win.h);
	else
		pasteblend(&pastevalues[pastebackground()], 0x3000, 0, 0, win.w, win.h);
	for (i = 1; i <= 3; i++)
		pasteblend(&pasteblack, 0x1800, x + s * i / 3, y + s * i / 3, w, h);
	pastedrawrect(pastecolor(pastebackground()), col, row, width, height);
	if (title) {
		pastedrawrect(pastecolor(pastetitlecolor()), col, row, width, 1);
		pasteblend(bar, 0xffff, x, y, w, up);
		if (title == PASTE_TITLEROOM)
			pasteblend(bar, 0xffff, x, y + up + win.ch, w, t);
	}
	pasteblend(&pastewhite, 0x7000, x, y, w, b);
	pasteblend(&pastewhite, 0x7000, x, y + b, b, h - b);
	pasteblend(&pasteblack, 0x5000, x + b, y + h - b, w - b, b);
	pasteblend(&pasteblack, 0x5000, x + w - b, y + b, b, h - 2 * b);
	pasteblend(&pasteblack, 0x9000, x - 1, y - 1, w + 2, 1);
	pasteblend(&pasteblack, 0x9000, x - 1, y + h, w + 2, 1);
	pasteblend(&pasteblack, 0x9000, x - 1, y, 1, h);
	pasteblend(&pasteblack, 0x9000, x + w, y, 1, h);
}

/* A raised area, like a key: a lighter fill with darker bottom and right
 * edges. */
static void
pasteraise(const XRenderColor *fill, int x, int y, int w, int h)
{
	int b = MAX(1, win.ch / 10);

	pasteblend(fill, 0x4000, x, y, w, h - b);
	pasteblend(&pasteblack, 0x6000, x, y + h - b, w, b);
	pasteblend(&pasteblack, 0x4000, x + w - 1, y, 1, h - b);
}

/* Draw a two-tone count chip, e.g. "17 bytes", with one-cell padding around
 * each part. Extend into a blank row above to balance vertical padding.
 * Return its width including the following gap, or 0 if it does not fit. */
static int
pastedrawchip(const char *s, int col, int row, int width, int room, Color *fg)
{
	const char *unit = strchr(s, ' ') + 1;
	int n = unit - s - 1, u = pastewidth(unit), top = 0;
	int x = borderpx + col * win.cw, y = borderpx + row * win.ch;
	char number[32];

	if (n + u + 4 > width)
		return 0;
	if (room)
		top = pastefieldtop();
	snprintf(number, sizeof(number), "%.*s", n, s);
	pasteblend(&pastevalues[pastetitlecolor()], 0xffff, x, y - top, (n + 2) * win.cw,
	           win.ch + top);
	pasteblend(&pasteblack, 0x4000, x + (n + 2) * win.cw, y - top, (u + 2) * win.cw,
	           win.ch + top);
	pastedrawline(number, col + 1, row, n, fg);
	pastedrawline(unit, col + n + 3, row, u, fg);
	return n + u + 5;
}

/* A raised key cap over a key name width cells wide at col, with a cell
 * on either side. Given a blank row above, it is as tall as the chips and
 * the preview field. */
static void
pastedrawkey(const XRenderColor *fill, int col, int width, int row, int room)
{
	int top = room ? pastefieldtop() : -1;

	pasteraise(fill, borderpx + (col - 1) * win.cw,
	           borderpx + row * win.ch + pasteshift - top,
	           (width + 2) * win.cw, win.ch + top + (room ? 0 : -1));
}

/* Lines of the dialog and its question; the kinds are ordered by how
 * readily they are dropped when the window is too small. Options are never
 * dropped; gaps are the blank lines between them. */
enum { PASTE_PAD, PASTE_GAP, PASTE_BLANK, PASTE_CHIPS, PASTE_HEADER,
       PASTE_PREVIEW, PASTE_OPTION };
typedef struct { int kind, arg; } PasteLine;

/* Drop the least important kinds of line until they fit in rows, or only
 * options are left. Return how many lines remain. */
static int
pastefit(PasteLine *lines, int n, int rows)
{
	int level, i, kept;

	for (level = PASTE_PAD; n > rows && level < PASTE_OPTION; level++) {
		for (i = kept = 0; i < n; i++)
			if (lines[i].kind != level)
				lines[kept++] = lines[i];
		n = kept;
	}
	return n;
}

/* How the title bar is drawn for the lines left: not at all if it was
 * dropped, and reaching down only into a blank line below it. */
static int
pastetitlemode(const PasteLine *lines, int n)
{
	if (!n || lines[0].kind != PASTE_HEADER)
		return PASTE_NOTITLE;
	return n > 1 && lines[1].kind == PASTE_BLANK ? PASTE_TITLEROOM : PASTE_TITLE;
}

/* Whether line i has a blank line above it for fields to reach up into. */
static int
pasteroom(const PasteLine *lines, int i)
{
	return i > 0 && (lines[i - 1].kind == PASTE_BLANK ||
	                 lines[i - 1].kind == PASTE_GAP);
}

/* Draw the full view's row from i to next, inverting what is selected. */
static void
pastedrawrow(size_t i, size_t next, int left, int y, int inner, char *buf,
             Color *fg, Color *bg)
{
	size_t lo, hi, a, b;
	int x, w;

	if (!pastemarkrange(&lo, &hi))
		lo = hi = next;
	a = MAX(i, MIN(lo, next));
	b = MAX(a, MIN(hi, next));
	x = pastetext(i, a, buf, 0);
	pastedrawline(buf, left, y, inner, fg);
	if (a < b && x < inner) {
		w = MIN(pastetext(a, b, buf, 0), inner - x);
		pastedrawrect(fg, left + x, y, w, 1);
		pastedrawline(buf, left + x, y, w, bg);
		x += w;
	}
	if (b < next && x < inner) {
		pastetext(b, next, buf, 0);
		pastedrawline(buf, left + x, y, inner - x, fg);
	}
}

static void
pastedrawview(int cols, int rows, Color *fg, Color *bg)
{
	char *buf;
	int padx = cols >= 12 ? 2 : 0, pady = rows >= 7, inner, height, top, left, y;
	int titled, n;
	size_t row, next, lo, hi;
	const char *label;

	inner = MAX(1, cols - 2 * padx - (cols >= 8 ? 2 : 0));
	height = rows - (rows >= 5 ? 2 : 0);
	left = (cols - inner - 2 * padx) / 2;
	top = (rows - height) / 2;
	titled = height >= 4;
	pastedrawframe(left, top, inner + 2 * padx, height,
	               !titled ? PASTE_NOTITLE : pady ? PASTE_TITLEROOM : PASTE_TITLE, 0);
	left += padx;
	paste.viewcols = inner;
	paste.viewrows = MAX(1, height - (titled ? 2 + 2 * pady : 0));
	buf = xmalloc((size_t)inner * 4 + 16);
	y = top;
	if (titled) {
		pastedrawline(pastetitle(), left, y++, inner, fg);
		y += pady;
	}
	paste.viewx = left;
	paste.viewy = y;
	for (row = paste.viewtop; y < top + height - (titled ? 1 + pady : 0); y++) {
		if (row >= paste.len)
			break;
		next = pasterow(row, inner, NULL);
		pastedrawrow(row, next, left, y, inner, buf, fg, bg);
		row = next;
	}
	if (titled) {
		/* Right-align the scroll position; show the copy key and label if they fit. */
		y = top + height - pady - 1;
		n = snprintf(buf, 16, "%d%%",
		             row >= paste.len ? 100 : (int)(100 * (double)row / paste.len));
		pastedrawline(buf, left + MAX(0, inner - n), y, MIN(n, inner), fg);
		if (inner >= n + 4) {
			pastedrawkey(&pastewhite, left + 1, 1, y, 0);
			pastedrawline("C", left + 1, y, 1, fg);
		}
		label = paste.copied ? "Copied to clipboard" :
		        pastemarkrange(&lo, &hi) ? "Copy selection" : "Copy all";
		if (pastewidth(label) <= inner - n - 6)
			pastedrawline(label, left + 5, y, inner - n - 6, fg);
	}
	free(buf);
}

/* Choices and the preview: the marker at column 0, the key in uppercase on
 * a key cap over columns 2 to 4 and the label from column 7. The title and
 * the chips start where the key caps do. */
enum { PASTE_CAPCOL = 2, PASTE_KEYCOL = 3, PASTE_LABELCOL = 7 };

/* A choice or the preview, with the marker when marked and the label
 * inverted if asked to; nothing is drawn before the key cap unless marked.
 * Given a blank row above, the key cap and inverted label reach up into it
 * like the preview field. Return the column the inverted label ends at. */
static int
pastedrawitem(int key, const char *label, int marked, int invert, int room,
              int left, int row, int inner, Color *fg, Color *bg)
{
	char buf[2] = { toupper(key) };
	int w = invert ? MIN(pastewidth(label) + 2, inner - PASTE_LABELCOL + 1) : 0;
	int top = room ? pastefieldtop() : 0;

	if (inner > PASTE_KEYCOL + 1)
		pastedrawkey(&pastewhite, left + PASTE_KEYCOL, 1, row, room);
	if (marked)
		pastedrawline("\u25b8", left, row, inner, fg);
	if (inner > PASTE_KEYCOL)
		pastedrawline(buf, left + PASTE_KEYCOL, row, 1, fg);
	if (w > 0) {
		pastedrawrect(fg, left + PASTE_LABELCOL - 1, row, w, 1);
		if (top)
			pasteblend(&pastevalues[pasteforeground()], 0xffff,
			           borderpx + (left + PASTE_LABELCOL - 1) * win.cw,
			           borderpx + row * win.ch - top, w * win.cw, top);
	}
	if (inner > PASTE_LABELCOL)
		pastedrawline(label, left + PASTE_LABELCOL, row, inner - PASTE_LABELCOL,
		              w > 0 ? bg : fg);
	return w > 0 ? PASTE_LABELCOL - 1 + w : 0;
}

/* Ask whether to paste the original, in a box like the dialog over it:
 * a title bar and No and Yes side by side, centered below it, without the
 * marker, so each starts at its key cap. */
static void
pastedrawask(int cols, int rows, Color *fg, Color *bg)
{
	static const char title[] = "Confirm pasting data unmodified";
	/* Each choice from its key cap to the end of its inverted label, and
	 * between them. */
	enum { NOW = PASTE_LABELCOL - PASTE_CAPCOL + 3,
	       YESW = PASTE_LABELCOL - PASTE_CAPCOL + 4, GAPW = 3 };
	PasteLine lines[] = { { PASTE_HEADER, 0 }, { PASTE_BLANK, 0 },
	                      { PASTE_OPTION, 0 }, { PASTE_PAD, 0 } };
	int nlines, i, want, padx, inner, left, top, row, room, x;

	nlines = MIN(pastefit(lines, LEN(lines), rows), rows);
	/* The title is centered with as much room on either side. */
	want = MAX(2 * PASTE_CAPCOL + (int)strlen(title), NOW + GAPW + YESW);
	padx = cols >= want + 4 ? 2 : cols >= want + 2 ? 1 : 0;
	inner = MIN(want, cols - 2 * padx);
	left = (cols - inner - 2 * padx) / 2;
	top = MAX(0, (rows - nlines) / 2);
	pastedrawframe(left, top, inner + 2 * padx, nlines,
	               pastetitlemode(lines, nlines), 1);
	left += padx;
	for (i = 0, row = top; i < nlines; i++, row++) {
		room = pasteroom(lines, i);
		if (lines[i].kind == PASTE_HEADER) {
			x = MAX(0, (inner - (int)strlen(title)) / 2);
			pastedrawline(title, left + x, row, inner - x, fg);
		} else if (lines[i].kind == PASTE_OPTION) {
			x = MAX(0, (inner - NOW - GAPW - YESW) / 2) - PASTE_CAPCOL;
			pastedrawitem('n', "No", 0, !paste.askyes, room, left + x, row,
			              MIN(NOW, inner - x - PASTE_CAPCOL) + PASTE_CAPCOL, fg, bg);
			x += NOW + GAPW;
			if (x + PASTE_CAPCOL < inner)
				pastedrawitem('y', "Yes", 0, paste.askyes, room, left + x, row,
				              inner - x, fg, bg);
		}
	}
}

/* Why a paste failed, in a red box like the question's: a title bar and
 * the reason, both centered. */
static void
pastedrawfail(int cols, int rows, Color *fg)
{
	static const char title[] = "Paste failed";
	PasteLine lines[] = { { PASTE_HEADER, 0 }, { PASTE_BLANK, 0 },
	                      { PASTE_OPTION, 0 }, { PASTE_PAD, 0 } };
	int nlines, i, want, padx, inner, left, top, row, x, n;

	nlines = MIN(pastefit(lines, LEN(lines), rows), rows);
	want = 2 * PASTE_CAPCOL + MAX((int)strlen(title), pastewidth(pastefail.text));
	padx = cols >= want + 4 ? 2 : cols >= want + 2 ? 1 : 0;
	inner = MIN(want, cols - 2 * padx);
	left = (cols - inner - 2 * padx) / 2;
	top = MAX(0, (rows - nlines) / 2);
	pastedrawframe(left, top, inner + 2 * padx, nlines,
	               pastetitlemode(lines, nlines), 0);
	left += padx;
	for (i = 0, row = top; i < nlines; i++, row++) {
		if (lines[i].kind == PASTE_HEADER) {
			n = strlen(title);
		} else if (lines[i].kind == PASTE_OPTION) {
			n = pastewidth(pastefail.text);
		} else {
			continue;
		}
		x = MAX(0, (inner - n) / 2);
		pastedrawline(lines[i].kind == PASTE_HEADER ? title : pastefail.text,
		              left + x, row, inner - x, fg);
	}
}

/* The preview's field starts a cell after "View". */
enum { PASTE_FIELDCOL = PASTE_LABELCOL + 5 };

/* "V View" and one line of the paste in a sunken field, which reaches up
 * like the chips given room above and is inverted when selected. In narrow
 * dialogs only the paste is shown. */
static void
pastedrawpreview(int left, int row, int inner, int room, Color *fg, Color *bg)
{
	int selected = paste.selected < 0;
	int field = inner >= PASTE_FIELDCOL + 6 ? PASTE_FIELDCOL : 0;
	int pad = field ? 1 : 0; /* inside the field, on either side */
	int top = field && room ? pastefieldtop() : 0;
	int x = borderpx + (left + field) * win.cw, y = borderpx + row * win.ch;
	int w = (inner - field) * win.cw;
	char *text = xmalloc((size_t)inner * 4 + 16);

	if (field)
		pastedrawitem('v', "View", selected, 0, room, left, row, inner, fg, bg);
	if (selected) {
		pastedrawrect(fg, left + field, row, inner - field, 1);
		if (top)
			pasteblend(&pastevalues[pasteforeground()], 0xffff, x, y - top, w, top);
	} else if (field) {
		pasteblend(&pasteblack, 0x4000, x, y - top, w, win.ch + top);
	}
	pastepreview(text, inner - field - 2 * pad);
	pastedrawline(text, left + field + pad, row, inner - field - 2 * pad,
	              selected ? bg : fg);
	free(text);
}

enum { PASTE_CHIPMAX = 6, PASTE_CHIPSIZE = 48 };

/* The size and counts as chips, e.g. "17 bytes", "16 characters", "1 TAB".
 * Return how many there are, and in *width the columns they take. */
static int
pastechips(char chips[PASTE_CHIPMAX][PASTE_CHIPSIZE], int *width)
{
	static const struct { size_t *count; const char *name; } counts[] = {
		{ &paste.lf, "LF" }, { &paste.tabs, "TAB" },
		{ &paste.crlf, "CRLF" }, { &paste.danger, "DANGER!" },
	};
	int n = 0, i;
	size_t k;

	snprintf(chips[n++], PASTE_CHIPSIZE, "%zu byte%s", paste.len,
	         paste.len == 1 ? "" : "s");
	snprintf(chips[n++], PASTE_CHIPSIZE, "%zu character%s", paste.chars,
	         paste.chars == 1 ? "" : "s");
	for (k = 0; k < LEN(counts); k++)
		if (*counts[k].count)
			snprintf(chips[n++], PASTE_CHIPSIZE, "%zu %s", *counts[k].count,
			         counts[k].name);
	/* As pastedrawchip draws them, a cell apart. */
	for (i = 0, *width = -1; i < n; i++)
		*width += pastewidth(chips[i]) + 4;
	return n;
}

void
pastedraw(void)
{
	PasteLine lines[24];
	char chips[PASTE_CHIPMAX][PASTE_CHIPSIZE];
	Color *fg, *bg;
	int cols, rows, nlines = 0, nchips, chipsw, want, padx, inner, height;
	int visible, first, top, left, row, indent, option, end, i, x;

	if (!pastemodal())
		return;
	cols = win.tw / win.cw;
	rows = win.th / win.ch;
	if (cols < 1 || rows < 1)
		return;
	fg = pastecolor(pasteforeground());
	bg = pastecolor(pastebackground());
	if (pastefail.text[0]) {
		pastedrawfail(cols, rows, fg);
		return;
	}
	if (paste.viewing) {
		pastedrawview(cols, rows, fg, bg);
		return;
	}

#define LINE(k, a) (lines[nlines].kind = (k), lines[nlines++].arg = (a))
	LINE(PASTE_HEADER, 0);
	LINE(PASTE_BLANK, 0);
	LINE(PASTE_CHIPS, 0);
	LINE(PASTE_BLANK, 0);
	LINE(PASTE_PREVIEW, 0);
	LINE(PASTE_BLANK, 0);
	for (option = 0; option < paste.noptions; option++) {
		if (option)
			LINE(PASTE_GAP, 0);
		LINE(PASTE_OPTION, option);
	}
	LINE(PASTE_PAD, 0);
#undef LINE
	nlines = pastefit(lines, nlines, rows);
	/* Options scroll if they do not fit even then. */
	visible = MIN(paste.noptions, rows - (nlines - paste.noptions));
	height = nlines - paste.noptions + visible;
	first = MAX(0, MAX(0, paste.selected) - visible + 1);

	nchips = pastechips(chips, &chipsw);
	want = MAX(44, PASTE_CAPCOL + MAX(chipsw, pastewidth(pastetitle())));
	for (option = 0; option < paste.noptions; option++)
		/* With a cell after it for the selected label's padding. */
		want = MAX(want, pastewidth(paste.options[option].label) +
		                 PASTE_LABELCOL + 1);
	padx = cols >= want + 4 ? 2 : cols >= want + 2 ? 1 : 0;
	inner = MIN(want, cols - 2 * padx);
	left = (cols - inner - 2 * padx) / 2;
	top = (rows - height) / 2;

	/* The dialog is reapplied to a fresh copy of the terminal each frame. */
	pastedrawframe(left, top, inner + 2 * padx, height,
	               pastetitlemode(lines, nlines), 0);
	left += padx;
	indent = inner >= 10 ? PASTE_CAPCOL : 0;
	for (i = 0, row = top; i < nlines; i++, row++) {
		switch (lines[i].kind) {
		case PASTE_HEADER:
			pastedrawline(pastetitle(), left + indent, row, inner - indent, fg);
			break;
		case PASTE_CHIPS:
			for (option = 0, x = indent; option < nchips; option++)
				x += pastedrawchip(chips[option], left + x, row, inner - x,
				                   pasteroom(lines, i), fg);
			break;
		case PASTE_PREVIEW:
			pastedrawpreview(left, row, inner, pasteroom(lines, i), fg, bg);
			break;
		case PASTE_OPTION:
			option = lines[i].arg;
			if (option < first || option >= first + visible) {
				row--;
				break;
			}
			end = pastedrawitem(paste.options[option].key,
			                    paste.options[option].label,
			                    option == paste.selected, option == paste.selected,
			                    pasteroom(lines, i), left, row, inner, fg, bg);
			/* Mark options scrolled out of view, if that leaves the
			 * marker; on the inverted label if it reaches the edge. */
			if (inner >= 5 && option == first && first > 0)
				pastedrawline("\u25b2", left + inner - 1, row, 1,
				              end >= inner ? bg : fg);
			if (inner >= 5 && option == first + visible - 1 &&
			    option < paste.noptions - 1)
				pastedrawline("\u25bc", left + inner - 1, row, 1,
				              end >= inner ? bg : fg);
			break;
		}
	}
	if (paste.asking)
		pastedrawask(cols, rows, fg, bg);
}

void
propnotify(XEvent *e)
{
	if (pastedrain.window && e->xproperty.window == pastedrain.window &&
	    e->xproperty.state == PropertyNewValue &&
	    e->xproperty.atom == pasteproperty)
		pastedrainchunk();
	else if (paste.state == PASTE_INCR &&
	    e->xproperty.window == paste.window &&
	    e->xproperty.state == PropertyNewValue &&
	    e->xproperty.atom == paste.property)
		selnotify(e);
}

void
selnotify(XEvent *e)
{
	ulong nitems, ofs = 0, rem;
	int format, complete = 0;
	uchar *data;
	Atom type, incratom = XInternAtom(xw.dpy, "INCR", 0);
	size_t cap;

	if (e->type == SelectionNotify) {
		/* phist requests a single target, so an owner echoing another
		 * one still answers the current request. */
		if (paste.state != PASTE_WAIT || !paste.property ||
		    e->xselection.requestor != paste.window ||
		    e->xselection.selection != paste.selection ||
		    e->xselection.time != paste.time)
			return;
		if (e->xselection.property == None) {
			paste.finished = 1;
			pasteclear();
			return;
		}
		if (e->xselection.property != paste.property)
			return;
	} else if (e->type != PropertyNotify || paste.state != PASTE_INCR ||
	           e->xproperty.window != paste.window ||
	           e->xproperty.atom != paste.property) {
		return;
	}

	do {
		data = NULL;
		if (XGetWindowProperty(xw.dpy, paste.window, paste.property, ofs,
		                      BUFSIZ/4, False, AnyPropertyType,
		                      &type, &format, &nitems, &rem, &data)) {
			XFree(data);
			goto unreadable;
		}
		if (type == incratom) {
			/* Its contents are only a hint of the size, which some
			 * owners, e.g. xclip, leave out; only the type counts. */
			if (paste.state != PASTE_WAIT || ofs) {
				XFree(data);
				goto unreadable;
			}
			XFree(data);
			paste.state = PASTE_INCR;
			clock_gettime(CLOCK_MONOTONIC, &paste.progress);
			/* Acknowledge INCR; its chunks will arrive in properties. */
			XDeleteProperty(xw.dpy, paste.window, paste.property);
			return;
		}
		if (paste.len > pastemaxbytes || nitems > pastemaxbytes - paste.len ||
		    rem > pastemaxbytes - paste.len - nitems) {
			XFree(data);
			goto toolarge;
		}
		/* Owners may answer with another text type, e.g. STRING or TEXT. */
		if (format != 8 || type == None || (paste.type && type != paste.type) ||
		    (nitems && !data) ||
		    (rem && (!nitems || nitems % 4 || ofs > LONG_MAX - nitems/4))) {
			XFree(data);
			goto failed;
		}
		paste.type = type;
		if (nitems) {
			if (paste.len + nitems > paste.cap) {
				cap = MAX(paste.len + nitems,
				          paste.cap > pastemaxbytes/2 ? pastemaxbytes :
				          MAX(paste.cap * 2, MIN(BUFSIZ, pastemaxbytes)));
				paste.data = xrealloc(paste.data, cap);
				paste.cap = cap;
			}
			memcpy(paste.data + paste.len, data, nitems);
			paste.len += nitems;
			clock_gettime(CLOCK_MONOTONIC, &paste.progress);
		}
		complete = paste.state == PASTE_WAIT || (!ofs && !nitems && !rem);
		XFree(data);
		ofs += nitems / 4;
	} while (rem);

	XDeleteProperty(xw.dpy, paste.window, paste.property);
	if (complete)
		pastecomplete();
	return;

toolarge:
	paste.answered = 1;
	pasteclear();
	pastefailedsize();
	return;

failed:
	paste.answered = 1;
	/* FALLTHROUGH */
unreadable:
	/* Unless the reply was read, it may have announced an INCR transfer,
	 * which deleting it acknowledged: the window is not reused. */
	paste.answered |= paste.state == PASTE_INCR;
	pasteclear();
	pastefailed("The selection owner sent invalid data");
}
