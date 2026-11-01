#include <assert.h>
#include <fcntl.h>
#define VERSION "test"
#define main st_main
#include "../x.c"
#undef main

#define W 240
#define H 180
#define WINDOW 7
static uint32_t pixels[8][H][W], original[H][W], firstframe[H][W];
static unsigned char touched[H][W];
static int pixmaps[8], draws, allocations, render_available = 1;
static int copies, flushes;
static Color colors[260];
typedef struct { Drawable drawable; } TestDraw;
static struct {
	int live, radial, nstops;
	XLinearGradient linear;
	XRadialGradient circles;
	XFixed stops[33];
	XRenderColor colors[33];
} gradient;

/* Emulate XRender Over, using premultiplied input and an opaque destination. */
static uint32_t
over(uint32_t pixel, const XRenderColor *color)
{
	uint32_t rgb = 0;
	const unsigned short channels[] = {color->blue, color->green, color->red};
	int i;

	for (i = 0; i < 3; i++) {
		unsigned int value = channels[i] * 255U +
		                     ((pixel >> (8 * i)) & 255) * (65535U - color->alpha);
		rgb |= MIN(255, (value + 32767) / 65535) << (8 * i);
	}
	return rgb;
}

static Picture
gradientcreate(const XFixed *stops, const XRenderColor *colors, int nstops)
{
	int i;

	assert(!gradient.live && nstops > 1 && nstops <= LEN(gradient.stops));
	for (i = 1; i < nstops; i++)
		assert(stops[i] > stops[i - 1]);
	assert(stops[0] == 0 && stops[nstops - 1] == XDoubleToFixed(1));
	memcpy(gradient.stops, stops, nstops * sizeof(*stops));
	memcpy(gradient.colors, colors, nstops * sizeof(*colors));
	gradient.nstops = nstops;
	gradient.live = 1;
	return 100;
}

Picture
XRenderCreateLinearGradient(Display *d, const XLinearGradient *linear,
                              const XFixed *stops, const XRenderColor *colors, int nstops)
{
	assert(linear->p2.x > linear->p1.x);
	gradient.radial = 0;
	gradient.linear = *linear;
	return gradientcreate(stops, colors, nstops);
}

Picture
XRenderCreateRadialGradient(Display *d, const XRadialGradient *radial,
                              const XFixed *stops, const XRenderColor *colors, int nstops)
{
	assert(radial->inner.x == radial->outer.x && radial->inner.y == radial->outer.y);
	assert(radial->inner.radius >= 0 && radial->outer.radius > radial->inner.radius);
	gradient.radial = 1;
	gradient.circles = *radial;
	return gradientcreate(stops, colors, nstops);
}

void
XRenderFreePicture(Display *d, Picture picture)
{
	assert(picture == 100 && gradient.live);
	gradient.live = 0;
}

/* Sample gradient stops independently, then convert straight RGBA to Over input. */
static XRenderColor
gradientsample(double t)
{
	XRenderColor color;
	int i;
	double mix;

	t = MAX(0, MIN(1, t)) * 65536;
	for (i = 0; i + 2 < gradient.nstops && t > gradient.stops[i + 1]; i++)
		;
	mix = (t - gradient.stops[i]) / (gradient.stops[i + 1] - gradient.stops[i]);
	color.alpha = gradient.colors[i].alpha * (1 - mix) + gradient.colors[i + 1].alpha * mix;
	color.red = (gradient.colors[i].red * (1 - mix) + gradient.colors[i + 1].red * mix) * color.alpha / 65535;
	color.green = (gradient.colors[i].green * (1 - mix) + gradient.colors[i + 1].green * mix) * color.alpha / 65535;
	color.blue = (gradient.colors[i].blue * (1 - mix) + gradient.colors[i + 1].blue * mix) * color.alpha / 65535;
	return color;
}

void
XRenderComposite(Display *d, int op, Picture src, Picture mask, Picture dst,
                   int sx, int sy, int mx, int my, int dx, int dy,
                   unsigned int width, unsigned int height)
{
	int x, y;
	double t;
	XRenderColor color;

	assert(op == PictOpOver && src == 100 && mask == None && dst == vbell.buf);
	assert(gradient.live && width == win.w && height == win.h);
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++) {
			if (gradient.radial)
				t = (hypot((x + 0.5) * 65536 - gradient.circles.inner.x,
				           (y + 0.5) * 65536 - gradient.circles.inner.y) -
				     gradient.circles.inner.radius) /
				    (gradient.circles.outer.radius - gradient.circles.inner.radius);
			else
				t = ((x + 0.5) * 65536 - gradient.linear.p1.x) /
				    (gradient.linear.p2.x - gradient.linear.p1.x);
			color = gradientsample(t);
			assert(++touched[y][x] == 1);
			pixels[dst][y][x] = over(pixels[dst][y][x], &color);
		}
}

