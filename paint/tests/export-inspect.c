// Decode the files produced by the native CLI, independently of the exporter.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint32_t read_u32(const unsigned char *p) {
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float sample(const unsigned char *p, int bytes) {
	if (bytes == 4) {
		float value;
		memcpy(&value, p, 4);
		return value;
	}
	unsigned value = p[0] | p[1] << 8;
	unsigned exp = (value >> 10) & 31, mantissa = value & 1023;
	float result = exp == 0 ? ldexpf((float)mantissa, -24) : exp == 31 ? INFINITY : ldexpf(1.0f + mantissa / 1024.0f, (int)exp - 15);
	return value & 32768 ? -result : result;
}

static int inspect_exr(unsigned char *data, size_t size, int bytes, int known) {
	if (size < 8 || read_u32(data) != 20000630 || read_u32(data + 4) != 2) { return 0; }
	size_t p = 8;
	int width = 0, height = 0, channels = 0, compression = -1;
	char order[4] = {0};
	while (p < size && data[p]) {
		char *name = (char *)data + p;
		size_t n = strlen(name) + 1;
		if (n > size - p) { return 0; }
		p += n;
		n = strlen((char *)data + p) + 1;
		if (n > size - p) { return 0; }
		p += n;
		if (size - p < 4) { return 0; }
		n = read_u32(data + p); p += 4;
		if (n > size - p) { return 0; }
		if (strcmp(name, "dataWindow") == 0 && n == 16) {
			width = (int)read_u32(data + p + 8) - (int)read_u32(data + p) + 1;
			height = (int)read_u32(data + p + 12) - (int)read_u32(data + p + 4) + 1;
		}
		if (strcmp(name, "compression") == 0 && n == 1) { compression = data[p]; }
		if (strcmp(name, "channels") == 0) {
			size_t c = p;
			while (c < p + n && data[c]) {
				if (channels >= 3 || c + 18 > p + n || data[c + 1] || read_u32(data + c + 2) != (bytes == 2 ? 1u : 2u) ||
				    read_u32(data + c + 10) != 1 || read_u32(data + c + 14) != 1) { return 0; }
				order[channels++] = data[c]; c += 18;
			}
		}
		p += n;
	}
	p++;
	if (width != 2048 || height != 2048 || channels != 3 || compression != 0 || p + height * 8 > size) { return 0; }
	for (int y = 0; y < height; ++y) {
		uint64_t offset = read_u32(data + p + y * 8) | (uint64_t)read_u32(data + p + y * 8 + 4) << 32;
		size_t row_size = (size_t)width * channels * bytes;
		if (offset > size || size - offset < 8 + row_size || read_u32(data + offset) != (uint32_t)y || read_u32(data + offset + 4) != row_size) { return 0; }
		for (int c = 0; c < channels; ++c) {
			float expected = order[c] == 'R' ? 0.2f : order[c] == 'G' ? 0.4f : order[c] == 'B' ? 0.6f : -1.0f;
			if (expected < 0) { return 0; }
			for (int x = 0; x < width; ++x) {
				float value = sample(data + offset + 8 + ((size_t)c * width + x) * bytes, bytes);
				if (!isfinite(value) || (known && fabsf(value - expected) > 0.005f)) { return 0; }
			}
		}
	}
	return 1;
}

int main(int argc, char **argv) {
	if (argc < 3) { return 2; }
	int known = argc > 3;
	int ok = 1, width = 0, height = 0, channels = 0;
	if (strcmp(argv[2], "exr16") == 0 || strcmp(argv[2], "exr32") == 0) {
		FILE *file = fopen(argv[1], "rb");
		if (!file) { return 1; }
		fseek(file, 0, SEEK_END); size_t size = ftell(file); rewind(file);
		unsigned char *data = calloc(1, size + 1);
		ok = fread(data, 1, size, file) == size;
		fclose(file);
		ok = ok && inspect_exr(data, size, strcmp(argv[2], "exr16") == 0 ? 2 : 4, known);
		free(data);
	}
	else {
		FILE *file = fopen(argv[1], "rb");
		if (!file) { return 1; }
		fseek(file, 0, SEEK_END); int size = (int)ftell(file); rewind(file);
		unsigned char *data = malloc(size);
		int read_ok = fread(data, 1, size, file) == (size_t)size;
		fclose(file);
		unsigned char *pixels = read_ok ? stbi_load_from_memory(data, size, &width, &height, &channels, 3) : NULL;
		free(data);
		ok = pixels != NULL && width == 2048 && height == 2048 && channels == 3;
		if (ok && known) {
			for (int i = 0; i < width * height; ++i) {
				if (abs(pixels[i * 3] - 51) > 3 || abs(pixels[i * 3 + 1] - 102) > 3 || abs(pixels[i * 3 + 2] - 153) > 3) { ok = 0; break; }
			}
		}
		stbi_image_free(pixels);
	}
	printf("DECODE %s %s known_color=%d\n", ok ? "PASS" : "FAIL", argv[2], known);
	return ok ? 0 : 1;
}
