// Exercise the production PNG/JPEG writer, including failures after open.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fault, closes, failures, checks;
static size_t test_fwrite(const void *data, size_t size, size_t count, FILE *file);
static int test_fflush(FILE *file);
static int test_fclose(FILE *file);
#define fwrite test_fwrite
#define fflush test_fflush
#define fclose test_fclose
#define WITH_IMAGE_WRITE
#include "../../base/sources/iron_image.c"
#undef fwrite
#undef fflush
#undef fclose

static size_t test_fwrite(const void *data, size_t size, size_t count, FILE *file) {
	return fwrite(data, size, fault == 1 && count > 0 ? count - 1 : count, file);
}

static int test_fflush(FILE *file) {
	int result = fflush(file);
	return fault == 2 ? EOF : result;
}

static int test_fclose(FILE *file) {
	closes++;
	int result = fclose(file);
	return fault == 3 ? EOF : result;
}

static void check(int ok, const char *name) {
	checks++;
	if (!ok) { failures++; }
	printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

int main(int argc, char **argv) {
	if (argc != 2) { return 2; }
	unsigned char pixels[] = {51, 102, 153, 255, 51, 102, 153, 255, 51, 102, 153, 255, 51, 102, 153, 255};
	buffer_t bytes = {.buffer = pixels, .length = sizeof(pixels), .capacity = sizeof(pixels)};
	char path[4096];
	for (int jpg = 0; jpg <= 1; ++jpg) {
		snprintf(path, sizeof(path), "%s/image.%s", argv[1], jpg ? "jpg" : "png");
		for (fault = 0; fault <= 3; ++fault) {
			closes = 0;
			bool result = jpg ? iron_write_jpg(path, &bytes, 2, 2, 2, 95) : iron_write_png(path, &bytes, 2, 2, 2);
			check(result == (fault == 0) && closes == 1, "encoding, short write, flush and close results propagate; stream closes");
		}
		fault = 0;
		snprintf(path, sizeof(path), "%s/missing/image.%s", argv[1], jpg ? "jpg" : "png");
		closes = 0;
		check(!(jpg ? iron_write_jpg(path, &bytes, 2, 2, 2, 95) : iron_write_png(path, &bytes, 2, 2, 2)) && closes == 0,
		      "missing directory fails before encoding");
	}
	printf("IMAGE WRITE checks=%d failures=%d\n", checks, failures);
	return failures ? 1 : 0;
}