void
XRenderFillRectangles(Display *d, int op, Picture dst, const XRenderColor *color,
                       const XRectangle *rects, int nrects)
{
	int i, x, y;

	assert(op == PictOpOver && dst == vbell.buf);
	assert(color->red <= color->alpha && color->green <= color->alpha &&
	       color->blue <= color->alpha);
	for (i = 0; i < nrects; i++) {
		assert(rects[i].width && rects[i].height);
		assert(rects[i].x >= 0 && rects[i].y >= 0);
		assert(rects[i].x + rects[i].width <= win.w);
		assert(rects[i].y + rects[i].height <= win.h);
		for (y = rects[i].y; y < rects[i].y + rects[i].height; y++)
			for (x = rects[i].x; x < rects[i].x + rects[i].width; x++) {
				assert(++touched[y][x] == 1); /* no double blending at corners */
				pixels[dst][y][x] = over(pixels[dst][y][x], color);
			}
	}
}

Pixmap
XCreatePixmap(Display *d, Drawable parent, unsigned int w, unsigned int h,
               unsigned int depth)
{
	int i;

	assert(w == win.w && h == win.h && depth == 24);
	for (i = 1; i < WINDOW; i++)
		if (!pixmaps[i]) {
			pixmaps[i] = 1;
			allocations++;
			return i;
		}
	abort();
}

int XSync(Display *d, Bool discard) { return 1; }

int
XFreePixmap(Display *d, Pixmap pixmap)
{
	assert(pixmap > 0 && pixmap < WINDOW && pixmaps[pixmap]);
	pixmaps[pixmap] = 0;
	return 0;
}

XftDraw *
XftDrawCreate(Display *d, Drawable drawable, Visual *visual, Colormap map)
{
	TestDraw *draw = malloc(sizeof(*draw));

	assert(draw);
	draw->drawable = drawable;
	draws++;
	return (XftDraw *)draw;
}

void
XftDrawDestroy(XftDraw *draw)
{
	assert(draw && draws > 0);
	free(draw);
	draws--;
}

void
XftDrawChange(XftDraw *draw, Drawable drawable)
{
	((TestDraw *)draw)->drawable = drawable;
}

Picture
XftDrawPicture(XftDraw *draw)
{
	return render_available ? ((TestDraw *)draw)->drawable : None;
}

void
XftDrawRect(XftDraw *draw, const XftColor *color, int x, int y,
             unsigned int width, unsigned int height)
{
	int xx, yy;
	Drawable dst = ((TestDraw *)draw)->drawable;

	/* XftDrawRect uses Src, so it is only suitable for clearing here. */
	for (yy = y; yy < y + height; yy++)
		for (xx = x; xx < x + width; xx++)
			pixels[dst][yy][xx] = color->pixel;
}

int
XCopyArea(Display *d, Drawable src, Drawable dst, GC gc, int sx, int sy,
           unsigned int width, unsigned int height, int dx, int dy)
{
	int x, y;
	copies++;

	assert(src != dst && width == win.w && height == win.h);
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
			pixels[dst][dy + y][dx + x] = pixels[src][sy + y][sx + x];
	return 0;
}

int XSetForeground(Display *d, GC gc, unsigned long color) { return 0; }
int XFlush(Display *d) { flushes++; return 0; }

static void
background(void)
{
	int x, y;

	for (y = 0; y < win.h; y++)
		for (x = 0; x < win.w; x++)
			pixels[xw.buf][y][x] = (x % 8 < 3 && y % 12 < 5) ? 0xeeeeee : 0x204060;
	memcpy(original, pixels[xw.buf], sizeof(original));
}

static void
frame(void)
{
	memset(touched, 0, sizeof(touched));
	xfinishdraw();
	assert(!memcmp(original, pixels[xw.buf], sizeof(original)));
}

static void
frameat(double progress)
{
	Drawable source;

	memset(touched, 0, sizeof(touched));
	source = vbellrender(progress);
	XCopyArea(xw.dpy, source, xw.win, dc.gc, 0, 0, win.w, win.h, 0, 0);
	assert(!memcmp(original, pixels[xw.buf], sizeof(original)));
	assert(!gradient.live);
}

