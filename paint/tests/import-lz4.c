#include "../../base/sources/iron_lz4.c"
#include <stdio.h>
void iron_log(const char *format, ...) {
	puts(format);
}

static int  checks, failures;
static void check(bool ok, char *name) {
	checks++;
	if (!ok)
		failures++;
	printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}
static void bad(u8 *bytes, u32 length, u32 output, char *name) {
	buffer_t b = {.buffer = bytes, .length = length};
	check(!lz4_validate(&b, output) && lz4_decode(&b, output) == NULL, name);
}
int main(void) {
	bad((u8[]){0xf0}, 1, 64, "missing literal extension");
	bad((u8[]){0x20, 'a'}, 2, 2, "short literal payload");
	bad((u8[]){0x10, 'a', 1}, 3, 5, "short match offset");
	bad((u8[]){0x10, 'a', 0, 0}, 4, 5, "zero match offset");
	bad((u8[]){0x10, 'a', 2, 0}, 4, 5, "match before output start");
	bad((u8[]){0x1f, 'a', 1, 0, 255}, 5, 1024, "missing match extension");
	bad((u8[]){0x10, 'a', 1, 0}, 4, 4, "match beyond output end");
	bad((u8[]){0x10, 'a'}, 2, 2, "decoded byte count too short");
	buffer_t  valid   = {.buffer = (u8[]){0x10, 'a', 1, 0, 0}, .length = 5};
	buffer_t *decoded = lz4_decode(&valid, 5);
	check(decoded != NULL && memcmp(decoded->buffer, "aaaaa", 5) == 0, "overlapping match and empty final literal");
	for (u32 length = 64; length <= 8192; length *= 128) {
		buffer_t *source = buffer_create(length);
		for (u32 i = 0; i < length; ++i)
			source->buffer[i] = (i * 17 + i / 32) & 255;
		buffer_t *encoded   = lz4_encode(source);
		buffer_t *roundtrip = lz4_decode(encoded, length);
		check(roundtrip != NULL && memcmp(source->buffer, roundtrip->buffer, length) == 0, "production LZ4 round trip");
	}
	printf("LZ4 IMPORT: %d assertions, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
