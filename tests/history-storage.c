#include "terminal.h"

static uint32_t seed = 1;

static uint32_t
random32(void)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed;
}

static size_t
bytes(const HistoryLine *row)
{
	return row->runs ? row->cols * sizeof(Rune) + row->runs * sizeof(HistoryRun) :
	                  row->cols * sizeof(Glyph);
}

static void
equal(Glyph a, Glyph b)
{
	assert(a.u == b.u && !ATTRCMP(a, b));
}

static void
roundtrip(Line line, int cols)
{
	HistoryLine row = sb_pack(line, cols);
	Glyph decoded[280];
	int widths[] = { 0, 1, cols / 2, cols, cols + 17 };
	int i, x;

	assert(row.cols == cols && bytes(&row) <= cols * sizeof(Glyph));
	for (i = 0; i < LEN(widths); i++) {
		/* A decoder may clip or pad, but must not write beyond its request. */
		decoded[widths[i]] = (Glyph){ .u = 0x10FFFF, .mode = ATTR_BLINK };
		sb_unpack(&row, decoded, widths[i]);
		for (x = 0; x < widths[i]; x++)
			equal(decoded[x], x < cols ? line[x] : sb_padglyph());
		equal(decoded[widths[i]], (Glyph){ .u = 0x10FFFF, .mode = ATTR_BLINK });
	}
	free(row.cells);
}

int
main(void)
{
	Glyph line[256], decoded[256];
	HistoryLine row;
	Line held[16];
	size_t total = 0;
	int i, x, cols, style;

	setlocale(LC_CTYPE, "C.UTF-8");
	reset(160, 5);
	for (x = 0; x < LEN(line); x++)
		line[x] = (Glyph){ .u = 'A' + x % 26, .fg = defaultfg,
			.bg = defaultbg, .ucolor = UCOLOR_DEFAULT };
	row = sb_pack(line, 150);
	assert(row.runs == 1 && bytes(&row) == 150 * sizeof(Rune) + sizeof(HistoryRun));
	free(row.cells);
	row = sb_pack(line, 1);
	assert(!row.runs); /* A tiny row is cheaper without a run header. */
	free(row.cells);
	for (x = 0; x < LEN(line); x++) {
		line[x].fg = TRUECOLOR(x, 255 - x, x);
		line[x].ucolor = TRUECOLOR(255 - x, x, 255 - x);
	}
	row = sb_pack(line, LEN(line));
	assert(!row.runs); /* Per-cell formatting must not increase storage. */
	free(row.cells);
	/* 111 runs over 160 cells still beat raw cells when a run costs a cell. */
	for (x = 111; x < 160; x++)
		line[x] = line[110];
	row = sb_pack(line, 160);
	assert(row.runs == 111 && bytes(&row) < 160 * sizeof(Glyph));
	free(row.cells);

	/* Every field survives, including zero/dummy cells and presentation flags. */
	for (i = 0; i < 6000; i++) {
		cols = 1 + random32() % LEN(line);
		style = 1 + random32() % cols;
		for (x = 0; x < cols; x++) {
			if (x % style) {
				line[x] = line[x - 1];
			} else {
				line[x] = (Glyph){ .mode = random32(), .fg = random32(),
					.bg = random32(), .ucolor = random32() };
			}
			line[x].u = random32();
		}
		roundtrip(line, cols);
	}

	/* Keep the full 64k ring, including eviction, within the compact budget. */
	sb_clear();
	free(sb.buf);
	sb_init(65536);
	for (x = 0; x < 160; x++) {
		line[x] = sb_padglyph();
		if (x < 150)
			line[x].u = 'x';
	}
	for (i = 0; i < 65536 + 64; i++) {
		line[0].u = 'A' + i % 26;
		sb_push(line);
	}
	assert(sb.len == 65536 && sb.base == 64);
	for (i = 0; i < sb.len; i++) {
		row = sb.buf[sb_phys_index(i)];
		assert(row.cols == 150 && row.runs == 1);
		total += bytes(&row);
		sb_unpack(&row, decoded, 160);
		assert(decoded[0].u == 'A' + (i + 64) % 26);
		equal(decoded[149], line[149]);
		equal(decoded[150], sb_padglyph());
	}
	assert(total < (size_t)40 * 1024 * 1024);
	/* Callers can hold several decoded rows while comparing wrapped text. */
	for (i = 0; i < LEN(held); i++)
		held[i] = sb_get(i);
	for (i = 0; i < LEN(held); i++)
		assert(held[i][0].u == 'A' + (i + 64) % 26);
	sb_clear();
	assert(sb.len == 0);

	printf("PASS: lossless compact/raw rows, clipping, padding, temporary row lifetime, "
	       "and 64k-line eviction (%.2f MiB payload)\n", total / 1048576.0);
	return 0;
}