static int
changed(int x, int y)
{
	return pixels[WINDOW][y][x] != original[y][x];
}

static double
waveposition(void)
{
	int x, count = 0, sum = 0;

	for (x = 0; x < win.w; x++)
		if (changed(x, win.h / 2)) {
			sum += x;
			count++;
		}
	assert(count);
	return (double)sum / count;
}

static struct timespec
after(struct timespec time, long ms)
{
	time.tv_nsec += ms * 1000000;
	time.tv_sec += time.tv_nsec / 1000000000;
	time.tv_nsec %= 1000000000;
	return time;
}

int
main(void)
{
	Screen screen = {.root_depth = 24};
	_XPrivDisplay display = calloc(1, sizeof(*display));
	XRenderColor edge, orange, middle, yellow, end, previous, color;
	int i, before, effect, x, y, count;
	double left;
	const int sizes[][2] = {{1, 1}, {1, 8}, {8, 1}, {2, 2}, {3, 7}, {77, 43}};

	/* Coordinates beyond XRender's fixed point saturate, and exact ones survive. */
	assert(xfixed(0) == 0 && xfixed(1.5) == 98304 && xfixed(-1.5) == -98304);
	assert(xfixed(32767) == 32767 * 65536 && xfixed(-32768) == INT_MIN);
	assert(xfixed(32768) == INT_MAX && xfixed(65535) == INT_MAX && xfixed(1e300) == INT_MAX);
	assert(xfixed(-32769) == INT_MIN && xfixed(-1e300) == INT_MIN && xfixed(NAN) == INT_MIN);
	{
		XLinearGradient line;
		XRadialGradient circles;

		assert(xgradientline(&line, 0, 65535) && !line.p1.x && line.p2.x == INT_MAX);
		assert(xgradientline(&line, 65000 - 40000, 65000));
		assert(!xgradientline(&line, 40000, 65535)); /* no length left */
		assert(!xgradientline(&line, -70000, -40000));
		win.w = win.h = 65535;
		assert(vbellcircles(&circles, 0, 46340 + 256));
		assert(circles.inner.x == xfixed(32767.5) && circles.outer.radius == INT_MAX);
		assert(vbellcircles(&circles, 32000, 32000));
		assert(circles.outer.radius == circles.inner.radius + 1);
		assert(!vbellcircles(&circles, 46000, 46340 + 256));
	}

	assert(display);
	display->screens = &screen;
	xw.dpy = (Display *)display;
	xw.win = WINDOW;
	win.w = W;
	win.h = H;
	win.cw = win.ch = 1;
	win.mode = MODE_FOCUSED; /* redraw() does not call real X text rendering. */
	vbelleffect = VBELL_GLOW;
	vbellcolor = 0xff0000;
	vbellinnercolor = 0xffff00;
	vbellopacity = 0.6;
	vbelltimeout = 50;
	dc.col = colors;
	colors[defaultbg].pixel = 0x204060;
	xw.buf = XCreatePixmap(xw.dpy, xw.win, win.w, win.h, 24);
	xw.draw = XftDrawCreate(xw.dpy, xw.buf, NULL, 0);
	vbellwidth = 32;
	background();

	/* No active bell uses the normal buffer without allocating an overlay. */
	frame();
	assert(!vbell.buf && draws == 1 && allocations == 1);
	assert(!memcmp(pixels[WINDOW], original, sizeof(original)));

	/* Bright red/orange/gold transition, yellow interior, soft transparent tip. */
	edge = vbellcolorat(0, 97);
	orange = vbellcolorat(24, 97);
	middle = vbellcolorat(48, 97);
	yellow = vbellcolorat(77, 97);
	end = vbellcolorat(96, 97);
	assert(edge.red && !edge.green && !edge.blue && edge.alpha < 65535);
	assert(orange.green > 0 && orange.green < middle.green);
	assert(middle.red > middle.green && !middle.blue);
	assert(orange.alpha == edge.alpha && middle.alpha == edge.alpha);
	assert(yellow.red == yellow.green && !yellow.blue && yellow.alpha > 0);
	assert(yellow.alpha < edge.alpha && !end.alpha);
	previous = edge;
	for (i = 1; i <= 96; i++) {
		color = vbellcolorat(i, 97);
		assert(color.alpha <= previous.alpha);
		assert(previous.alpha - color.alpha < 2000);
		previous = color;
	}

	/* Composite over both background and foreground pixels, including corners. */
	vbellbegin();
	assert(vbellset && vbellremaining(lastvbell) == vbelltimeout);
	frame();
	edge = vbellcolorat(0, 32);
	assert(pixels[WINDOW][0][0] == over(original[0][0], &edge));
	assert(pixels[WINDOW][0][W - 1] == over(original[0][W - 1], &edge));
	assert(pixels[WINDOW][H - 1][0] == over(original[H - 1][0], &edge));
	assert(pixels[WINDOW][H - 1][W - 1] == over(original[H - 1][W - 1], &edge));
	assert(pixels[WINDOW][H / 2][W / 2] == original[H / 2][W / 2]);
	assert(pixels[WINDOW][0][0] != pixels[WINDOW][0][4]); /* text remains visible */
	memcpy(firstframe, pixels[WINDOW], sizeof(firstframe));
	before = allocations;
	frame();
	assert(!memcmp(firstframe, pixels[WINDOW], sizeof(firstframe)));
	assert(allocations == before); /* cached resources, no accumulating tint */

	/* New output during a bell is taken from the current normal back buffer. */
	pixels[xw.buf][0][0] = original[0][0] = 0x00ff00;
	frame();
	assert(pixels[WINDOW][0][0] == over(0x00ff00, &edge));
	assert(vbellremaining(after(lastvbell, 10)) == vbelltimeout - 10);
	assert(vbellremaining(after(lastvbell, vbelltimeout)) < 0 && !vbellset);
	frame();
	assert(!memcmp(pixels[WINDOW], original, sizeof(original)));

	/* Repeated bells renew the existing timer instead of changing the buffer. */
	x = copies;
	y = flushes;
	vbellbegin();
	lastvbell.tv_sec--;
	for (i = 0; i < 100; i++)
		vbellbegin();
	assert(copies == x && flushes == y);
	assert(vbellremaining(lastvbell) == vbelltimeout);
	assert(allocations == before);

	/* Resizing while active discards the old overlay and stays in bounds. */
	vbellwidth = 96;
	for (i = 0; i < LEN(sizes); i++) {
		win.w = sizes[i][0];
		win.h = sizes[i][1];
		xw.nextbuf = xcreatepixmap();
	xresize(win.w, win.h);
		assert(!vbell.buf && !vbell.draw && draws == 1);
		background();
		vbellset = 1;
		frame();
		vbellset = 0;
		frame();
	}

	/* All animation modes begin/end transparent and restore the original buffer. */
	win.w = W;
	win.h = H;
	xw.nextbuf = xcreatepixmap();
	xresize(win.w, win.h);
	background();
	vbellwidth = 40;
	vbellopacity = 0.25;
	vbelltimeout = 300;
	for (effect = VBELL_WAVE; effect <= VBELL_PULSE; effect++) {
		vbelleffect = effect;
		vbellset = 1;
		frameat(0);
		assert(!memcmp(original, pixels[WINDOW], sizeof(original)));
		frameat(0.5);
		assert(memcmp(original, pixels[WINDOW], sizeof(original)));
		memcpy(firstframe, pixels[WINDOW], sizeof(firstframe));
		frameat(0.5);
		assert(!memcmp(firstframe, pixels[WINDOW], sizeof(firstframe)));
		frameat(1);
		assert(!memcmp(original, pixels[WINDOW], sizeof(original)));
	}

	/* The travelling band advances across the screen without tinting other areas. */
	vbelleffect = VBELL_WAVE;
	frameat(0.25);
	left = waveposition();
	assert(left > 45 && left < 55 && !changed(W / 2, H / 2));
	frameat(0.75);
	assert(waveposition() - left > 130);
	assert(!changed(50, H / 2));

	/* The ripple expands from the center, leaving both its center and trail clear. */
	vbelleffect = VBELL_RIPPLE;
	frameat(0.25);
	assert(!changed(W / 2, H / 2) && !changed(0, 0));
	assert(changed(W / 2 + 28, H / 2));
	assert(changed(W / 2 - 28, H / 2));
	frameat(0.65);
	assert(!changed(W / 2 + 28, H / 2));
	assert(changed(W / 2 + 103, H / 2));

	/* The rectangular band moves inward from all four edges. */
	vbelleffect = VBELL_INWARD;
	frameat(0.25);
	assert(changed(W / 2, 12) && !changed(W / 2, H / 2));
	frameat(0.75);
	assert(!changed(W / 2, 12) && changed(W / 2, 77));

	/* A pulse tints the whole window at its peak. */
	vbelleffect = VBELL_PULSE;
	frameat(0.5);
	for (y = 0; y < win.h; y++)
		for (x = 0; x < win.w; x++)
			assert(changed(x, y));

	/* Custom RGB colors apply to every effect, including both native gradients. */
	vbellcolor = vbellinnercolor = 0x0000ff;
	for (effect = VBELL_GLOW; effect <= VBELL_PULSE; effect++) {
		vbelleffect = effect;
		frameat(0.5);
		count = 0;
		for (y = 0; y < win.h; y++)
			for (x = 0; x < win.w; x++) {
				assert((pixels[WINDOW][y][x] & 255) >= (original[y][x] & 255));
				assert((pixels[WINDOW][y][x] >> 8) <= (original[y][x] >> 8));
				count += changed(x, y);
			}
		assert(count);
	}
	vbellcolor = 0xff0000;
	vbellinnercolor = 0xffff00;

	/* Idle frames are scheduled, overdue frames bypass batching, hidden ones sleep. */
	vbelleffect = VBELL_WAVE;
	lastvbellframe = lastvbell;
	win.mode |= MODE_VISIBLE;
	assert(vbellnextframe(lastvbell) == vbellinterval);
	assert(vbellnextframe(after(lastvbell, 10)) == vbellinterval - 10);
	assert(vbellnextframe(after(lastvbell, 20)) == 0);
	assert(vbellprogress(after(lastvbell, 150)) == 0.5);
	vbellinterval = 0;
	assert(vbellnextframe(lastvbell) == 1);
	vbellinterval = 16;
	win.mode &= ~MODE_VISIBLE;
	assert(vbellnextframe(lastvbell) == vbelltimeout);
	assert(vbellremaining(after(lastvbell, vbelltimeout)) < 0 && !vbellset);
	frame();
	assert(!memcmp(original, pixels[WINDOW], sizeof(original)));

	/* Every effect remains bounded when an active animation is resized. */
	for (effect = VBELL_GLOW; effect <= VBELL_PULSE; effect++)
		for (i = 0; i < LEN(sizes); i++) {
			vbelleffect = effect;
			win.w = sizes[i][0];
			win.h = sizes[i][1];
			xw.nextbuf = xcreatepixmap();
	xresize(win.w, win.h);
			assert(!vbell.buf && !vbell.draw);
			background();
			vbellset = 1;
			frameat(0.5);
			if (vbellanimated())
				frameat(0.0000000001); /* tiny radial radii stay valid */
			vbellset = 0;
			frame();
		}

	/* Width/opacity disabling and missing Render support leave content intact. */
	vbellset = 1;
	vbellwidth = 0;
	frame();
	vbellwidth = 96;
	vbellopacity = 0;
	frame();
	vbellopacity = 0.6;
	vbellfree();
	render_available = 0;
	frame();
	assert(!vbell.buf && !vbell.draw);
	assert(!memcmp(original, pixels[WINDOW], sizeof(original)));
	vbellset = 0;
	vbelltimeout = 0;
	xbell();
	assert(!vbellset);

	/* An Expose leaves a pending resize to run(), which applies it once. */
	{
		XEvent ev = { .type = Expose };
		int err = dup(2), null = open("/dev/null", O_WRONLY);
		int w = win.w, h = win.h;

		dup2(null, 2); /* no tty to resize */
		before = allocations; /* one pixmap per applied resize */
		pendingw = W - 2;
		pendingh = H - 1;
		expose(&ev);
		pendingw = W - 3;
		expose(&ev);
		assert(win.w == w && win.h == h && allocations == before);
		applyresize();
		assert(win.w == W - 3 && win.h == H - 1 && allocations == before + 1);
		assert(!pendingw && !pendingh);
		pendingw = W;
		pendingh = H;
		applyresize();
		dup2(err, 2);
		close(err);
		close(null);
		assert(win.w == W && win.h == H);
	}

	vbellfree();
	XftDrawDestroy(xw.draw);
	XFreePixmap(xw.dpy, xw.buf);
	assert(!draws);
	for (i = 1; i < WINDOW; i++)
		assert(!pixmaps[i]);
	free(xw.specbuf);
	free(display);
	puts("PASS: glow/wave/ripple/inward/pulse, custom colors, animation geometry and deadlines, transparency, redraws, expiry, resize, tiny and huge windows");
	return 0;
}
