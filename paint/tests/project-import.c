// Production project decoder, guarded staging ownership and a corrupt-file corpus.
#include "../sources/functions.h"
#include <iron_lz4.h>
#include <stdio.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

static void *owned[65536];
static int   live, alloc_calls, fail_at, failures, checks;

static void *track(void *p) {
	if (p != NULL)
		owned[live++] = p;
	return p;
}
static void *test_calloc(size_t n, size_t size) {
	return ++alloc_calls == fail_at ? NULL : track(calloc(n, size));
}
static void *test_realloc(void *p, size_t size) {
	if (++alloc_calls == fail_at)
		return NULL;
	int index = -1;
	for (int i = 0; i < live; ++i)
		if (owned[i] == p)
			index = i;
	void *next = realloc(p, size);
	if (next != NULL) {
		if (index < 0)
			track(next);
		else
			owned[index] = next;
	}
	return next;
}
static void test_free(void *p) {
	if (p == NULL)
		return;
	for (int i = 0; i < live; ++i)
		if (owned[i] == p) {
			owned[i] = owned[--live];
			free(p);
			return;
		}
	printf("FAIL unowned/double staging free\n");
	failures++;
}
#define calloc  test_calloc
#define realloc test_realloc
#define free    test_free
#include "../sources/io/import_arm_validate.c"
#undef calloc
#undef realloc
#undef free
#include "../sources/util/util_encode.c"

void iron_log(const char *format, ...) {
	puts(format);
}
char *manifest_version_project = "19";

static void check(bool ok, const char *name) {
	checks++;
	if (!ok)
		failures++;
	printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}
static void release_staging(void) {
	while (live > 0)
		free(owned[--live]);
}

static project_t *fixture(void) {
	project_t *p      = calloc(1, sizeof(project_t));
	p->version        = "19";
	p->assets         = string_array_create(0);
	p->material_nodes = any_array_create(0);
	p->material_datas = any_array_create(0);
	p->brush_nodes    = any_array_create(0);
	for (int i = 0; i < 2; ++i) {
		ui_node_canvas_t *c =
		    ALLOC_INIT(ui_node_canvas_t, {.name = i == 0 ? "Material A" : "Material B", .nodes = any_array_create(0), .links = any_array_create(0)});
		any_array_push(p->material_nodes, c);
		any_array_push(p->material_datas, ALLOC_INIT(material_data2_t, {.paint_base = true}));
		if (i == 0)
			any_array_push(p->brush_nodes, c);
	}
	buffer_t *pixels = buffer_create(64);
	memset(pixels->buffer, 123, pixels->length);
	buffer_t     *compressed = lz4_encode(pixels);
	layer_data_t *layer      = ALLOC_INIT(layer_data_t, {.name          = "Paint",
	                                                     .res           = 4,
	                                                     .bpp           = 8,
	                                                     .texpaint      = compressed,
	                                                     .texpaint_nor  = compressed,
	                                                     .texpaint_pack = compressed,
	                                                     .parent        = -1,
	                                                     .fill_material = -1,
	                                                     .path_material = -1,
	                                                     .visible       = true});
	p->layer_datas           = any_array_create_from_raw((void *[]){layer}, 1);
	p->mesh_datas            = any_array_create(0);
	p->mesh_transforms       = any_array_create(0);
	p->mesh_materials        = i32_array_create_from_raw((i32[]){0, 1, 1}, 3);
	p->mesh_parents          = i32_array_create_from_raw((i32[]){-1, 0, 0}, 3);
	for (int i = 0; i < 3; ++i) {
		vertex_array_t *pos =
		    ALLOC_INIT(vertex_array_t, {.attrib = "pos",
		                                .data   = "short4norm",
		                                .values = i16_array_create_from_raw((i16[]){0, 0, 0, 32767, 32767, 0, 0, 32767, 0, 32767, 0, 32767}, 12)});
		vertex_array_t *nor = ALLOC_INIT(vertex_array_t, {.attrib = "nor", .data = "short2norm", .values = i16_array_create(6)});
		vertex_array_t *tex = ALLOC_INIT(vertex_array_t, {.attrib = "tex", .data = "short2norm", .values = i16_array_create(6)});
		mesh_data_t    *m   = ALLOC_INIT(mesh_data_t, {.name          = i == 0   ? "First"
		                                                                : i == 1 ? "Second"
		                                                                         : "Shared@0",
		                                               .scale_pos     = 1,
		                                               .scale_tex     = 1,
		                                               .vertex_arrays = any_array_create_from_raw((void *[]){pos, nor, tex}, 3),
		                                               .index_array   = u32_array_create_from_raw((u32[]){0, 1, 2}, 3)});
		any_array_push(p->mesh_datas, m);
		any_array_push(p->mesh_transforms, f32_array_create_from_raw((f32[]){1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 3, 4, 1}, 16));
	}
	return p;
}

