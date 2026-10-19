// Compare decoded pixels, independently of the editor and PNG encoder.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned char *load_png(char *path, int *width, int *height, int *channels) {
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		return NULL;
	}
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);
	unsigned char *data   = size > 0 && size <= INT_MAX ? malloc((size_t)size) : NULL;
	unsigned char *pixels = NULL;
	if (data != NULL && fread(data, 1, size, file) == (size_t)size) {
		pixels = stbi_load_from_memory(data, (int)size, width, height, channels, 4);
	}
	free(data);
	fclose(file);
	return pixels;
}

int main(int argc, char **argv) {
	if (argc != 4) {
		return 2;
	}
	int            aw = 0, ah = 0, ac = 0, bw = 0, bh = 0, bc = 0;
	unsigned char *a = load_png(argv[2], &aw, &ah, &ac);
	if (strcmp(argv[1], "known") == 0) {
		int expected[3] = {0};
		int recognized  = 1;
		if (strcmp(argv[3], "base") == 0) {
			expected[0] = 51;
			expected[1] = 102;
			expected[2] = 153;
		}
		else if (strcmp(argv[3], "nor") == 0) {
			// color_from_floats truncates each 0.5 clear to 127/255. The
			// default whiteout blend combines that layer and export clear;
			// these are the quantized Windows/D3D12 neutral-normal values.
			expected[0] = 126;
			expected[1] = 126;
			expected[2] = 255;
		}
		else if (strcmp(argv[3], "occ") == 0) {
			expected[0] = expected[1] = expected[2] = 255;
		}
		else if (strcmp(argv[3], "rough") == 0) {
			expected[0] = expected[1] = expected[2] = 64;
		}
		else if (strcmp(argv[3], "metal") != 0) {
			recognized = 0;
		}
		int channels = strcmp(argv[3], "base") == 0 || strcmp(argv[3], "nor") == 0 ? 3 : 1;
		int ok       = recognized && a != NULL && aw == 2048 && ah == 2048 && ac == channels;
		for (int i = 0; ok && i < aw * ah; ++i) {
			for (int c = 0; c < 3; ++c) {
				if (abs(a[i * 4 + c] - expected[c]) > 1) {
					printf("Mismatch pixel=%d channel=%d actual=%d expected=%d\n", i, c, a[i * 4 + c], expected[c]);
					ok = 0;
				}
			}
		}
		printf("PIXELS %s known %s dimensions=%dx%d channels=%d\n", ok ? "PASS" : "FAIL", argv[3], aw, ah, ac);
		stbi_image_free(a);
		return ok ? 0 : 1;
	}
	unsigned char *b       = load_png(argv[3], &bw, &bh, &bc);
	int            ok      = a != NULL && b != NULL && aw == 2048 && ah == 2048 && aw == bw && ah == bh;
	int            changed = 0, erased = 0;
	if (ok) {
		for (int i = 0; i < aw * ah; ++i) {
			changed += memcmp(a + i * 4, b + i * 4, 4) != 0;
			erased += b[i * 4 + 3] < a[i * 4 + 3];
		}
		if (strcmp(argv[1], "equal") == 0) {
			ok = changed == 0;
		}
		else if (strcmp(argv[1], "changed") == 0) {
			ok = changed > 0 && changed < aw * ah;
		}
		else if (strcmp(argv[1], "erased") == 0) {
			ok = erased > 0 && changed < aw * ah;
		}
		else {
			ok = 0;
		}
	}
	printf("PIXELS %s %s dimensions=%dx%d/%dx%d changed=%d erased=%d\n", ok ? "PASS" : "FAIL", argv[1], aw, ah, bw, bh, changed, erased);
	stbi_image_free(a);
	stbi_image_free(b);
	return ok ? 0 : 1;
}
