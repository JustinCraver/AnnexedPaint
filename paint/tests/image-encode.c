// Guard the real encoder's heap allocations. Extra storage lets the baseline
// overwrite a canary deterministically without corrupting the test process.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct allocation { void *pointer; size_t size; } allocation_t;
static allocation_t allocations[8192];
static int guard_failures, checks, failures;
static allocation_t *find_allocation(void *pointer) {
	for (int i = 0; i < 8192; ++i) { if (allocations[i].pointer == pointer) { return &allocations[i]; } }
	fprintf(stderr, "Untracked allocation %p\n", pointer);
	abort();
}
static int intact(allocation_t *allocation) {
	for (int i = 0; i < 4096; ++i) { if (((unsigned char *)allocation->pointer)[allocation->size + i] != 0xa5) { return 0; } }
	return 1;
}
static void *guarded_realloc(void *pointer, size_t size) {
	allocation_t *allocation = find_allocation(pointer);
	if (pointer && !intact(allocation)) { guard_failures++; }
	void *result = realloc(pointer, size + 4096);
	if (!result) { abort(); }
	allocation->pointer = result;
	allocation->size = size;
	memset((unsigned char *)result + size, 0xa5, 4096);
	return result;
}
static void *guarded_malloc(size_t size) { return guarded_realloc(NULL, size); }
static void guarded_free(void *pointer) {
	if (!pointer) { return; }
	allocation_t *allocation = find_allocation(pointer);
	if (!intact(allocation)) { guard_failures++; }
	allocation->pointer = NULL;
	free(pointer);
}

#define malloc guarded_malloc
#define realloc guarded_realloc
#define free guarded_free
#define WITH_IMAGE_WRITE
#define IRON_BGRA
#include "../../base/sources/iron_image.c"
#undef malloc
#undef realloc
#undef free

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

static void check(int ok, const char *name) {
	checks++;
	if (!ok) { failures++; }
	printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

int main(void) {
	setbuf(stdout, NULL);
	int sizes[] = {1, 4, 17};
	for (int jpg = 0; jpg <= 1; ++jpg) {
		for (int n = 0; n < 3; ++n) {
			int side = sizes[n];
			unsigned char pixels[17 * 17 * 4];
			uint32_t seed = 7;
			for (int i = 0; i < side * side; ++i) {
				for (int c = 0; c < 4; ++c) {
					seed = seed * 1664525u + 1013904223u;
					pixels[i * 4 + c] = n == 2 ? seed >> 24 : c == 0 ? 153 : c == 1 ? 102 : c == 2 ? 51 : 255;
				}
			}
			buffer_t source = {.buffer = pixels, .length = side * side * 4, .capacity = sizeof(pixels)};
			buffer_t *encoded = jpg ? iron_encode_jpg(&source, side, side, 0, 95) : iron_encode_png(&source, side, side, 0);
			allocation_t *allocation = find_allocation(encoded->buffer);
			printf("ENCODE %s %dx%d raw=%u encoded=%u allocated=%zu\n", jpg ? "jpg" : "png", side, side, source.length, encoded->length, allocation->size);
			check(intact(allocation) && encoded->length <= allocation->size, "encoded output fits allocation with intact guard bytes");
			int width, height, channels;
			unsigned char *decoded = stbi_load_from_memory(encoded->buffer, encoded->length, &width, &height, &channels, 4);
			int valid = decoded && width == side && height == side;
			if (valid && n != 2) {
				for (int i = 0; i < side * side; ++i) {
					if (abs(decoded[i * 4] - 51) > 3 || abs(decoded[i * 4 + 1] - 102) > 3 || abs(decoded[i * 4 + 2] - 153) > 3) { valid = 0; }
				}
			}
			check(valid, "small and incompressible PNG/JPEG decode with expected dimensions and color");
			stbi_image_free(decoded);
			guarded_free(encoded->buffer);
			// The production buffer struct may use calloc; only data is guarded.
			allocation_t *struct_allocation = NULL;
			for (int i = 0; i < 8192; ++i) { if (allocations[i].pointer == encoded) { struct_allocation = &allocations[i]; } }
			if (struct_allocation) { guarded_free(encoded); } else { free(encoded); }
		}
	}
	check(guard_failures == 0, "all codec allocations retain their guards through release");
	printf("IMAGE ENCODE checks=%d failures=%d guard_failures=%d\n", checks, failures, guard_failures);
	return failures ? 1 : 0;
}