static void write_blob(const char *dir, const char *name, buffer_t *b) {
	char file[4096];
	snprintf(file, sizeof(file), "%s/%s.arm", dir, name);
	FILE *f = fopen(file, "wb");
	if (f == NULL || fwrite(b->buffer, 1, b->length, f) != b->length || fclose(f) != 0) {
		printf("FAIL fixture write %s\n", file);
		failures++;
	}
}

static void reject(project_t *p, const char *dir, const char *name) {
	buffer_t *b = util_encode_project(p);
	write_blob(dir, name, b);
	check(import_arm_decode_project(b) == NULL && live == 0, name);
	release_staging();
	array_free(b);
	free(b);
}

static void legacy_layer_field(buffer_t *b) {
	// Versions 0-6 called this reference fill_layer. Preserve that representation
	// in the generated legacy fixtures, rather than only changing a version label.
	for (u32 i = 0; i + 18 <= b->length; ++i) {
		u32 length;
		memcpy(&length, b->buffer + i + 1, 4);
		if (b->buffer[i] != 0xdb || length != 13 || memcmp(b->buffer + i + 5, "fill_material", 13) != 0)
			continue;
		length = 10;
		memcpy(b->buffer + i + 1, &length, 4);
		memcpy(b->buffer + i + 5, "fill_layer", 10);
		memmove(b->buffer + i + 15, b->buffer + i + 18, b->length - i - 18);
		b->length -= 3;
	}
}

