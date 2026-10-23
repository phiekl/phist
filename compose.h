/* Canonical compositions of a base and a combining character, sorted by
 * base and mark.  Generated from Python's unicodedata (Unicode 15.1.0):
 * every two-character canonical decomposition that NFC recomposes.
 * Hangul syllables compose algorithmically and are not listed. */
static const struct { Rune base, mark, comp; } composetbl[] = {
	{ 0x003C, 0x0338, 0x226E }, /* not less-than */
	{ 0x003D, 0x0338, 0x2260 }, /* not equal to */
	{ 0x003E, 0x0338, 0x226F }, /* not greater-than */
	{ 0x0041, 0x0300, 0x00C0 }, /* latin capital letter a with grave */
	{ 0x0041, 0x0301, 0x00C1 }, /* latin capital letter a with acute */
	{ 0x0041, 0x0302, 0x00C2 }, /* latin capital letter a with circumflex */
	{ 0x0041, 0x0303, 0x00C3 }, /* latin capital letter a with tilde */
	{ 0x0041, 0x0304, 0x0100 }, /* latin capital letter a with macron */
	{ 0x0041, 0x0306, 0x0102 }, /* latin capital letter a with breve */
	{ 0x0041, 0x0307, 0x0226 }, /* latin capital letter a with dot above */
	{ 0x0041, 0x0308, 0x00C4 }, /* latin capital letter a with diaeresis */
	{ 0x0041, 0x0309, 0x1EA2 }, /* latin capital letter a with hook above */
	{ 0x0041, 0x030A, 0x00C5 }, /* latin capital letter a with ring above */
	{ 0x0041, 0x030C, 0x01CD }, /* latin capital letter a with caron */
	{ 0x0041, 0x030F, 0x0200 }, /* latin capital letter a with double grave */
	{ 0x0041, 0x0311, 0x0202 }, /* latin capital letter a with inverted breve */
	{ 0x0041, 0x0323, 0x1EA0 }, /* latin capital letter a with dot below */
	{ 0x0041, 0x0325, 0x1E00 }, /* latin capital letter a with ring below */
	{ 0x0041, 0x0328, 0x0104 }, /* latin capital letter a with ogonek */
	{ 0x0042, 0x0307, 0x1E02 }, /* latin capital letter b with dot above */
	{ 0x0042, 0x0323, 0x1E04 }, /* latin capital letter b with dot below */
	{ 0x0042, 0x0331, 0x1E06 }, /* latin capital letter b with line below */
	{ 0x0043, 0x0301, 0x0106 }, /* latin capital letter c with acute */
	{ 0x0043, 0x0302, 0x0108 }, /* latin capital letter c with circumflex */
	{ 0x0043, 0x0307, 0x010A }, /* latin capital letter c with dot above */
	{ 0x0043, 0x030C, 0x010C }, /* latin capital letter c with caron */
	{ 0x0043, 0x0327, 0x00C7 }, /* latin capital letter c with cedilla */
	{ 0x0044, 0x0307, 0x1E0A }, /* latin capital letter d with dot above */
	{ 0x0044, 0x030C, 0x010E }, /* latin capital letter d with caron */
	{ 0x0044, 0x0323, 0x1E0C }, /* latin capital letter d with dot below */
	{ 0x0044, 0x0327, 0x1E10 }, /* latin capital letter d with cedilla */
	{ 0x0044, 0x032D, 0x1E12 }, /* latin capital letter d with circumflex below */
	{ 0x0044, 0x0331, 0x1E0E }, /* latin capital letter d with line below */
	{ 0x0045, 0x0300, 0x00C8 }, /* latin capital letter e with grave */
	{ 0x0045, 0x0301, 0x00C9 }, /* latin capital letter e with acute */
	{ 0x0045, 0x0302, 0x00CA }, /* latin capital letter e with circumflex */
	{ 0x0045, 0x0303, 0x1EBC }, /* latin capital letter e with tilde */
	{ 0x0045, 0x0304, 0x0112 }, /* latin capital letter e with macron */
	{ 0x0045, 0x0306, 0x0114 }, /* latin capital letter e with breve */
	{ 0x0045, 0x0307, 0x0116 }, /* latin capital letter e with dot above */
	{ 0x0045, 0x0308, 0x00CB }, /* latin capital letter e with diaeresis */
	{ 0x0045, 0x0309, 0x1EBA }, /* latin capital letter e with hook above */
	{ 0x0045, 0x030C, 0x011A }, /* latin capital letter e with caron */
	{ 0x0045, 0x030F, 0x0204 }, /* latin capital letter e with double grave */
	{ 0x0045, 0x0311, 0x0206 }, /* latin capital letter e with inverted breve */
	{ 0x0045, 0x0323, 0x1EB8 }, /* latin capital letter e with dot below */
	{ 0x0045, 0x0327, 0x0228 }, /* latin capital letter e with cedilla */
	{ 0x0045, 0x0328, 0x0118 }, /* latin capital letter e with ogonek */
	{ 0x0045, 0x032D, 0x1E18 }, /* latin capital letter e with circumflex below */
	{ 0x0045, 0x0330, 0x1E1A }, /* latin capital letter e with tilde below */
	{ 0x0046, 0x0307, 0x1E1E }, /* latin capital letter f with dot above */
	{ 0x0047, 0x0301, 0x01F4 }, /* latin capital letter g with acute */
	{ 0x0047, 0x0302, 0x011C }, /* latin capital letter g with circumflex */
	{ 0x0047, 0x0304, 0x1E20 }, /* latin capital letter g with macron */
	{ 0x0047, 0x0306, 0x011E }, /* latin capital letter g with breve */
	{ 0x0047, 0x0307, 0x0120 }, /* latin capital letter g with dot above */
	{ 0x0047, 0x030C, 0x01E6 }, /* latin capital letter g with caron */
	{ 0x0047, 0x0327, 0x0122 }, /* latin capital letter g with cedilla */
	{ 0x0048, 0x0302, 0x0124 }, /* latin capital letter h with circumflex */
	{ 0x0048, 0x0307, 0x1E22 }, /* latin capital letter h with dot above */
	{ 0x0048, 0x0308, 0x1E26 }, /* latin capital letter h with diaeresis */
	{ 0x0048, 0x030C, 0x021E }, /* latin capital letter h with caron */
	{ 0x0048, 0x0323, 0x1E24 }, /* latin capital letter h with dot below */
	{ 0x0048, 0x0327, 0x1E28 }, /* latin capital letter h with cedilla */
	{ 0x0048, 0x032E, 0x1E2A }, /* latin capital letter h with breve below */
	{ 0x0049, 0x0300, 0x00CC }, /* latin capital letter i with grave */
	{ 0x0049, 0x0301, 0x00CD }, /* latin capital letter i with acute */
	{ 0x0049, 0x0302, 0x00CE }, /* latin capital letter i with circumflex */
	{ 0x0049, 0x0303, 0x0128 }, /* latin capital letter i with tilde */
	{ 0x0049, 0x0304, 0x012A }, /* latin capital letter i with macron */
	{ 0x0049, 0x0306, 0x012C }, /* latin capital letter i with breve */
	{ 0x0049, 0x0307, 0x0130 }, /* latin capital letter i with dot above */
	{ 0x0049, 0x0308, 0x00CF }, /* latin capital letter i with diaeresis */
	{ 0x0049, 0x0309, 0x1EC8 }, /* latin capital letter i with hook above */
	{ 0x0049, 0x030C, 0x01CF }, /* latin capital letter i with caron */
	{ 0x0049, 0x030F, 0x0208 }, /* latin capital letter i with double grave */
	{ 0x0049, 0x0311, 0x020A }, /* latin capital letter i with inverted breve */
	{ 0x0049, 0x0323, 0x1ECA }, /* latin capital letter i with dot below */
	{ 0x0049, 0x0328, 0x012E }, /* latin capital letter i with ogonek */
	{ 0x0049, 0x0330, 0x1E2C }, /* latin capital letter i with tilde below */
	{ 0x004A, 0x0302, 0x0134 }, /* latin capital letter j with circumflex */
	{ 0x004B, 0x0301, 0x1E30 }, /* latin capital letter k with acute */
	{ 0x004B, 0x030C, 0x01E8 }, /* latin capital letter k with caron */
	{ 0x004B, 0x0323, 0x1E32 }, /* latin capital letter k with dot below */
	{ 0x004B, 0x0327, 0x0136 }, /* latin capital letter k with cedilla */
	{ 0x004B, 0x0331, 0x1E34 }, /* latin capital letter k with line below */
	{ 0x004C, 0x0301, 0x0139 }, /* latin capital letter l with acute */
	{ 0x004C, 0x030C, 0x013D }, /* latin capital letter l with caron */
	{ 0x004C, 0x0323, 0x1E36 }, /* latin capital letter l with dot below */
	{ 0x004C, 0x0327, 0x013B }, /* latin capital letter l with cedilla */
	{ 0x004C, 0x032D, 0x1E3C }, /* latin capital letter l with circumflex below */
	{ 0x004C, 0x0331, 0x1E3A }, /* latin capital letter l with line below */
	{ 0x004D, 0x0301, 0x1E3E }, /* latin capital letter m with acute */
	{ 0x004D, 0x0307, 0x1E40 }, /* latin capital letter m with dot above */
	{ 0x004D, 0x0323, 0x1E42 }, /* latin capital letter m with dot below */
	{ 0x004E, 0x0300, 0x01F8 }, /* latin capital letter n with grave */
	{ 0x004E, 0x0301, 0x0143 }, /* latin capital letter n with acute */
	{ 0x004E, 0x0303, 0x00D1 }, /* latin capital letter n with tilde */
	{ 0x004E, 0x0307, 0x1E44 }, /* latin capital letter n with dot above */
	{ 0x004E, 0x030C, 0x0147 }, /* latin capital letter n with caron */
	{ 0x004E, 0x0323, 0x1E46 }, /* latin capital letter n with dot below */
	{ 0x004E, 0x0327, 0x0145 }, /* latin capital letter n with cedilla */
	{ 0x004E, 0x032D, 0x1E4A }, /* latin capital letter n with circumflex below */
	{ 0x004E, 0x0331, 0x1E48 }, /* latin capital letter n with line below */
	{ 0x004F, 0x0300, 0x00D2 }, /* latin capital letter o with grave */
	{ 0x004F, 0x0301, 0x00D3 }, /* latin capital letter o with acute */
	{ 0x004F, 0x0302, 0x00D4 }, /* latin capital letter o with circumflex */
	{ 0x004F, 0x0303, 0x00D5 }, /* latin capital letter o with tilde */
	{ 0x004F, 0x0304, 0x014C }, /* latin capital letter o with macron */
	{ 0x004F, 0x0306, 0x014E }, /* latin capital letter o with breve */
	{ 0x004F, 0x0307, 0x022E }, /* latin capital letter o with dot above */
	{ 0x004F, 0x0308, 0x00D6 }, /* latin capital letter o with diaeresis */
	{ 0x004F, 0x0309, 0x1ECE }, /* latin capital letter o with hook above */
	{ 0x004F, 0x030B, 0x0150 }, /* latin capital letter o with double acute */
	{ 0x004F, 0x030C, 0x01D1 }, /* latin capital letter o with caron */
	{ 0x004F, 0x030F, 0x020C }, /* latin capital letter o with double grave */
	{ 0x004F, 0x0311, 0x020E }, /* latin capital letter o with inverted breve */
	{ 0x004F, 0x031B, 0x01A0 }, /* latin capital letter o with horn */
	{ 0x004F, 0x0323, 0x1ECC }, /* latin capital letter o with dot below */
	{ 0x004F, 0x0328, 0x01EA }, /* latin capital letter o with ogonek */
	{ 0x0050, 0x0301, 0x1E54 }, /* latin capital letter p with acute */
	{ 0x0050, 0x0307, 0x1E56 }, /* latin capital letter p with dot above */
	{ 0x0052, 0x0301, 0x0154 }, /* latin capital letter r with acute */
	{ 0x0052, 0x0307, 0x1E58 }, /* latin capital letter r with dot above */
	{ 0x0052, 0x030C, 0x0158 }, /* latin capital letter r with caron */
	{ 0x0052, 0x030F, 0x0210 }, /* latin capital letter r with double grave */
	{ 0x0052, 0x0311, 0x0212 }, /* latin capital letter r with inverted breve */
	{ 0x0052, 0x0323, 0x1E5A }, /* latin capital letter r with dot below */
	{ 0x0052, 0x0327, 0x0156 }, /* latin capital letter r with cedilla */
	{ 0x0052, 0x0331, 0x1E5E }, /* latin capital letter r with line below */
	{ 0x0053, 0x0301, 0x015A }, /* latin capital letter s with acute */
	{ 0x0053, 0x0302, 0x015C }, /* latin capital letter s with circumflex */
	{ 0x0053, 0x0307, 0x1E60 }, /* latin capital letter s with dot above */
	{ 0x0053, 0x030C, 0x0160 }, /* latin capital letter s with caron */
	{ 0x0053, 0x0323, 0x1E62 }, /* latin capital letter s with dot below */
	{ 0x0053, 0x0326, 0x0218 }, /* latin capital letter s with comma below */
	{ 0x0053, 0x0327, 0x015E }, /* latin capital letter s with cedilla */
	{ 0x0054, 0x0307, 0x1E6A }, /* latin capital letter t with dot above */
	{ 0x0054, 0x030C, 0x0164 }, /* latin capital letter t with caron */
	{ 0x0054, 0x0323, 0x1E6C }, /* latin capital letter t with dot below */
	{ 0x0054, 0x0326, 0x021A }, /* latin capital letter t with comma below */
	{ 0x0054, 0x0327, 0x0162 }, /* latin capital letter t with cedilla */
	{ 0x0054, 0x032D, 0x1E70 }, /* latin capital letter t with circumflex below */
	{ 0x0054, 0x0331, 0x1E6E }, /* latin capital letter t with line below */
	{ 0x0055, 0x0300, 0x00D9 }, /* latin capital letter u with grave */
	{ 0x0055, 0x0301, 0x00DA }, /* latin capital letter u with acute */
	{ 0x0055, 0x0302, 0x00DB }, /* latin capital letter u with circumflex */
	{ 0x0055, 0x0303, 0x0168 }, /* latin capital letter u with tilde */
	{ 0x0055, 0x0304, 0x016A }, /* latin capital letter u with macron */
	{ 0x0055, 0x0306, 0x016C }, /* latin capital letter u with breve */
	{ 0x0055, 0x0308, 0x00DC }, /* latin capital letter u with diaeresis */
	{ 0x0055, 0x0309, 0x1EE6 }, /* latin capital letter u with hook above */
	{ 0x0055, 0x030A, 0x016E }, /* latin capital letter u with ring above */
	{ 0x0055, 0x030B, 0x0170 }, /* latin capital letter u with double acute */
	{ 0x0055, 0x030C, 0x01D3 }, /* latin capital letter u with caron */
	{ 0x0055, 0x030F, 0x0214 }, /* latin capital letter u with double grave */
	{ 0x0055, 0x0311, 0x0216 }, /* latin capital letter u with inverted breve */
	{ 0x0055, 0x031B, 0x01AF }, /* latin capital letter u with horn */
	{ 0x0055, 0x0323, 0x1EE4 }, /* latin capital letter u with dot below */
	{ 0x0055, 0x0324, 0x1E72 }, /* latin capital letter u with diaeresis below */
	{ 0x0055, 0x0328, 0x0172 }, /* latin capital letter u with ogonek */
	{ 0x0055, 0x032D, 0x1E76 }, /* latin capital letter u with circumflex below */
	{ 0x0055, 0x0330, 0x1E74 }, /* latin capital letter u with tilde below */
	{ 0x0056, 0x0303, 0x1E7C }, /* latin capital letter v with tilde */
	{ 0x0056, 0x0323, 0x1E7E }, /* latin capital letter v with dot below */
	{ 0x0057, 0x0300, 0x1E80 }, /* latin capital letter w with grave */
	{ 0x0057, 0x0301, 0x1E82 }, /* latin capital letter w with acute */
	{ 0x0057, 0x0302, 0x0174 }, /* latin capital letter w with circumflex */
	{ 0x0057, 0x0307, 0x1E86 }, /* latin capital letter w with dot above */
	{ 0x0057, 0x0308, 0x1E84 }, /* latin capital letter w with diaeresis */
	{ 0x0057, 0x0323, 0x1E88 }, /* latin capital letter w with dot below */
	{ 0x0058, 0x0307, 0x1E8A }, /* latin capital letter x with dot above */
	{ 0x0058, 0x0308, 0x1E8C }, /* latin capital letter x with diaeresis */
	{ 0x0059, 0x0300, 0x1EF2 }, /* latin capital letter y with grave */
	{ 0x0059, 0x0301, 0x00DD }, /* latin capital letter y with acute */
	{ 0x0059, 0x0302, 0x0176 }, /* latin capital letter y with circumflex */
	{ 0x0059, 0x0303, 0x1EF8 }, /* latin capital letter y with tilde */
	{ 0x0059, 0x0304, 0x0232 }, /* latin capital letter y with macron */
	{ 0x0059, 0x0307, 0x1E8E }, /* latin capital letter y with dot above */
	{ 0x0059, 0x0308, 0x0178 }, /* latin capital letter y with diaeresis */
	{ 0x0059, 0x0309, 0x1EF6 }, /* latin capital letter y with hook above */
	{ 0x0059, 0x0323, 0x1EF4 }, /* latin capital letter y with dot below */
	{ 0x005A, 0x0301, 0x0179 }, /* latin capital letter z with acute */
	{ 0x005A, 0x0302, 0x1E90 }, /* latin capital letter z with circumflex */
	{ 0x005A, 0x0307, 0x017B }, /* latin capital letter z with dot above */
	{ 0x005A, 0x030C, 0x017D }, /* latin capital letter z with caron */
	{ 0x005A, 0x0323, 0x1E92 }, /* latin capital letter z with dot below */
	{ 0x005A, 0x0331, 0x1E94 }, /* latin capital letter z with line below */
	{ 0x0061, 0x0300, 0x00E0 }, /* latin small letter a with grave */
	{ 0x0061, 0x0301, 0x00E1 }, /* latin small letter a with acute */
	{ 0x0061, 0x0302, 0x00E2 }, /* latin small letter a with circumflex */
	{ 0x0061, 0x0303, 0x00E3 }, /* latin small letter a with tilde */
	{ 0x0061, 0x0304, 0x0101 }, /* latin small letter a with macron */
	{ 0x0061, 0x0306, 0x0103 }, /* latin small letter a with breve */
	{ 0x0061, 0x0307, 0x0227 }, /* latin small letter a with dot above */
	{ 0x0061, 0x0308, 0x00E4 }, /* latin small letter a with diaeresis */
	{ 0x0061, 0x0309, 0x1EA3 }, /* latin small letter a with hook above */
	{ 0x0061, 0x030A, 0x00E5 }, /* latin small letter a with ring above */
	{ 0x0061, 0x030C, 0x01CE }, /* latin small letter a with caron */
	{ 0x0061, 0x030F, 0x0201 }, /* latin small letter a with double grave */
	{ 0x0061, 0x0311, 0x0203 }, /* latin small letter a with inverted breve */
	{ 0x0061, 0x0323, 0x1EA1 }, /* latin small letter a with dot below */
	{ 0x0061, 0x0325, 0x1E01 }, /* latin small letter a with ring below */
	{ 0x0061, 0x0328, 0x0105 }, /* latin small letter a with ogonek */
	{ 0x0062, 0x0307, 0x1E03 }, /* latin small letter b with dot above */
	{ 0x0062, 0x0323, 0x1E05 }, /* latin small letter b with dot below */
	{ 0x0062, 0x0331, 0x1E07 }, /* latin small letter b with line below */
	{ 0x0063, 0x0301, 0x0107 }, /* latin small letter c with acute */
	{ 0x0063, 0x0302, 0x0109 }, /* latin small letter c with circumflex */
	{ 0x0063, 0x0307, 0x010B }, /* latin small letter c with dot above */
	{ 0x0063, 0x030C, 0x010D }, /* latin small letter c with caron */
	{ 0x0063, 0x0327, 0x00E7 }, /* latin small letter c with cedilla */
	{ 0x0064, 0x0307, 0x1E0B }, /* latin small letter d with dot above */
	{ 0x0064, 0x030C, 0x010F }, /* latin small letter d with caron */
	{ 0x0064, 0x0323, 0x1E0D }, /* latin small letter d with dot below */
	{ 0x0064, 0x0327, 0x1E11 }, /* latin small letter d with cedilla */
	{ 0x0064, 0x032D, 0x1E13 }, /* latin small letter d with circumflex below */
	{ 0x0064, 0x0331, 0x1E0F }, /* latin small letter d with line below */
	{ 0x0065, 0x0300, 0x00E8 }, /* latin small letter e with grave */
	{ 0x0065, 0x0301, 0x00E9 }, /* latin small letter e with acute */
	{ 0x0065, 0x0302, 0x00EA }, /* latin small letter e with circumflex */
	{ 0x0065, 0x0303, 0x1EBD }, /* latin small letter e with tilde */
	{ 0x0065, 0x0304, 0x0113 }, /* latin small letter e with macron */
	{ 0x0065, 0x0306, 0x0115 }, /* latin small letter e with breve */
	{ 0x0065, 0x0307, 0x0117 }, /* latin small letter e with dot above */
	{ 0x0065, 0x0308, 0x00EB }, /* latin small letter e with diaeresis */
	{ 0x0065, 0x0309, 0x1EBB }, /* latin small letter e with hook above */
	{ 0x0065, 0x030C, 0x011B }, /* latin small letter e with caron */
	{ 0x0065, 0x030F, 0x0205 }, /* latin small letter e with double grave */
	{ 0x0065, 0x0311, 0x0207 }, /* latin small letter e with inverted breve */
	{ 0x0065, 0x0323, 0x1EB9 }, /* latin small letter e with dot below */
	{ 0x0065, 0x0327, 0x0229 }, /* latin small letter e with cedilla */
	{ 0x0065, 0x0328, 0x0119 }, /* latin small letter e with ogonek */
	{ 0x0065, 0x032D, 0x1E19 }, /* latin small letter e with circumflex below */
	{ 0x0065, 0x0330, 0x1E1B }, /* latin small letter e with tilde below */
	{ 0x0066, 0x0307, 0x1E1F }, /* latin small letter f with dot above */
	{ 0x0067, 0x0301, 0x01F5 }, /* latin small letter g with acute */
	{ 0x0067, 0x0302, 0x011D }, /* latin small letter g with circumflex */
	{ 0x0067, 0x0304, 0x1E21 }, /* latin small letter g with macron */
	{ 0x0067, 0x0306, 0x011F }, /* latin small letter g with breve */
	{ 0x0067, 0x0307, 0x0121 }, /* latin small letter g with dot above */
	{ 0x0067, 0x030C, 0x01E7 }, /* latin small letter g with caron */
	{ 0x0067, 0x0327, 0x0123 }, /* latin small letter g with cedilla */
	{ 0x0068, 0x0302, 0x0125 }, /* latin small letter h with circumflex */
	{ 0x0068, 0x0307, 0x1E23 }, /* latin small letter h with dot above */
	{ 0x0068, 0x0308, 0x1E27 }, /* latin small letter h with diaeresis */
	{ 0x0068, 0x030C, 0x021F }, /* latin small letter h with caron */
	{ 0x0068, 0x0323, 0x1E25 }, /* latin small letter h with dot below */
	{ 0x0068, 0x0327, 0x1E29 }, /* latin small letter h with cedilla */
	{ 0x0068, 0x032E, 0x1E2B }, /* latin small letter h with breve below */
	{ 0x0068, 0x0331, 0x1E96 }, /* latin small letter h with line below */
	{ 0x0069, 0x0300, 0x00EC }, /* latin small letter i with grave */
	{ 0x0069, 0x0301, 0x00ED }, /* latin small letter i with acute */
	{ 0x0069, 0x0302, 0x00EE }, /* latin small letter i with circumflex */
	{ 0x0069, 0x0303, 0x0129 }, /* latin small letter i with tilde */
	{ 0x0069, 0x0304, 0x012B }, /* latin small letter i with macron */
	{ 0x0069, 0x0306, 0x012D }, /* latin small letter i with breve */
	{ 0x0069, 0x0308, 0x00EF }, /* latin small letter i with diaeresis */
	{ 0x0069, 0x0309, 0x1EC9 }, /* latin small letter i with hook above */
	{ 0x0069, 0x030C, 0x01D0 }, /* latin small letter i with caron */
	{ 0x0069, 0x030F, 0x0209 }, /* latin small letter i with double grave */
	{ 0x0069, 0x0311, 0x020B }, /* latin small letter i with inverted breve */
	{ 0x0069, 0x0323, 0x1ECB }, /* latin small letter i with dot below */
	{ 0x0069, 0x0328, 0x012F }, /* latin small letter i with ogonek */
	{ 0x0069, 0x0330, 0x1E2D }, /* latin small letter i with tilde below */
	{ 0x006A, 0x0302, 0x0135 }, /* latin small letter j with circumflex */
	{ 0x006A, 0x030C, 0x01F0 }, /* latin small letter j with caron */
	{ 0x006B, 0x0301, 0x1E31 }, /* latin small letter k with acute */
	{ 0x006B, 0x030C, 0x01E9 }, /* latin small letter k with caron */
	{ 0x006B, 0x0323, 0x1E33 }, /* latin small letter k with dot below */
	{ 0x006B, 0x0327, 0x0137 }, /* latin small letter k with cedilla */
	{ 0x006B, 0x0331, 0x1E35 }, /* latin small letter k with line below */
	{ 0x006C, 0x0301, 0x013A }, /* latin small letter l with acute */
	{ 0x006C, 0x030C, 0x013E }, /* latin small letter l with caron */
	{ 0x006C, 0x0323, 0x1E37 }, /* latin small letter l with dot below */
	{ 0x006C, 0x0327, 0x013C }, /* latin small letter l with cedilla */
	{ 0x006C, 0x032D, 0x1E3D }, /* latin small letter l with circumflex below */
	{ 0x006C, 0x0331, 0x1E3B }, /* latin small letter l with line below */
	{ 0x006D, 0x0301, 0x1E3F }, /* latin small letter m with acute */
	{ 0x006D, 0x0307, 0x1E41 }, /* latin small letter m with dot above */
	{ 0x006D, 0x0323, 0x1E43 }, /* latin small letter m with dot below */
	{ 0x006E, 0x0300, 0x01F9 }, /* latin small letter n with grave */
	{ 0x006E, 0x0301, 0x0144 }, /* latin small letter n with acute */
	{ 0x006E, 0x0303, 0x00F1 }, /* latin small letter n with tilde */
	{ 0x006E, 0x0307, 0x1E45 }, /* latin small letter n with dot above */
	{ 0x006E, 0x030C, 0x0148 }, /* latin small letter n with caron */
	{ 0x006E, 0x0323, 0x1E47 }, /* latin small letter n with dot below */
	{ 0x006E, 0x0327, 0x0146 }, /* latin small letter n with cedilla */
	{ 0x006E, 0x032D, 0x1E4B }, /* latin small letter n with circumflex below */
	{ 0x006E, 0x0331, 0x1E49 }, /* latin small letter n with line below */
	{ 0x006F, 0x0300, 0x00F2 }, /* latin small letter o with grave */
	{ 0x006F, 0x0301, 0x00F3 }, /* latin small letter o with acute */
	{ 0x006F, 0x0302, 0x00F4 }, /* latin small letter o with circumflex */
	{ 0x006F, 0x0303, 0x00F5 }, /* latin small letter o with tilde */
	{ 0x006F, 0x0304, 0x014D }, /* latin small letter o with macron */
	{ 0x006F, 0x0306, 0x014F }, /* latin small letter o with breve */
	{ 0x006F, 0x0307, 0x022F }, /* latin small letter o with dot above */
	{ 0x006F, 0x0308, 0x00F6 }, /* latin small letter o with diaeresis */
	{ 0x006F, 0x0309, 0x1ECF }, /* latin small letter o with hook above */
	{ 0x006F, 0x030B, 0x0151 }, /* latin small letter o with double acute */
	{ 0x006F, 0x030C, 0x01D2 }, /* latin small letter o with caron */
	{ 0x006F, 0x030F, 0x020D }, /* latin small letter o with double grave */
	{ 0x006F, 0x0311, 0x020F }, /* latin small letter o with inverted breve */
	{ 0x006F, 0x031B, 0x01A1 }, /* latin small letter o with horn */
	{ 0x006F, 0x0323, 0x1ECD }, /* latin small letter o with dot below */
	{ 0x006F, 0x0328, 0x01EB }, /* latin small letter o with ogonek */
	{ 0x0070, 0x0301, 0x1E55 }, /* latin small letter p with acute */
	{ 0x0070, 0x0307, 0x1E57 }, /* latin small letter p with dot above */
	{ 0x0072, 0x0301, 0x0155 }, /* latin small letter r with acute */
	{ 0x0072, 0x0307, 0x1E59 }, /* latin small letter r with dot above */
	{ 0x0072, 0x030C, 0x0159 }, /* latin small letter r with caron */
	{ 0x0072, 0x030F, 0x0211 }, /* latin small letter r with double grave */
	{ 0x0072, 0x0311, 0x0213 }, /* latin small letter r with inverted breve */
	{ 0x0072, 0x0323, 0x1E5B }, /* latin small letter r with dot below */
	{ 0x0072, 0x0327, 0x0157 }, /* latin small letter r with cedilla */
	{ 0x0072, 0x0331, 0x1E5F }, /* latin small letter r with line below */
	{ 0x0073, 0x0301, 0x015B }, /* latin small letter s with acute */
	{ 0x0073, 0x0302, 0x015D }, /* latin small letter s with circumflex */
	{ 0x0073, 0x0307, 0x1E61 }, /* latin small letter s with dot above */
	{ 0x0073, 0x030C, 0x0161 }, /* latin small letter s with caron */
	{ 0x0073, 0x0323, 0x1E63 }, /* latin small letter s with dot below */
	{ 0x0073, 0x0326, 0x0219 }, /* latin small letter s with comma below */
	{ 0x0073, 0x0327, 0x015F }, /* latin small letter s with cedilla */
	{ 0x0074, 0x0307, 0x1E6B }, /* latin small letter t with dot above */
	{ 0x0074, 0x0308, 0x1E97 }, /* latin small letter t with diaeresis */
	{ 0x0074, 0x030C, 0x0165 }, /* latin small letter t with caron */
	{ 0x0074, 0x0323, 0x1E6D }, /* latin small letter t with dot below */
	{ 0x0074, 0x0326, 0x021B }, /* latin small letter t with comma below */
	{ 0x0074, 0x0327, 0x0163 }, /* latin small letter t with cedilla */
	{ 0x0074, 0x032D, 0x1E71 }, /* latin small letter t with circumflex below */
	{ 0x0074, 0x0331, 0x1E6F }, /* latin small letter t with line below */
	{ 0x0075, 0x0300, 0x00F9 }, /* latin small letter u with grave */
	{ 0x0075, 0x0301, 0x00FA }, /* latin small letter u with acute */
	{ 0x0075, 0x0302, 0x00FB }, /* latin small letter u with circumflex */
	{ 0x0075, 0x0303, 0x0169 }, /* latin small letter u with tilde */
	{ 0x0075, 0x0304, 0x016B }, /* latin small letter u with macron */
	{ 0x0075, 0x0306, 0x016D }, /* latin small letter u with breve */
	{ 0x0075, 0x0308, 0x00FC }, /* latin small letter u with diaeresis */
	{ 0x0075, 0x0309, 0x1EE7 }, /* latin small letter u with hook above */
	{ 0x0075, 0x030A, 0x016F }, /* latin small letter u with ring above */
	{ 0x0075, 0x030B, 0x0171 }, /* latin small letter u with double acute */
	{ 0x0075, 0x030C, 0x01D4 }, /* latin small letter u with caron */
	{ 0x0075, 0x030F, 0x0215 }, /* latin small letter u with double grave */
	{ 0x0075, 0x0311, 0x0217 }, /* latin small letter u with inverted breve */
	{ 0x0075, 0x031B, 0x01B0 }, /* latin small letter u with horn */
	{ 0x0075, 0x0323, 0x1EE5 }, /* latin small letter u with dot below */
	{ 0x0075, 0x0324, 0x1E73 }, /* latin small letter u with diaeresis below */
	{ 0x0075, 0x0328, 0x0173 }, /* latin small letter u with ogonek */
	{ 0x0075, 0x032D, 0x1E77 }, /* latin small letter u with circumflex below */
	{ 0x0075, 0x0330, 0x1E75 }, /* latin small letter u with tilde below */
	{ 0x0076, 0x0303, 0x1E7D }, /* latin small letter v with tilde */
	{ 0x0076, 0x0323, 0x1E7F }, /* latin small letter v with dot below */
	{ 0x0077, 0x0300, 0x1E81 }, /* latin small letter w with grave */
	{ 0x0077, 0x0301, 0x1E83 }, /* latin small letter w with acute */
	{ 0x0077, 0x0302, 0x0175 }, /* latin small letter w with circumflex */
	{ 0x0077, 0x0307, 0x1E87 }, /* latin small letter w with dot above */
	{ 0x0077, 0x0308, 0x1E85 }, /* latin small letter w with diaeresis */
	{ 0x0077, 0x030A, 0x1E98 }, /* latin small letter w with ring above */
	{ 0x0077, 0x0323, 0x1E89 }, /* latin small letter w with dot below */
	{ 0x0078, 0x0307, 0x1E8B }, /* latin small letter x with dot above */
	{ 0x0078, 0x0308, 0x1E8D }, /* latin small letter x with diaeresis */
	{ 0x0079, 0x0300, 0x1EF3 }, /* latin small letter y with grave */
	{ 0x0079, 0x0301, 0x00FD }, /* latin small letter y with acute */
	{ 0x0079, 0x0302, 0x0177 }, /* latin small letter y with circumflex */
	{ 0x0079, 0x0303, 0x1EF9 }, /* latin small letter y with tilde */
	{ 0x0079, 0x0304, 0x0233 }, /* latin small letter y with macron */
	{ 0x0079, 0x0307, 0x1E8F }, /* latin small letter y with dot above */
	{ 0x0079, 0x0308, 0x00FF }, /* latin small letter y with diaeresis */
	{ 0x0079, 0x0309, 0x1EF7 }, /* latin small letter y with hook above */
	{ 0x0079, 0x030A, 0x1E99 }, /* latin small letter y with ring above */
	{ 0x0079, 0x0323, 0x1EF5 }, /* latin small letter y with dot below */
	{ 0x007A, 0x0301, 0x017A }, /* latin small letter z with acute */
	{ 0x007A, 0x0302, 0x1E91 }, /* latin small letter z with circumflex */
	{ 0x007A, 0x0307, 0x017C }, /* latin small letter z with dot above */
	{ 0x007A, 0x030C, 0x017E }, /* latin small letter z with caron */
	{ 0x007A, 0x0323, 0x1E93 }, /* latin small letter z with dot below */
	{ 0x007A, 0x0331, 0x1E95 }, /* latin small letter z with line below */
	{ 0x00A8, 0x0300, 0x1FED }, /* greek dialytika and varia */
	{ 0x00A8, 0x0301, 0x0385 }, /* greek dialytika tonos */
	{ 0x00A8, 0x0342, 0x1FC1 }, /* greek dialytika and perispomeni */
	{ 0x00C2, 0x0300, 0x1EA6 }, /* latin capital letter a with circumflex and grave */
	{ 0x00C2, 0x0301, 0x1EA4 }, /* latin capital letter a with circumflex and acute */
	{ 0x00C2, 0x0303, 0x1EAA }, /* latin capital letter a with circumflex and tilde */
	{ 0x00C2, 0x0309, 0x1EA8 }, /* latin capital letter a with circumflex and hook above */
	{ 0x00C4, 0x0304, 0x01DE }, /* latin capital letter a with diaeresis and macron */
	{ 0x00C5, 0x0301, 0x01FA }, /* latin capital letter a with ring above and acute */
	{ 0x00C6, 0x0301, 0x01FC }, /* latin capital letter ae with acute */
	{ 0x00C6, 0x0304, 0x01E2 }, /* latin capital letter ae with macron */
	{ 0x00C7, 0x0301, 0x1E08 }, /* latin capital letter c with cedilla and acute */
	{ 0x00CA, 0x0300, 0x1EC0 }, /* latin capital letter e with circumflex and grave */
	{ 0x00CA, 0x0301, 0x1EBE }, /* latin capital letter e with circumflex and acute */
	{ 0x00CA, 0x0303, 0x1EC4 }, /* latin capital letter e with circumflex and tilde */
	{ 0x00CA, 0x0309, 0x1EC2 }, /* latin capital letter e with circumflex and hook above */
	{ 0x00CF, 0x0301, 0x1E2E }, /* latin capital letter i with diaeresis and acute */
	{ 0x00D4, 0x0300, 0x1ED2 }, /* latin capital letter o with circumflex and grave */
	{ 0x00D4, 0x0301, 0x1ED0 }, /* latin capital letter o with circumflex and acute */
	{ 0x00D4, 0x0303, 0x1ED6 }, /* latin capital letter o with circumflex and tilde */
	{ 0x00D4, 0x0309, 0x1ED4 }, /* latin capital letter o with circumflex and hook above */
	{ 0x00D5, 0x0301, 0x1E4C }, /* latin capital letter o with tilde and acute */
	{ 0x00D5, 0x0304, 0x022C }, /* latin capital letter o with tilde and macron */
	{ 0x00D5, 0x0308, 0x1E4E }, /* latin capital letter o with tilde and diaeresis */
	{ 0x00D6, 0x0304, 0x022A }, /* latin capital letter o with diaeresis and macron */
	{ 0x00D8, 0x0301, 0x01FE }, /* latin capital letter o with stroke and acute */
	{ 0x00DC, 0x0300, 0x01DB }, /* latin capital letter u with diaeresis and grave */
	{ 0x00DC, 0x0301, 0x01D7 }, /* latin capital letter u with diaeresis and acute */
	{ 0x00DC, 0x0304, 0x01D5 }, /* latin capital letter u with diaeresis and macron */
	{ 0x00DC, 0x030C, 0x01D9 }, /* latin capital letter u with diaeresis and caron */
	{ 0x00E2, 0x0300, 0x1EA7 }, /* latin small letter a with circumflex and grave */
	{ 0x00E2, 0x0301, 0x1EA5 }, /* latin small letter a with circumflex and acute */
	{ 0x00E2, 0x0303, 0x1EAB }, /* latin small letter a with circumflex and tilde */
	{ 0x00E2, 0x0309, 0x1EA9 }, /* latin small letter a with circumflex and hook above */
	{ 0x00E4, 0x0304, 0x01DF }, /* latin small letter a with diaeresis and macron */
	{ 0x00E5, 0x0301, 0x01FB }, /* latin small letter a with ring above and acute */
	{ 0x00E6, 0x0301, 0x01FD }, /* latin small letter ae with acute */
	{ 0x00E6, 0x0304, 0x01E3 }, /* latin small letter ae with macron */
	{ 0x00E7, 0x0301, 0x1E09 }, /* latin small letter c with cedilla and acute */
	{ 0x00EA, 0x0300, 0x1EC1 }, /* latin small letter e with circumflex and grave */
	{ 0x00EA, 0x0301, 0x1EBF }, /* latin small letter e with circumflex and acute */
	{ 0x00EA, 0x0303, 0x1EC5 }, /* latin small letter e with circumflex and tilde */
	{ 0x00EA, 0x0309, 0x1EC3 }, /* latin small letter e with circumflex and hook above */
	{ 0x00EF, 0x0301, 0x1E2F }, /* latin small letter i with diaeresis and acute */
	{ 0x00F4, 0x0300, 0x1ED3 }, /* latin small letter o with circumflex and grave */
	{ 0x00F4, 0x0301, 0x1ED1 }, /* latin small letter o with circumflex and acute */
	{ 0x00F4, 0x0303, 0x1ED7 }, /* latin small letter o with circumflex and tilde */
	{ 0x00F4, 0x0309, 0x1ED5 }, /* latin small letter o with circumflex and hook above */
	{ 0x00F5, 0x0301, 0x1E4D }, /* latin small letter o with tilde and acute */
	{ 0x00F5, 0x0304, 0x022D }, /* latin small letter o with tilde and macron */
	{ 0x00F5, 0x0308, 0x1E4F }, /* latin small letter o with tilde and diaeresis */
	{ 0x00F6, 0x0304, 0x022B }, /* latin small letter o with diaeresis and macron */
	{ 0x00F8, 0x0301, 0x01FF }, /* latin small letter o with stroke and acute */
	{ 0x00FC, 0x0300, 0x01DC }, /* latin small letter u with diaeresis and grave */
	{ 0x00FC, 0x0301, 0x01D8 }, /* latin small letter u with diaeresis and acute */
	{ 0x00FC, 0x0304, 0x01D6 }, /* latin small letter u with diaeresis and macron */
	{ 0x00FC, 0x030C, 0x01DA }, /* latin small letter u with diaeresis and caron */
	{ 0x0102, 0x0300, 0x1EB0 }, /* latin capital letter a with breve and grave */
	{ 0x0102, 0x0301, 0x1EAE }, /* latin capital letter a with breve and acute */
	{ 0x0102, 0x0303, 0x1EB4 }, /* latin capital letter a with breve and tilde */
	{ 0x0102, 0x0309, 0x1EB2 }, /* latin capital letter a with breve and hook above */
	{ 0x0103, 0x0300, 0x1EB1 }, /* latin small letter a with breve and grave */
	{ 0x0103, 0x0301, 0x1EAF }, /* latin small letter a with breve and acute */
	{ 0x0103, 0x0303, 0x1EB5 }, /* latin small letter a with breve and tilde */
	{ 0x0103, 0x0309, 0x1EB3 }, /* latin small letter a with breve and hook above */
	{ 0x0112, 0x0300, 0x1E14 }, /* latin capital letter e with macron and grave */
	{ 0x0112, 0x0301, 0x1E16 }, /* latin capital letter e with macron and acute */
	{ 0x0113, 0x0300, 0x1E15 }, /* latin small letter e with macron and grave */
	{ 0x0113, 0x0301, 0x1E17 }, /* latin small letter e with macron and acute */
	{ 0x014C, 0x0300, 0x1E50 }, /* latin capital letter o with macron and grave */
	{ 0x014C, 0x0301, 0x1E52 }, /* latin capital letter o with macron and acute */
	{ 0x014D, 0x0300, 0x1E51 }, /* latin small letter o with macron and grave */
	{ 0x014D, 0x0301, 0x1E53 }, /* latin small letter o with macron and acute */
	{ 0x015A, 0x0307, 0x1E64 }, /* latin capital letter s with acute and dot above */
	{ 0x015B, 0x0307, 0x1E65 }, /* latin small letter s with acute and dot above */
	{ 0x0160, 0x0307, 0x1E66 }, /* latin capital letter s with caron and dot above */
	{ 0x0161, 0x0307, 0x1E67 }, /* latin small letter s with caron and dot above */
	{ 0x0168, 0x0301, 0x1E78 }, /* latin capital letter u with tilde and acute */
	{ 0x0169, 0x0301, 0x1E79 }, /* latin small letter u with tilde and acute */
	{ 0x016A, 0x0308, 0x1E7A }, /* latin capital letter u with macron and diaeresis */
	{ 0x016B, 0x0308, 0x1E7B }, /* latin small letter u with macron and diaeresis */
	{ 0x017F, 0x0307, 0x1E9B }, /* latin small letter long s with dot above */
	{ 0x01A0, 0x0300, 0x1EDC }, /* latin capital letter o with horn and grave */
	{ 0x01A0, 0x0301, 0x1EDA }, /* latin capital letter o with horn and acute */
	{ 0x01A0, 0x0303, 0x1EE0 }, /* latin capital letter o with horn and tilde */
	{ 0x01A0, 0x0309, 0x1EDE }, /* latin capital letter o with horn and hook above */
	{ 0x01A0, 0x0323, 0x1EE2 }, /* latin capital letter o with horn and dot below */
	{ 0x01A1, 0x0300, 0x1EDD }, /* latin small letter o with horn and grave */
	{ 0x01A1, 0x0301, 0x1EDB }, /* latin small letter o with horn and acute */
	{ 0x01A1, 0x0303, 0x1EE1 }, /* latin small letter o with horn and tilde */
	{ 0x01A1, 0x0309, 0x1EDF }, /* latin small letter o with horn and hook above */
	{ 0x01A1, 0x0323, 0x1EE3 }, /* latin small letter o with horn and dot below */
	{ 0x01AF, 0x0300, 0x1EEA }, /* latin capital letter u with horn and grave */
	{ 0x01AF, 0x0301, 0x1EE8 }, /* latin capital letter u with horn and acute */
	{ 0x01AF, 0x0303, 0x1EEE }, /* latin capital letter u with horn and tilde */
	{ 0x01AF, 0x0309, 0x1EEC }, /* latin capital letter u with horn and hook above */
	{ 0x01AF, 0x0323, 0x1EF0 }, /* latin capital letter u with horn and dot below */
	{ 0x01B0, 0x0300, 0x1EEB }, /* latin small letter u with horn and grave */
	{ 0x01B0, 0x0301, 0x1EE9 }, /* latin small letter u with horn and acute */
	{ 0x01B0, 0x0303, 0x1EEF }, /* latin small letter u with horn and tilde */
	{ 0x01B0, 0x0309, 0x1EED }, /* latin small letter u with horn and hook above */
	{ 0x01B0, 0x0323, 0x1EF1 }, /* latin small letter u with horn and dot below */
	{ 0x01B7, 0x030C, 0x01EE }, /* latin capital letter ezh with caron */
	{ 0x01EA, 0x0304, 0x01EC }, /* latin capital letter o with ogonek and macron */
	{ 0x01EB, 0x0304, 0x01ED }, /* latin small letter o with ogonek and macron */
	{ 0x0226, 0x0304, 0x01E0 }, /* latin capital letter a with dot above and macron */
	{ 0x0227, 0x0304, 0x01E1 }, /* latin small letter a with dot above and macron */
	{ 0x0228, 0x0306, 0x1E1C }, /* latin capital letter e with cedilla and breve */
	{ 0x0229, 0x0306, 0x1E1D }, /* latin small letter e with cedilla and breve */
	{ 0x022E, 0x0304, 0x0230 }, /* latin capital letter o with dot above and macron */
	{ 0x022F, 0x0304, 0x0231 }, /* latin small letter o with dot above and macron */
	{ 0x0292, 0x030C, 0x01EF }, /* latin small letter ezh with caron */
	{ 0x0391, 0x0300, 0x1FBA }, /* greek capital letter alpha with varia */
	{ 0x0391, 0x0301, 0x0386 }, /* greek capital letter alpha with tonos */
	{ 0x0391, 0x0304, 0x1FB9 }, /* greek capital letter alpha with macron */
	{ 0x0391, 0x0306, 0x1FB8 }, /* greek capital letter alpha with vrachy */
	{ 0x0391, 0x0313, 0x1F08 }, /* greek capital letter alpha with psili */
	{ 0x0391, 0x0314, 0x1F09 }, /* greek capital letter alpha with dasia */
	{ 0x0391, 0x0345, 0x1FBC }, /* greek capital letter alpha with prosgegrammeni */
	{ 0x0395, 0x0300, 0x1FC8 }, /* greek capital letter epsilon with varia */
	{ 0x0395, 0x0301, 0x0388 }, /* greek capital letter epsilon with tonos */
	{ 0x0395, 0x0313, 0x1F18 }, /* greek capital letter epsilon with psili */
	{ 0x0395, 0x0314, 0x1F19 }, /* greek capital letter epsilon with dasia */
	{ 0x0397, 0x0300, 0x1FCA }, /* greek capital letter eta with varia */
	{ 0x0397, 0x0301, 0x0389 }, /* greek capital letter eta with tonos */
	{ 0x0397, 0x0313, 0x1F28 }, /* greek capital letter eta with psili */
	{ 0x0397, 0x0314, 0x1F29 }, /* greek capital letter eta with dasia */
	{ 0x0397, 0x0345, 0x1FCC }, /* greek capital letter eta with prosgegrammeni */
	{ 0x0399, 0x0300, 0x1FDA }, /* greek capital letter iota with varia */
	{ 0x0399, 0x0301, 0x038A }, /* greek capital letter iota with tonos */
	{ 0x0399, 0x0304, 0x1FD9 }, /* greek capital letter iota with macron */
	{ 0x0399, 0x0306, 0x1FD8 }, /* greek capital letter iota with vrachy */
	{ 0x0399, 0x0308, 0x03AA }, /* greek capital letter iota with dialytika */
	{ 0x0399, 0x0313, 0x1F38 }, /* greek capital letter iota with psili */
	{ 0x0399, 0x0314, 0x1F39 }, /* greek capital letter iota with dasia */
	{ 0x039F, 0x0300, 0x1FF8 }, /* greek capital letter omicron with varia */
	{ 0x039F, 0x0301, 0x038C }, /* greek capital letter omicron with tonos */
	{ 0x039F, 0x0313, 0x1F48 }, /* greek capital letter omicron with psili */
	{ 0x039F, 0x0314, 0x1F49 }, /* greek capital letter omicron with dasia */
	{ 0x03A1, 0x0314, 0x1FEC }, /* greek capital letter rho with dasia */
	{ 0x03A5, 0x0300, 0x1FEA }, /* greek capital letter upsilon with varia */
	{ 0x03A5, 0x0301, 0x038E }, /* greek capital letter upsilon with tonos */
	{ 0x03A5, 0x0304, 0x1FE9 }, /* greek capital letter upsilon with macron */
	{ 0x03A5, 0x0306, 0x1FE8 }, /* greek capital letter upsilon with vrachy */
	{ 0x03A5, 0x0308, 0x03AB }, /* greek capital letter upsilon with dialytika */
	{ 0x03A5, 0x0314, 0x1F59 }, /* greek capital letter upsilon with dasia */
	{ 0x03A9, 0x0300, 0x1FFA }, /* greek capital letter omega with varia */
	{ 0x03A9, 0x0301, 0x038F }, /* greek capital letter omega with tonos */
	{ 0x03A9, 0x0313, 0x1F68 }, /* greek capital letter omega with psili */
	{ 0x03A9, 0x0314, 0x1F69 }, /* greek capital letter omega with dasia */
	{ 0x03A9, 0x0345, 0x1FFC }, /* greek capital letter omega with prosgegrammeni */
	{ 0x03AC, 0x0345, 0x1FB4 }, /* greek small letter alpha with oxia and ypogegrammeni */
	{ 0x03AE, 0x0345, 0x1FC4 }, /* greek small letter eta with oxia and ypogegrammeni */
	{ 0x03B1, 0x0300, 0x1F70 }, /* greek small letter alpha with varia */
	{ 0x03B1, 0x0301, 0x03AC }, /* greek small letter alpha with tonos */
	{ 0x03B1, 0x0304, 0x1FB1 }, /* greek small letter alpha with macron */
	{ 0x03B1, 0x0306, 0x1FB0 }, /* greek small letter alpha with vrachy */
	{ 0x03B1, 0x0313, 0x1F00 }, /* greek small letter alpha with psili */
	{ 0x03B1, 0x0314, 0x1F01 }, /* greek small letter alpha with dasia */
	{ 0x03B1, 0x0342, 0x1FB6 }, /* greek small letter alpha with perispomeni */
	{ 0x03B1, 0x0345, 0x1FB3 }, /* greek small letter alpha with ypogegrammeni */
	{ 0x03B5, 0x0300, 0x1F72 }, /* greek small letter epsilon with varia */
	{ 0x03B5, 0x0301, 0x03AD }, /* greek small letter epsilon with tonos */
	{ 0x03B5, 0x0313, 0x1F10 }, /* greek small letter epsilon with psili */
	{ 0x03B5, 0x0314, 0x1F11 }, /* greek small letter epsilon with dasia */
	{ 0x03B7, 0x0300, 0x1F74 }, /* greek small letter eta with varia */
	{ 0x03B7, 0x0301, 0x03AE }, /* greek small letter eta with tonos */
	{ 0x03B7, 0x0313, 0x1F20 }, /* greek small letter eta with psili */
	{ 0x03B7, 0x0314, 0x1F21 }, /* greek small letter eta with dasia */
	{ 0x03B7, 0x0342, 0x1FC6 }, /* greek small letter eta with perispomeni */
	{ 0x03B7, 0x0345, 0x1FC3 }, /* greek small letter eta with ypogegrammeni */
	{ 0x03B9, 0x0300, 0x1F76 }, /* greek small letter iota with varia */
	{ 0x03B9, 0x0301, 0x03AF }, /* greek small letter iota with tonos */
	{ 0x03B9, 0x0304, 0x1FD1 }, /* greek small letter iota with macron */
	{ 0x03B9, 0x0306, 0x1FD0 }, /* greek small letter iota with vrachy */
	{ 0x03B9, 0x0308, 0x03CA }, /* greek small letter iota with dialytika */
	{ 0x03B9, 0x0313, 0x1F30 }, /* greek small letter iota with psili */
	{ 0x03B9, 0x0314, 0x1F31 }, /* greek small letter iota with dasia */
	{ 0x03B9, 0x0342, 0x1FD6 }, /* greek small letter iota with perispomeni */
	{ 0x03BF, 0x0300, 0x1F78 }, /* greek small letter omicron with varia */
	{ 0x03BF, 0x0301, 0x03CC }, /* greek small letter omicron with tonos */
	{ 0x03BF, 0x0313, 0x1F40 }, /* greek small letter omicron with psili */
	{ 0x03BF, 0x0314, 0x1F41 }, /* greek small letter omicron with dasia */
	{ 0x03C1, 0x0313, 0x1FE4 }, /* greek small letter rho with psili */
	{ 0x03C1, 0x0314, 0x1FE5 }, /* greek small letter rho with dasia */
	{ 0x03C5, 0x0300, 0x1F7A }, /* greek small letter upsilon with varia */
	{ 0x03C5, 0x0301, 0x03CD }, /* greek small letter upsilon with tonos */
	{ 0x03C5, 0x0304, 0x1FE1 }, /* greek small letter upsilon with macron */
	{ 0x03C5, 0x0306, 0x1FE0 }, /* greek small letter upsilon with vrachy */
	{ 0x03C5, 0x0308, 0x03CB }, /* greek small letter upsilon with dialytika */
	{ 0x03C5, 0x0313, 0x1F50 }, /* greek small letter upsilon with psili */
	{ 0x03C5, 0x0314, 0x1F51 }, /* greek small letter upsilon with dasia */
	{ 0x03C5, 0x0342, 0x1FE6 }, /* greek small letter upsilon with perispomeni */
	{ 0x03C9, 0x0300, 0x1F7C }, /* greek small letter omega with varia */
	{ 0x03C9, 0x0301, 0x03CE }, /* greek small letter omega with tonos */
	{ 0x03C9, 0x0313, 0x1F60 }, /* greek small letter omega with psili */
	{ 0x03C9, 0x0314, 0x1F61 }, /* greek small letter omega with dasia */
	{ 0x03C9, 0x0342, 0x1FF6 }, /* greek small letter omega with perispomeni */
	{ 0x03C9, 0x0345, 0x1FF3 }, /* greek small letter omega with ypogegrammeni */
	{ 0x03CA, 0x0300, 0x1FD2 }, /* greek small letter iota with dialytika and varia */
	{ 0x03CA, 0x0301, 0x0390 }, /* greek small letter iota with dialytika and tonos */
	{ 0x03CA, 0x0342, 0x1FD7 }, /* greek small letter iota with dialytika and perispomeni */
	{ 0x03CB, 0x0300, 0x1FE2 }, /* greek small letter upsilon with dialytika and varia */
	{ 0x03CB, 0x0301, 0x03B0 }, /* greek small letter upsilon with dialytika and tonos */
	{ 0x03CB, 0x0342, 0x1FE7 }, /* greek small letter upsilon with dialytika and perispomeni */
	{ 0x03CE, 0x0345, 0x1FF4 }, /* greek small letter omega with oxia and ypogegrammeni */
	{ 0x03D2, 0x0301, 0x03D3 }, /* greek upsilon with acute and hook symbol */
	{ 0x03D2, 0x0308, 0x03D4 }, /* greek upsilon with diaeresis and hook symbol */
	{ 0x0406, 0x0308, 0x0407 }, /* cyrillic capital letter yi */
	{ 0x0410, 0x0306, 0x04D0 }, /* cyrillic capital letter a with breve */
	{ 0x0410, 0x0308, 0x04D2 }, /* cyrillic capital letter a with diaeresis */
	{ 0x0413, 0x0301, 0x0403 }, /* cyrillic capital letter gje */
	{ 0x0415, 0x0300, 0x0400 }, /* cyrillic capital letter ie with grave */
	{ 0x0415, 0x0306, 0x04D6 }, /* cyrillic capital letter ie with breve */
	{ 0x0415, 0x0308, 0x0401 }, /* cyrillic capital letter io */
	{ 0x0416, 0x0306, 0x04C1 }, /* cyrillic capital letter zhe with breve */
	{ 0x0416, 0x0308, 0x04DC }, /* cyrillic capital letter zhe with diaeresis */
	{ 0x0417, 0x0308, 0x04DE }, /* cyrillic capital letter ze with diaeresis */
	{ 0x0418, 0x0300, 0x040D }, /* cyrillic capital letter i with grave */
	{ 0x0418, 0x0304, 0x04E2 }, /* cyrillic capital letter i with macron */
	{ 0x0418, 0x0306, 0x0419 }, /* cyrillic capital letter short i */
	{ 0x0418, 0x0308, 0x04E4 }, /* cyrillic capital letter i with diaeresis */
	{ 0x041A, 0x0301, 0x040C }, /* cyrillic capital letter kje */
	{ 0x041E, 0x0308, 0x04E6 }, /* cyrillic capital letter o with diaeresis */
	{ 0x0423, 0x0304, 0x04EE }, /* cyrillic capital letter u with macron */
	{ 0x0423, 0x0306, 0x040E }, /* cyrillic capital letter short u */
	{ 0x0423, 0x0308, 0x04F0 }, /* cyrillic capital letter u with diaeresis */
	{ 0x0423, 0x030B, 0x04F2 }, /* cyrillic capital letter u with double acute */
	{ 0x0427, 0x0308, 0x04F4 }, /* cyrillic capital letter che with diaeresis */
	{ 0x042B, 0x0308, 0x04F8 }, /* cyrillic capital letter yeru with diaeresis */
	{ 0x042D, 0x0308, 0x04EC }, /* cyrillic capital letter e with diaeresis */
	{ 0x0430, 0x0306, 0x04D1 }, /* cyrillic small letter a with breve */
	{ 0x0430, 0x0308, 0x04D3 }, /* cyrillic small letter a with diaeresis */
	{ 0x0433, 0x0301, 0x0453 }, /* cyrillic small letter gje */
	{ 0x0435, 0x0300, 0x0450 }, /* cyrillic small letter ie with grave */
	{ 0x0435, 0x0306, 0x04D7 }, /* cyrillic small letter ie with breve */
	{ 0x0435, 0x0308, 0x0451 }, /* cyrillic small letter io */
	{ 0x0436, 0x0306, 0x04C2 }, /* cyrillic small letter zhe with breve */
	{ 0x0436, 0x0308, 0x04DD }, /* cyrillic small letter zhe with diaeresis */
	{ 0x0437, 0x0308, 0x04DF }, /* cyrillic small letter ze with diaeresis */
	{ 0x0438, 0x0300, 0x045D }, /* cyrillic small letter i with grave */
	{ 0x0438, 0x0304, 0x04E3 }, /* cyrillic small letter i with macron */
	{ 0x0438, 0x0306, 0x0439 }, /* cyrillic small letter short i */
	{ 0x0438, 0x0308, 0x04E5 }, /* cyrillic small letter i with diaeresis */
	{ 0x043A, 0x0301, 0x045C }, /* cyrillic small letter kje */
	{ 0x043E, 0x0308, 0x04E7 }, /* cyrillic small letter o with diaeresis */
	{ 0x0443, 0x0304, 0x04EF }, /* cyrillic small letter u with macron */
	{ 0x0443, 0x0306, 0x045E }, /* cyrillic small letter short u */
	{ 0x0443, 0x0308, 0x04F1 }, /* cyrillic small letter u with diaeresis */
	{ 0x0443, 0x030B, 0x04F3 }, /* cyrillic small letter u with double acute */
	{ 0x0447, 0x0308, 0x04F5 }, /* cyrillic small letter che with diaeresis */
	{ 0x044B, 0x0308, 0x04F9 }, /* cyrillic small letter yeru with diaeresis */
	{ 0x044D, 0x0308, 0x04ED }, /* cyrillic small letter e with diaeresis */
	{ 0x0456, 0x0308, 0x0457 }, /* cyrillic small letter yi */
	{ 0x0474, 0x030F, 0x0476 }, /* cyrillic capital letter izhitsa with double grave accent */
	{ 0x0475, 0x030F, 0x0477 }, /* cyrillic small letter izhitsa with double grave accent */
	{ 0x04D8, 0x0308, 0x04DA }, /* cyrillic capital letter schwa with diaeresis */
	{ 0x04D9, 0x0308, 0x04DB }, /* cyrillic small letter schwa with diaeresis */
	{ 0x04E8, 0x0308, 0x04EA }, /* cyrillic capital letter barred o with diaeresis */
	{ 0x04E9, 0x0308, 0x04EB }, /* cyrillic small letter barred o with diaeresis */
	{ 0x0627, 0x0653, 0x0622 }, /* arabic letter alef with madda above */
	{ 0x0627, 0x0654, 0x0623 }, /* arabic letter alef with hamza above */
	{ 0x0627, 0x0655, 0x0625 }, /* arabic letter alef with hamza below */
	{ 0x0648, 0x0654, 0x0624 }, /* arabic letter waw with hamza above */
	{ 0x064A, 0x0654, 0x0626 }, /* arabic letter yeh with hamza above */
	{ 0x06C1, 0x0654, 0x06C2 }, /* arabic letter heh goal with hamza above */
	{ 0x06D2, 0x0654, 0x06D3 }, /* arabic letter yeh barree with hamza above */
	{ 0x06D5, 0x0654, 0x06C0 }, /* arabic letter heh with yeh above */
	{ 0x0928, 0x093C, 0x0929 }, /* devanagari letter nnna */
	{ 0x0930, 0x093C, 0x0931 }, /* devanagari letter rra */
	{ 0x0933, 0x093C, 0x0934 }, /* devanagari letter llla */
	{ 0x09C7, 0x09BE, 0x09CB }, /* bengali vowel sign o */
	{ 0x09C7, 0x09D7, 0x09CC }, /* bengali vowel sign au */
	{ 0x0B47, 0x0B3E, 0x0B4B }, /* oriya vowel sign o */
	{ 0x0B47, 0x0B56, 0x0B48 }, /* oriya vowel sign ai */
	{ 0x0B47, 0x0B57, 0x0B4C }, /* oriya vowel sign au */
	{ 0x0B92, 0x0BD7, 0x0B94 }, /* tamil letter au */
	{ 0x0BC6, 0x0BBE, 0x0BCA }, /* tamil vowel sign o */
	{ 0x0BC6, 0x0BD7, 0x0BCC }, /* tamil vowel sign au */
	{ 0x0BC7, 0x0BBE, 0x0BCB }, /* tamil vowel sign oo */
	{ 0x0C46, 0x0C56, 0x0C48 }, /* telugu vowel sign ai */
	{ 0x0CBF, 0x0CD5, 0x0CC0 }, /* kannada vowel sign ii */
	{ 0x0CC6, 0x0CC2, 0x0CCA }, /* kannada vowel sign o */
	{ 0x0CC6, 0x0CD5, 0x0CC7 }, /* kannada vowel sign ee */
	{ 0x0CC6, 0x0CD6, 0x0CC8 }, /* kannada vowel sign ai */
	{ 0x0CCA, 0x0CD5, 0x0CCB }, /* kannada vowel sign oo */
	{ 0x0D46, 0x0D3E, 0x0D4A }, /* malayalam vowel sign o */
	{ 0x0D46, 0x0D57, 0x0D4C }, /* malayalam vowel sign au */
	{ 0x0D47, 0x0D3E, 0x0D4B }, /* malayalam vowel sign oo */
	{ 0x0DD9, 0x0DCA, 0x0DDA }, /* sinhala vowel sign diga kombuva */
	{ 0x0DD9, 0x0DCF, 0x0DDC }, /* sinhala vowel sign kombuva haa aela-pilla */
	{ 0x0DD9, 0x0DDF, 0x0DDE }, /* sinhala vowel sign kombuva haa gayanukitta */
	{ 0x0DDC, 0x0DCA, 0x0DDD }, /* sinhala vowel sign kombuva haa diga aela-pilla */
	{ 0x1025, 0x102E, 0x1026 }, /* myanmar letter uu */
	{ 0x1B05, 0x1B35, 0x1B06 }, /* balinese letter akara tedung */
	{ 0x1B07, 0x1B35, 0x1B08 }, /* balinese letter ikara tedung */
	{ 0x1B09, 0x1B35, 0x1B0A }, /* balinese letter ukara tedung */
	{ 0x1B0B, 0x1B35, 0x1B0C }, /* balinese letter ra repa tedung */
	{ 0x1B0D, 0x1B35, 0x1B0E }, /* balinese letter la lenga tedung */
	{ 0x1B11, 0x1B35, 0x1B12 }, /* balinese letter okara tedung */
	{ 0x1B3A, 0x1B35, 0x1B3B }, /* balinese vowel sign ra repa tedung */
	{ 0x1B3C, 0x1B35, 0x1B3D }, /* balinese vowel sign la lenga tedung */
	{ 0x1B3E, 0x1B35, 0x1B40 }, /* balinese vowel sign taling tedung */
	{ 0x1B3F, 0x1B35, 0x1B41 }, /* balinese vowel sign taling repa tedung */
	{ 0x1B42, 0x1B35, 0x1B43 }, /* balinese vowel sign pepet tedung */
	{ 0x1E36, 0x0304, 0x1E38 }, /* latin capital letter l with dot below and macron */
	{ 0x1E37, 0x0304, 0x1E39 }, /* latin small letter l with dot below and macron */
	{ 0x1E5A, 0x0304, 0x1E5C }, /* latin capital letter r with dot below and macron */
	{ 0x1E5B, 0x0304, 0x1E5D }, /* latin small letter r with dot below and macron */
	{ 0x1E62, 0x0307, 0x1E68 }, /* latin capital letter s with dot below and dot above */
	{ 0x1E63, 0x0307, 0x1E69 }, /* latin small letter s with dot below and dot above */
	{ 0x1EA0, 0x0302, 0x1EAC }, /* latin capital letter a with circumflex and dot below */
	{ 0x1EA0, 0x0306, 0x1EB6 }, /* latin capital letter a with breve and dot below */
	{ 0x1EA1, 0x0302, 0x1EAD }, /* latin small letter a with circumflex and dot below */
	{ 0x1EA1, 0x0306, 0x1EB7 }, /* latin small letter a with breve and dot below */
	{ 0x1EB8, 0x0302, 0x1EC6 }, /* latin capital letter e with circumflex and dot below */
	{ 0x1EB9, 0x0302, 0x1EC7 }, /* latin small letter e with circumflex and dot below */
	{ 0x1ECC, 0x0302, 0x1ED8 }, /* latin capital letter o with circumflex and dot below */
	{ 0x1ECD, 0x0302, 0x1ED9 }, /* latin small letter o with circumflex and dot below */
	{ 0x1F00, 0x0300, 0x1F02 }, /* greek small letter alpha with psili and varia */
	{ 0x1F00, 0x0301, 0x1F04 }, /* greek small letter alpha with psili and oxia */
	{ 0x1F00, 0x0342, 0x1F06 }, /* greek small letter alpha with psili and perispomeni */
	{ 0x1F00, 0x0345, 0x1F80 }, /* greek small letter alpha with psili and ypogegrammeni */
	{ 0x1F01, 0x0300, 0x1F03 }, /* greek small letter alpha with dasia and varia */
	{ 0x1F01, 0x0301, 0x1F05 }, /* greek small letter alpha with dasia and oxia */
	{ 0x1F01, 0x0342, 0x1F07 }, /* greek small letter alpha with dasia and perispomeni */
	{ 0x1F01, 0x0345, 0x1F81 }, /* greek small letter alpha with dasia and ypogegrammeni */
	{ 0x1F02, 0x0345, 0x1F82 }, /* greek small letter alpha with psili and varia and ypogegrammeni */
	{ 0x1F03, 0x0345, 0x1F83 }, /* greek small letter alpha with dasia and varia and ypogegrammeni */
	{ 0x1F04, 0x0345, 0x1F84 }, /* greek small letter alpha with psili and oxia and ypogegrammeni */
	{ 0x1F05, 0x0345, 0x1F85 }, /* greek small letter alpha with dasia and oxia and ypogegrammeni */
	{ 0x1F06, 0x0345, 0x1F86 }, /* greek small letter alpha with psili and perispomeni and ypogegrammeni */
	{ 0x1F07, 0x0345, 0x1F87 }, /* greek small letter alpha with dasia and perispomeni and ypogegrammeni */
	{ 0x1F08, 0x0300, 0x1F0A }, /* greek capital letter alpha with psili and varia */
	{ 0x1F08, 0x0301, 0x1F0C }, /* greek capital letter alpha with psili and oxia */
	{ 0x1F08, 0x0342, 0x1F0E }, /* greek capital letter alpha with psili and perispomeni */
	{ 0x1F08, 0x0345, 0x1F88 }, /* greek capital letter alpha with psili and prosgegrammeni */
	{ 0x1F09, 0x0300, 0x1F0B }, /* greek capital letter alpha with dasia and varia */
	{ 0x1F09, 0x0301, 0x1F0D }, /* greek capital letter alpha with dasia and oxia */
	{ 0x1F09, 0x0342, 0x1F0F }, /* greek capital letter alpha with dasia and perispomeni */
	{ 0x1F09, 0x0345, 0x1F89 }, /* greek capital letter alpha with dasia and prosgegrammeni */
	{ 0x1F0A, 0x0345, 0x1F8A }, /* greek capital letter alpha with psili and varia and prosgegrammeni */
	{ 0x1F0B, 0x0345, 0x1F8B }, /* greek capital letter alpha with dasia and varia and prosgegrammeni */
	{ 0x1F0C, 0x0345, 0x1F8C }, /* greek capital letter alpha with psili and oxia and prosgegrammeni */
	{ 0x1F0D, 0x0345, 0x1F8D }, /* greek capital letter alpha with dasia and oxia and prosgegrammeni */
	{ 0x1F0E, 0x0345, 0x1F8E }, /* greek capital letter alpha with psili and perispomeni and prosgegrammeni */
	{ 0x1F0F, 0x0345, 0x1F8F }, /* greek capital letter alpha with dasia and perispomeni and prosgegrammeni */
	{ 0x1F10, 0x0300, 0x1F12 }, /* greek small letter epsilon with psili and varia */
	{ 0x1F10, 0x0301, 0x1F14 }, /* greek small letter epsilon with psili and oxia */
	{ 0x1F11, 0x0300, 0x1F13 }, /* greek small letter epsilon with dasia and varia */
	{ 0x1F11, 0x0301, 0x1F15 }, /* greek small letter epsilon with dasia and oxia */
	{ 0x1F18, 0x0300, 0x1F1A }, /* greek capital letter epsilon with psili and varia */
	{ 0x1F18, 0x0301, 0x1F1C }, /* greek capital letter epsilon with psili and oxia */
	{ 0x1F19, 0x0300, 0x1F1B }, /* greek capital letter epsilon with dasia and varia */
	{ 0x1F19, 0x0301, 0x1F1D }, /* greek capital letter epsilon with dasia and oxia */
	{ 0x1F20, 0x0300, 0x1F22 }, /* greek small letter eta with psili and varia */
	{ 0x1F20, 0x0301, 0x1F24 }, /* greek small letter eta with psili and oxia */
	{ 0x1F20, 0x0342, 0x1F26 }, /* greek small letter eta with psili and perispomeni */
	{ 0x1F20, 0x0345, 0x1F90 }, /* greek small letter eta with psili and ypogegrammeni */
	{ 0x1F21, 0x0300, 0x1F23 }, /* greek small letter eta with dasia and varia */
	{ 0x1F21, 0x0301, 0x1F25 }, /* greek small letter eta with dasia and oxia */
	{ 0x1F21, 0x0342, 0x1F27 }, /* greek small letter eta with dasia and perispomeni */
	{ 0x1F21, 0x0345, 0x1F91 }, /* greek small letter eta with dasia and ypogegrammeni */
	{ 0x1F22, 0x0345, 0x1F92 }, /* greek small letter eta with psili and varia and ypogegrammeni */
	{ 0x1F23, 0x0345, 0x1F93 }, /* greek small letter eta with dasia and varia and ypogegrammeni */
	{ 0x1F24, 0x0345, 0x1F94 }, /* greek small letter eta with psili and oxia and ypogegrammeni */
	{ 0x1F25, 0x0345, 0x1F95 }, /* greek small letter eta with dasia and oxia and ypogegrammeni */
	{ 0x1F26, 0x0345, 0x1F96 }, /* greek small letter eta with psili and perispomeni and ypogegrammeni */
	{ 0x1F27, 0x0345, 0x1F97 }, /* greek small letter eta with dasia and perispomeni and ypogegrammeni */
	{ 0x1F28, 0x0300, 0x1F2A }, /* greek capital letter eta with psili and varia */
	{ 0x1F28, 0x0301, 0x1F2C }, /* greek capital letter eta with psili and oxia */
	{ 0x1F28, 0x0342, 0x1F2E }, /* greek capital letter eta with psili and perispomeni */
	{ 0x1F28, 0x0345, 0x1F98 }, /* greek capital letter eta with psili and prosgegrammeni */
	{ 0x1F29, 0x0300, 0x1F2B }, /* greek capital letter eta with dasia and varia */
	{ 0x1F29, 0x0301, 0x1F2D }, /* greek capital letter eta with dasia and oxia */
	{ 0x1F29, 0x0342, 0x1F2F }, /* greek capital letter eta with dasia and perispomeni */
	{ 0x1F29, 0x0345, 0x1F99 }, /* greek capital letter eta with dasia and prosgegrammeni */
	{ 0x1F2A, 0x0345, 0x1F9A }, /* greek capital letter eta with psili and varia and prosgegrammeni */
	{ 0x1F2B, 0x0345, 0x1F9B }, /* greek capital letter eta with dasia and varia and prosgegrammeni */
	{ 0x1F2C, 0x0345, 0x1F9C }, /* greek capital letter eta with psili and oxia and prosgegrammeni */
	{ 0x1F2D, 0x0345, 0x1F9D }, /* greek capital letter eta with dasia and oxia and prosgegrammeni */
	{ 0x1F2E, 0x0345, 0x1F9E }, /* greek capital letter eta with psili and perispomeni and prosgegrammeni */
	{ 0x1F2F, 0x0345, 0x1F9F }, /* greek capital letter eta with dasia and perispomeni and prosgegrammeni */
	{ 0x1F30, 0x0300, 0x1F32 }, /* greek small letter iota with psili and varia */
	{ 0x1F30, 0x0301, 0x1F34 }, /* greek small letter iota with psili and oxia */
	{ 0x1F30, 0x0342, 0x1F36 }, /* greek small letter iota with psili and perispomeni */
	{ 0x1F31, 0x0300, 0x1F33 }, /* greek small letter iota with dasia and varia */
	{ 0x1F31, 0x0301, 0x1F35 }, /* greek small letter iota with dasia and oxia */
	{ 0x1F31, 0x0342, 0x1F37 }, /* greek small letter iota with dasia and perispomeni */
	{ 0x1F38, 0x0300, 0x1F3A }, /* greek capital letter iota with psili and varia */
	{ 0x1F38, 0x0301, 0x1F3C }, /* greek capital letter iota with psili and oxia */
	{ 0x1F38, 0x0342, 0x1F3E }, /* greek capital letter iota with psili and perispomeni */
	{ 0x1F39, 0x0300, 0x1F3B }, /* greek capital letter iota with dasia and varia */
	{ 0x1F39, 0x0301, 0x1F3D }, /* greek capital letter iota with dasia and oxia */
	{ 0x1F39, 0x0342, 0x1F3F }, /* greek capital letter iota with dasia and perispomeni */
	{ 0x1F40, 0x0300, 0x1F42 }, /* greek small letter omicron with psili and varia */
	{ 0x1F40, 0x0301, 0x1F44 }, /* greek small letter omicron with psili and oxia */
	{ 0x1F41, 0x0300, 0x1F43 }, /* greek small letter omicron with dasia and varia */
	{ 0x1F41, 0x0301, 0x1F45 }, /* greek small letter omicron with dasia and oxia */
	{ 0x1F48, 0x0300, 0x1F4A }, /* greek capital letter omicron with psili and varia */
	{ 0x1F48, 0x0301, 0x1F4C }, /* greek capital letter omicron with psili and oxia */
	{ 0x1F49, 0x0300, 0x1F4B }, /* greek capital letter omicron with dasia and varia */
	{ 0x1F49, 0x0301, 0x1F4D }, /* greek capital letter omicron with dasia and oxia */
	{ 0x1F50, 0x0300, 0x1F52 }, /* greek small letter upsilon with psili and varia */
	{ 0x1F50, 0x0301, 0x1F54 }, /* greek small letter upsilon with psili and oxia */
	{ 0x1F50, 0x0342, 0x1F56 }, /* greek small letter upsilon with psili and perispomeni */
	{ 0x1F51, 0x0300, 0x1F53 }, /* greek small letter upsilon with dasia and varia */
	{ 0x1F51, 0x0301, 0x1F55 }, /* greek small letter upsilon with dasia and oxia */
	{ 0x1F51, 0x0342, 0x1F57 }, /* greek small letter upsilon with dasia and perispomeni */
	{ 0x1F59, 0x0300, 0x1F5B }, /* greek capital letter upsilon with dasia and varia */
	{ 0x1F59, 0x0301, 0x1F5D }, /* greek capital letter upsilon with dasia and oxia */
	{ 0x1F59, 0x0342, 0x1F5F }, /* greek capital letter upsilon with dasia and perispomeni */
	{ 0x1F60, 0x0300, 0x1F62 }, /* greek small letter omega with psili and varia */
	{ 0x1F60, 0x0301, 0x1F64 }, /* greek small letter omega with psili and oxia */
	{ 0x1F60, 0x0342, 0x1F66 }, /* greek small letter omega with psili and perispomeni */
	{ 0x1F60, 0x0345, 0x1FA0 }, /* greek small letter omega with psili and ypogegrammeni */
	{ 0x1F61, 0x0300, 0x1F63 }, /* greek small letter omega with dasia and varia */
	{ 0x1F61, 0x0301, 0x1F65 }, /* greek small letter omega with dasia and oxia */
	{ 0x1F61, 0x0342, 0x1F67 }, /* greek small letter omega with dasia and perispomeni */
	{ 0x1F61, 0x0345, 0x1FA1 }, /* greek small letter omega with dasia and ypogegrammeni */
	{ 0x1F62, 0x0345, 0x1FA2 }, /* greek small letter omega with psili and varia and ypogegrammeni */
	{ 0x1F63, 0x0345, 0x1FA3 }, /* greek small letter omega with dasia and varia and ypogegrammeni */
	{ 0x1F64, 0x0345, 0x1FA4 }, /* greek small letter omega with psili and oxia and ypogegrammeni */
	{ 0x1F65, 0x0345, 0x1FA5 }, /* greek small letter omega with dasia and oxia and ypogegrammeni */
	{ 0x1F66, 0x0345, 0x1FA6 }, /* greek small letter omega with psili and perispomeni and ypogegrammeni */
	{ 0x1F67, 0x0345, 0x1FA7 }, /* greek small letter omega with dasia and perispomeni and ypogegrammeni */
	{ 0x1F68, 0x0300, 0x1F6A }, /* greek capital letter omega with psili and varia */
	{ 0x1F68, 0x0301, 0x1F6C }, /* greek capital letter omega with psili and oxia */
	{ 0x1F68, 0x0342, 0x1F6E }, /* greek capital letter omega with psili and perispomeni */
	{ 0x1F68, 0x0345, 0x1FA8 }, /* greek capital letter omega with psili and prosgegrammeni */
	{ 0x1F69, 0x0300, 0x1F6B }, /* greek capital letter omega with dasia and varia */
	{ 0x1F69, 0x0301, 0x1F6D }, /* greek capital letter omega with dasia and oxia */
	{ 0x1F69, 0x0342, 0x1F6F }, /* greek capital letter omega with dasia and perispomeni */
	{ 0x1F69, 0x0345, 0x1FA9 }, /* greek capital letter omega with dasia and prosgegrammeni */
	{ 0x1F6A, 0x0345, 0x1FAA }, /* greek capital letter omega with psili and varia and prosgegrammeni */
	{ 0x1F6B, 0x0345, 0x1FAB }, /* greek capital letter omega with dasia and varia and prosgegrammeni */
	{ 0x1F6C, 0x0345, 0x1FAC }, /* greek capital letter omega with psili and oxia and prosgegrammeni */
	{ 0x1F6D, 0x0345, 0x1FAD }, /* greek capital letter omega with dasia and oxia and prosgegrammeni */
	{ 0x1F6E, 0x0345, 0x1FAE }, /* greek capital letter omega with psili and perispomeni and prosgegrammeni */
	{ 0x1F6F, 0x0345, 0x1FAF }, /* greek capital letter omega with dasia and perispomeni and prosgegrammeni */
	{ 0x1F70, 0x0345, 0x1FB2 }, /* greek small letter alpha with varia and ypogegrammeni */
	{ 0x1F74, 0x0345, 0x1FC2 }, /* greek small letter eta with varia and ypogegrammeni */
	{ 0x1F7C, 0x0345, 0x1FF2 }, /* greek small letter omega with varia and ypogegrammeni */
	{ 0x1FB6, 0x0345, 0x1FB7 }, /* greek small letter alpha with perispomeni and ypogegrammeni */
	{ 0x1FBF, 0x0300, 0x1FCD }, /* greek psili and varia */
	{ 0x1FBF, 0x0301, 0x1FCE }, /* greek psili and oxia */
	{ 0x1FBF, 0x0342, 0x1FCF }, /* greek psili and perispomeni */
	{ 0x1FC6, 0x0345, 0x1FC7 }, /* greek small letter eta with perispomeni and ypogegrammeni */
	{ 0x1FF6, 0x0345, 0x1FF7 }, /* greek small letter omega with perispomeni and ypogegrammeni */
	{ 0x1FFE, 0x0300, 0x1FDD }, /* greek dasia and varia */
	{ 0x1FFE, 0x0301, 0x1FDE }, /* greek dasia and oxia */
	{ 0x1FFE, 0x0342, 0x1FDF }, /* greek dasia and perispomeni */
	{ 0x2190, 0x0338, 0x219A }, /* leftwards arrow with stroke */
	{ 0x2192, 0x0338, 0x219B }, /* rightwards arrow with stroke */
	{ 0x2194, 0x0338, 0x21AE }, /* left right arrow with stroke */
	{ 0x21D0, 0x0338, 0x21CD }, /* leftwards double arrow with stroke */
	{ 0x21D2, 0x0338, 0x21CF }, /* rightwards double arrow with stroke */
	{ 0x21D4, 0x0338, 0x21CE }, /* left right double arrow with stroke */
	{ 0x2203, 0x0338, 0x2204 }, /* there does not exist */
	{ 0x2208, 0x0338, 0x2209 }, /* not an element of */
	{ 0x220B, 0x0338, 0x220C }, /* does not contain as member */
	{ 0x2223, 0x0338, 0x2224 }, /* does not divide */
	{ 0x2225, 0x0338, 0x2226 }, /* not parallel to */
	{ 0x223C, 0x0338, 0x2241 }, /* not tilde */
	{ 0x2243, 0x0338, 0x2244 }, /* not asymptotically equal to */
	{ 0x2245, 0x0338, 0x2247 }, /* neither approximately nor actually equal to */
	{ 0x2248, 0x0338, 0x2249 }, /* not almost equal to */
	{ 0x224D, 0x0338, 0x226D }, /* not equivalent to */
	{ 0x2261, 0x0338, 0x2262 }, /* not identical to */
	{ 0x2264, 0x0338, 0x2270 }, /* neither less-than nor equal to */
	{ 0x2265, 0x0338, 0x2271 }, /* neither greater-than nor equal to */
	{ 0x2272, 0x0338, 0x2274 }, /* neither less-than nor equivalent to */
	{ 0x2273, 0x0338, 0x2275 }, /* neither greater-than nor equivalent to */
	{ 0x2276, 0x0338, 0x2278 }, /* neither less-than nor greater-than */
	{ 0x2277, 0x0338, 0x2279 }, /* neither greater-than nor less-than */
	{ 0x227A, 0x0338, 0x2280 }, /* does not precede */
	{ 0x227B, 0x0338, 0x2281 }, /* does not succeed */
	{ 0x227C, 0x0338, 0x22E0 }, /* does not precede or equal */
	{ 0x227D, 0x0338, 0x22E1 }, /* does not succeed or equal */
	{ 0x2282, 0x0338, 0x2284 }, /* not a subset of */
	{ 0x2283, 0x0338, 0x2285 }, /* not a superset of */
	{ 0x2286, 0x0338, 0x2288 }, /* neither a subset of nor equal to */
	{ 0x2287, 0x0338, 0x2289 }, /* neither a superset of nor equal to */
	{ 0x2291, 0x0338, 0x22E2 }, /* not square image of or equal to */
	{ 0x2292, 0x0338, 0x22E3 }, /* not square original of or equal to */
	{ 0x22A2, 0x0338, 0x22AC }, /* does not prove */
	{ 0x22A8, 0x0338, 0x22AD }, /* not true */
	{ 0x22A9, 0x0338, 0x22AE }, /* does not force */
	{ 0x22AB, 0x0338, 0x22AF }, /* negated double vertical bar double right turnstile */
	{ 0x22B2, 0x0338, 0x22EA }, /* not normal subgroup of */
	{ 0x22B3, 0x0338, 0x22EB }, /* does not contain as normal subgroup */
	{ 0x22B4, 0x0338, 0x22EC }, /* not normal subgroup of or equal to */
	{ 0x22B5, 0x0338, 0x22ED }, /* does not contain as normal subgroup or equal */
	{ 0x3046, 0x3099, 0x3094 }, /* hiragana letter vu */
	{ 0x304B, 0x3099, 0x304C }, /* hiragana letter ga */
	{ 0x304D, 0x3099, 0x304E }, /* hiragana letter gi */
	{ 0x304F, 0x3099, 0x3050 }, /* hiragana letter gu */
	{ 0x3051, 0x3099, 0x3052 }, /* hiragana letter ge */
	{ 0x3053, 0x3099, 0x3054 }, /* hiragana letter go */
	{ 0x3055, 0x3099, 0x3056 }, /* hiragana letter za */
	{ 0x3057, 0x3099, 0x3058 }, /* hiragana letter zi */
	{ 0x3059, 0x3099, 0x305A }, /* hiragana letter zu */
	{ 0x305B, 0x3099, 0x305C }, /* hiragana letter ze */
	{ 0x305D, 0x3099, 0x305E }, /* hiragana letter zo */
	{ 0x305F, 0x3099, 0x3060 }, /* hiragana letter da */
	{ 0x3061, 0x3099, 0x3062 }, /* hiragana letter di */
	{ 0x3064, 0x3099, 0x3065 }, /* hiragana letter du */
	{ 0x3066, 0x3099, 0x3067 }, /* hiragana letter de */
	{ 0x3068, 0x3099, 0x3069 }, /* hiragana letter do */
	{ 0x306F, 0x3099, 0x3070 }, /* hiragana letter ba */
	{ 0x306F, 0x309A, 0x3071 }, /* hiragana letter pa */
	{ 0x3072, 0x3099, 0x3073 }, /* hiragana letter bi */
	{ 0x3072, 0x309A, 0x3074 }, /* hiragana letter pi */
	{ 0x3075, 0x3099, 0x3076 }, /* hiragana letter bu */
	{ 0x3075, 0x309A, 0x3077 }, /* hiragana letter pu */
	{ 0x3078, 0x3099, 0x3079 }, /* hiragana letter be */
	{ 0x3078, 0x309A, 0x307A }, /* hiragana letter pe */
	{ 0x307B, 0x3099, 0x307C }, /* hiragana letter bo */
	{ 0x307B, 0x309A, 0x307D }, /* hiragana letter po */
	{ 0x309D, 0x3099, 0x309E }, /* hiragana voiced iteration mark */
	{ 0x30A6, 0x3099, 0x30F4 }, /* katakana letter vu */
	{ 0x30AB, 0x3099, 0x30AC }, /* katakana letter ga */
	{ 0x30AD, 0x3099, 0x30AE }, /* katakana letter gi */
	{ 0x30AF, 0x3099, 0x30B0 }, /* katakana letter gu */
	{ 0x30B1, 0x3099, 0x30B2 }, /* katakana letter ge */
	{ 0x30B3, 0x3099, 0x30B4 }, /* katakana letter go */
	{ 0x30B5, 0x3099, 0x30B6 }, /* katakana letter za */
	{ 0x30B7, 0x3099, 0x30B8 }, /* katakana letter zi */
	{ 0x30B9, 0x3099, 0x30BA }, /* katakana letter zu */
	{ 0x30BB, 0x3099, 0x30BC }, /* katakana letter ze */
	{ 0x30BD, 0x3099, 0x30BE }, /* katakana letter zo */
	{ 0x30BF, 0x3099, 0x30C0 }, /* katakana letter da */
	{ 0x30C1, 0x3099, 0x30C2 }, /* katakana letter di */
	{ 0x30C4, 0x3099, 0x30C5 }, /* katakana letter du */
	{ 0x30C6, 0x3099, 0x30C7 }, /* katakana letter de */
	{ 0x30C8, 0x3099, 0x30C9 }, /* katakana letter do */
	{ 0x30CF, 0x3099, 0x30D0 }, /* katakana letter ba */
	{ 0x30CF, 0x309A, 0x30D1 }, /* katakana letter pa */
	{ 0x30D2, 0x3099, 0x30D3 }, /* katakana letter bi */
	{ 0x30D2, 0x309A, 0x30D4 }, /* katakana letter pi */
	{ 0x30D5, 0x3099, 0x30D6 }, /* katakana letter bu */
	{ 0x30D5, 0x309A, 0x30D7 }, /* katakana letter pu */
	{ 0x30D8, 0x3099, 0x30D9 }, /* katakana letter be */
	{ 0x30D8, 0x309A, 0x30DA }, /* katakana letter pe */
	{ 0x30DB, 0x3099, 0x30DC }, /* katakana letter bo */
	{ 0x30DB, 0x309A, 0x30DD }, /* katakana letter po */
	{ 0x30EF, 0x3099, 0x30F7 }, /* katakana letter va */
	{ 0x30F0, 0x3099, 0x30F8 }, /* katakana letter vi */
	{ 0x30F1, 0x3099, 0x30F9 }, /* katakana letter ve */
	{ 0x30F2, 0x3099, 0x30FA }, /* katakana letter vo */
	{ 0x30FD, 0x3099, 0x30FE }, /* katakana voiced iteration mark */
	{ 0x11099, 0x110BA, 0x1109A }, /* kaithi letter dddha */
	{ 0x1109B, 0x110BA, 0x1109C }, /* kaithi letter rha */
	{ 0x110A5, 0x110BA, 0x110AB }, /* kaithi letter va */
	{ 0x11131, 0x11127, 0x1112E }, /* chakma vowel sign o */
	{ 0x11132, 0x11127, 0x1112F }, /* chakma vowel sign au */
	{ 0x11347, 0x1133E, 0x1134B }, /* grantha vowel sign oo */
	{ 0x11347, 0x11357, 0x1134C }, /* grantha vowel sign au */
	{ 0x114B9, 0x114B0, 0x114BC }, /* tirhuta vowel sign o */
	{ 0x114B9, 0x114BA, 0x114BB }, /* tirhuta vowel sign ai */
	{ 0x114B9, 0x114BD, 0x114BE }, /* tirhuta vowel sign au */
	{ 0x115B8, 0x115AF, 0x115BA }, /* siddham vowel sign o */
	{ 0x115B9, 0x115AF, 0x115BB }, /* siddham vowel sign au */
	{ 0x11935, 0x11930, 0x11938 }, /* dives akuru vowel sign o */
};
