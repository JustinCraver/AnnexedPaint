#include "../sources/functions.h"
#include <stdio.h>

project_t         *g_project;
context_t         *g_context;
any_map_t         *import_mesh_importers;
static int         calls, mode, commits, replacements, errors, failures, checks;
static raw_mesh_t *first;
static raw_mesh_t *accepted[2];

bool path_is_mesh(char *path) {
	return true;
}
bool context_enable_import_plugin(char *path) {
	return false;
}
void console_error(char *text) {
	errors++;
}
char *strings_unknown_asset_format(void) {
	return "Unknown format";
}
char *path_base_name(char *path) {
	return "test";
}
void import_obj_run(char *path, bool replace_existing) {}
void import_blend_mesh_run(char *path, bool replace_existing) {}

static void accept(raw_mesh_t *m) {
	accepted[commits]    = malloc(sizeof(raw_mesh_t));
	*accepted[commits++] = *m;
}
void import_mesh_make_mesh(raw_mesh_t *m) {
	replacements++;
	accept(m);
}
void import_mesh_add_mesh(raw_mesh_t *m) {
	accept(m);
}

// Generated from the exact production staging owner, up to GPU reconstruction.
#include "import-stream-owner.c"

static raw_mesh_t *make_mesh(bool more) {
	return ALLOC_INIT(raw_mesh_t, {.name      = "staged",
	                               .posa      = i16_array_create(12),
	                               .nora      = i16_array_create(6),
	                               .inda      = u32_array_create_from_raw((u32[]){0, 1, 2}, 3),
	                               .has_next  = more,
	                               .scale_pos = 1,
	                               .scale_tex = 1});
}
static raw_mesh_t *importer(char *path) {
	calls++;
	if (mode == 0 || calls == 2 && mode == 1)
		return NULL;
	if (calls == 2 && mode == 4)
		return first;
	raw_mesh_t *m = make_mesh(calls == 1);
	if (calls == 1)
		first = m;
	if (calls == 2 && mode == 2)
		m->inda->buffer[2] = 99;
	if (calls == 2 && mode == 3)
		m->nora->length = 1;
	return m;
}
static void check(bool ok, char *name) {
	checks++;
	if (!ok)
		failures++;
	printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

int main(void) {
	project_t p           = {0};
	context_t c           = {0};
	g_project             = &p;
	g_context             = &c;
	import_mesh_importers = any_map_create();
	any_map_set(import_mesh_importers, "gltf", importer);
	for (mode = 0; mode < 5; ++mode) {
		calls = commits = errors = 0;
		c.layer_filter           = 7;
		string_array_t *previous = string_array_create(0);
		p.mesh_assets            = previous;
		bool ok                  = import_mesh_run("test.gltf", true, true, false);
		check(!ok && commits == 0 && errors == 1 && c.layer_filter == 7 && p.mesh_assets == previous, mode == 0   ? "null first record"
		                                                                                              : mode == 1 ? "null later record preserves project"
		                                                                                              : mode == 2 ? "later bad index preserves project"
		                                                                                              : mode == 3
		                                                                                                  ? "later short normals preserve project"
		                                                                                                  : "duplicate record rejected without double free");
	}
	mode  = 5;
	calls = commits = errors = replacements = 0;
	check(import_mesh_run("test.gltf", true, true, false) && commits == 2 && replacements == 1 && errors == 0,
	      "all records validated before replacement and addition");
	for (int i = 0; i < commits; ++i)
		import_mesh_discard(accepted[i]);
	calls = commits = errors = replacements = 0;
	check(import_mesh_run("test.gltf", false, false, true) && commits == 2 && replacements == 0 && import_mesh_append, "valid append commits both records");
	for (int i = 0; i < commits; ++i)
		import_mesh_discard(accepted[i]);
	printf("MESH STREAM: %d assertions, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