int main(int argc, char **argv) {
	if (argc < 2)
		return 2;
	project_t *p       = fixture();
	buffer_t  *encoded = util_encode_project(p);
	project_t *decoded = import_arm_decode_project(encoded);
	check(decoded != NULL && decoded->mesh_datas->length == 3 && decoded->mesh_materials->buffer[1] == 1 &&
	          string_equals(decoded->material_nodes->buffer[1]->name, "Material B") && decoded->mesh_transforms->buffer[1]->buffer[12] == 2 &&
	          decoded->mesh_datas->buffer[0]->index_array->buffer[2] == 2,
	      "current geometry, materials, hierarchy and transforms");
	int allocation_count = alloc_calls;
	release_staging();
	bool truncation_ok = true;
#ifdef _WIN32
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	size_t readable = (encoded->length + info.dwPageSize - 1) / info.dwPageSize * info.dwPageSize;
	u8    *guard    = VirtualAlloc(NULL, readable + info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	DWORD  old_protection;
	check(guard != NULL && VirtualProtect(guard + readable, info.dwPageSize, PAGE_NOACCESS, &old_protection), "input guard page installed");
	if (guard == NULL)
		return 4;
#endif
	for (u32 n = 0; n < encoded->length; ++n) {
		buffer_t short_file = {.buffer = encoded->buffer, .length = n};
#ifdef _WIN32
		short_file.buffer = guard + readable - n;
		memcpy(short_file.buffer, encoded->buffer, n);
#endif
		if (import_arm_decode_project(&short_file) != NULL || live != 0)
			truncation_ok = false;
		release_staging();
	}
#ifdef _WIN32
	VirtualFree(guard, 0, MEM_RELEASE);
#endif
	check(truncation_ok, "every truncated prefix rejected and staging freed");
	bool allocations_ok = true;
	for (int n = 1; n <= allocation_count; ++n) {
		alloc_calls = 0;
		fail_at     = n;
		if (import_arm_decode_project(encoded) != NULL || live != 0)
			allocations_ok = false;
		release_staging();
	}
	fail_at = 0;
	check(allocations_ok, "every staging allocation failure rejected without leaks");
	buffer_t *terminated = buffer_create(encoded->length + 1);
	memcpy(terminated->buffer, encoded->buffer, encoded->length);
	check(import_arm_decode_project(terminated) != NULL, "historical trailing zero accepted");
	release_staging();
	terminated->buffer[terminated->length - 1] = 1;
	check(import_arm_decode_project(terminated) == NULL && live == 0, "nonzero trailing byte rejected");
	write_blob(argv[1], "trailing-data", terminated);
	buffer_t *mutated = buffer_create(encoded->length);
	memcpy(mutated->buffer, encoded->buffer, encoded->length);
	// Swap the first two complete fields. The old decoder assumed positional layout.
	memcpy(mutated->buffer + 5, encoded->buffer + 24, 16);
	memcpy(mutated->buffer + 21, encoded->buffer + 5, 19);
	check(import_arm_decode_project(mutated) != NULL, "reordered fields decoded by name");
	release_staging();
	memcpy(mutated->buffer, encoded->buffer, encoded->length);
	mutated->buffer[35] = 0xdb;
	check(import_arm_decode_project(mutated) == NULL && live == 0, "string cannot impersonate asset array");
	write_blob(argv[1], "wrong-field-type", mutated);
	memcpy(mutated->buffer, encoded->buffer, encoded->length);
	u32 huge_count = INT_MAX;
	memcpy(mutated->buffer + 1, &huge_count, 4);
	check(import_arm_decode_project(mutated) == NULL && live == 0, "map count bounded by file length");
	write_blob(argv[1], "oversized-count", mutated);
	buffer_t *duplicate = buffer_create(128);
	armpack_encode_start(duplicate->buffer);
	armpack_encode_map(2);
	armpack_encode_string("version");
	armpack_encode_string("19");
	armpack_encode_string("version");
	armpack_encode_string("19");
	duplicate->length = armpack_encode_end();
	check(import_arm_decode_project(duplicate) == NULL && live == 0, "duplicate fields rejected");
	write_blob(argv[1], "duplicate-field", duplicate);
	buffer_t tiny = {.buffer = (u8[]){0xdf, 1, 0, 0, 0}, .length = 5};
	write_blob(argv[1], "truncated", &tiny);
	buffer_t empty = {.buffer = (u8 *)"", .length = 0};
	write_blob(argv[1], "empty", &empty);
	for (int v = 0; v <= 19; ++v) {
		char version[4];
		snprintf(version, sizeof(version), "%d", v);
		p->version                               = version;
		p->layer_datas->buffer[0]->fill_material = 1;
		buffer_t *old                            = util_encode_project(p);
		if (v < 7)
			legacy_layer_field(old);
		decoded = import_arm_decode_project(old);
		check(decoded != NULL && decoded->mesh_datas->length == 3 && decoded->material_nodes->length == 2 &&
		          decoded->layer_datas->buffer[0]->fill_material == 1 && decoded->mesh_datas->buffer[1]->index_array->length == 3 &&
		          (v < 6 ? decoded->mesh_transforms->buffer[1]->buffer[12] == 0 : decoded->mesh_transforms->buffer[1]->buffer[12] == 2),
		      version);
		release_staging();
		array_free(old);
		free(old);
	}
	p->layer_datas->buffer[0]->fill_material = -1;
	p->version                               = "99";
	reject(p, argv[1], "unsupported-version");
	p->version = "19";
	p->version = NULL;
	reject(p, argv[1], "missing-version");
	p->version                   = "19";
	layer_data_t_array_t *layers = p->layer_datas;
	p->layer_datas               = NULL;
	reject(p, argv[1], "missing-layers");
	p->layer_datas              = layers;
	mesh_data_t_array_t *meshes = p->mesh_datas;
	p->mesh_datas               = any_array_create(0);
	reject(p, argv[1], "empty-meshes");
	p->mesh_datas          = meshes;
	string_array_t *assets = p->assets;
	p->assets              = NULL;
	reject(p, argv[1], "missing-assets");
	p->assets                  = assets;
	p->mesh_parents->buffer[0] = 1;
	reject(p, argv[1], "cyclic-parent");
	p->mesh_parents->buffer[0]   = -1;
	p->mesh_materials->buffer[1] = 99;
	reject(p, argv[1], "bad-material-index");
	p->mesh_materials->buffer[1]          = 1;
	p->mesh_transforms->buffer[1]->length = 2;
	reject(p, argv[1], "short-transform");
	p->mesh_transforms->buffer[1]->length            = 16;
	p->mesh_datas->buffer[1]->index_array->buffer[2] = 99;
	reject(p, argv[1], "bad-vertex-index");
	p->mesh_datas->buffer[1]->index_array->buffer[2] = 2;
	layer_data_t *l                                  = p->layer_datas->buffer[0];
	l->res                                           = INT_MAX;
	reject(p, argv[1], "oversized-layer");
	buffer_t *base_pixels = l->texpaint, *normal_pixels = l->texpaint_nor, *packed_pixels = l->texpaint_pack;
	buffer_t  zero_pixels = {.buffer = (u8[]){0}, .length = 1};
	l->res                = 1 << 30;
	l->bpp                = 32;
	l->texpaint = l->texpaint_nor = l->texpaint_pack = &zero_pixels;
	reject(p, argv[1], "overflow-layer");
	l->bpp           = 8;
	l->texpaint      = base_pixels;
	l->texpaint_nor  = normal_pixels;
	l->texpaint_pack = packed_pixels;
	l->res           = 4;
	l->bpp           = 7;
	reject(p, argv[1], "bad-bpp");
	l->bpp    = 8;
	l->parent = 0;
	reject(p, argv[1], "cyclic-layer");
	l->parent                         = -1;
	u32 compressed_length             = ((buffer_t *)l->texpaint)->length;
	((buffer_t *)l->texpaint)->length = 1;
	reject(p, argv[1], "truncated-lz4");
	((buffer_t *)l->texpaint)->length = compressed_length;
	l->fill_material                  = 99;
	reject(p, argv[1], "bad-fill-reference");
	l->fill_material = -1;
	// Caller-supplied native fixture is also decoded independently before use.
	if (argc > 2) {
		FILE *f = fopen(argv[2], "rb");
		if (f == NULL)
			return 3;
		fseek(f, 0, SEEK_END);
		long size = ftell(f);
		rewind(f);
		buffer_t *b = buffer_create(size);
		fread(b->buffer, 1, size, f);
		fclose(f);
		decoded = import_arm_decode_project(b);
		check(decoded != NULL, "native saved project passes checked decoder");
		if (decoded != NULL && argc < 4)
			check(decoded->mesh_datas->length == 3 && decoded->material_nodes->length == 2 && decoded->mesh_materials->buffer[2] == 1,
			      "native multi-object material mapping retained");
		if (decoded != NULL && argc < 4)
			for (int v = 4; v <= 19; v += v == 4 ? 5 : v == 9 ? 9 : 1) {
				char version[4], name[32];
				snprintf(version, sizeof(version), "%d", v);
				snprintf(name, sizeof(name), "valid-version-%d", v);
				decoded->version = version;
				buffer_t *old    = util_encode_project(decoded);
				if (v < 7)
					legacy_layer_field(old);
				write_blob(argv[1], name, old);
				project_t *roundtrip = import_arm_decode_project(old);
				check(roundtrip != NULL && roundtrip->mesh_datas->length == 3 && roundtrip->material_nodes->length == 2 &&
				          (v < 9 || roundtrip->mesh_materials->buffer[2] == 1),
				      name);
				array_free(old);
				free(old);
			}
		release_staging();
	}
	printf("PROJECT IMPORT: %d assertions, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
