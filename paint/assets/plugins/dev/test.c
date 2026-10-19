// editor-windows.ps1 appends main() with a disposable root and mode.
#include "global.h"

char            *tmp_root;
char            *mode;
char            *file_project;
int              step     = 0;
int              checks   = 0;
int              failures = 0;
slot_material_t *mat_noise;

void check(int ok, char *name) {
	checks++;
	if (ok) {
		printf("[test]   ok   %s", name);
	}
	else {
		failures++;
		printf("[test]   FAIL %s", name);
	}
}

int capture(char *name) {
	slot_layer_t *layer = script_layer_get("test_paint");
	if (layer == NULL || layer->texpaint == NULL) {
		return 0;
	}
	gpu_texture_t *image  = layer->texpaint;
	buffer_t      *pixels = script_get_texture_pixels(image);
	if (pixels == NULL || image->width != 2048 || image->height != 2048 || pixels->length != 2048 * 2048 * 4) {
		return 0;
	}
	return iron_write_png(string("%s/%s.png", tmp_root, name), pixels, image->width, image->height, 0);
}

void finished(int expected_checks) {
	if (checks != expected_checks) {
		failures++;
		printf("[test]   FAIL expected %d checks, ran %d", expected_checks, checks);
	}
	printf("[test] %d checks, %d failures", checks, failures);
	if (failures == 0) {
		printf("TEST PASS %s", mode);
	}
	else {
		printf("TEST FAIL %s", mode);
	}
	// CLI observes the final assertion and returns nonzero. Both outcomes use
	// automatic CLI completion; no script_quit() bypasses shutdown.
	script_assert(failures == 0, "editor exercise failed");
}

void verify_state(float opacity) {
	slot_layer_t *layer = script_layer_get("test_paint");
	check(script_layer_count() == 2 && layer != NULL && layer->mask_opacity == opacity, "layer count, name and opacity round trip");
	slot_material_t *material = script_get_material("test_noise");
	check(material != NULL, "named material round trip");
	if (material != NULL) {
		script_material_set(material);
		ui_node_t *noise = script_material_get_node("TEX_NOISE");
		if (noise != NULL) {
			ui_node_socket_t *scale = noise->inputs->buffer[1];
			check(scale->default_value->buffer[0] == 8.0, "material node scale round trip");
		}
	}
	check(script_get_object("test_shape") != NULL && script_get_object("test_shape_copy") != NULL, "named geometry round trip");
}

void verify_fresh_process() {
	verify_state(0.75);
	check(capture("fresh"), "fresh process layer pixels captured");
	check(export_texture_run(string("%s/fresh", tmp_root), false), "fresh process texture export completed");
	finished(6);
}

void create_material() {
	mat_noise = script_material_create("test_noise");
	check(mat_noise != NULL, "material created");
	ui_node_t *noise = script_material_create_node_at("TEX_NOISE", -700.0, 0.0);
	ui_node_t *rgb   = script_material_create_node_at("RGB", -700.0, 400.0);
	ui_node_t *mix   = script_material_create_node_at("MIX_RGB", -350.0, 0.0);
	ui_node_t *out   = script_material_get_node("OUTPUT_MATERIAL_PBR");
	int        ok    = noise != NULL && rgb != NULL && mix != NULL && out != NULL;
	check(ok, "material nodes created");
	if (!ok) {
		return;
	}
	script_material_set_float(noise, true, 1, 8.0);
	script_material_connect(noise, 0, mix, 0);
	script_material_connect(rgb, 0, mix, 1);
	script_material_connect(mix, 0, out, 0);
	script_material_update();
	check(script_material_get_node_id(noise->id) == noise, "material node lookup");
	int links      = mat_noise->canvas->links->length;
	int nodes      = mat_noise->canvas->nodes->length;
	int removed_id = rgb->id;
	script_material_disconnect(mix, 1);
	script_material_remove_node(rgb);
	script_material_update();
	check(script_material_get_node_id(removed_id) == NULL && mat_noise->canvas->nodes->length == nodes - 1 && mat_noise->canvas->links->length == links - 1,
	      "material node disconnect and remove");
	slot_material_t *temporary = script_material_create("test_temp");
	check(temporary != NULL, "temporary material created");
	script_material_delete(temporary);
	check(script_get_material("test_temp") == NULL, "temporary material deleted");
	script_material_set(mat_noise);
}

void create_shapes() {
	string_array_t *shapes = script_shape_list();
	check(shapes != NULL && shapes->length > 0, "shape list");
	if (shapes == NULL || shapes->length == 0) {
		return;
	}
	object_t *object = script_shape_add(shapes->buffer[0]);
	check(object != NULL, "shape added");
	if (object == NULL) {
		return;
	}
	script_object_set_name(object, "test_shape");
	object_t *copy = script_object_duplicate(object);
	check(copy != NULL && copy != object, "distinct object duplicated");
	if (copy != NULL) {
		script_object_set_name(copy, "test_shape_copy");
	}
}

void run_step(int s) {
	if (s == 0) {
		context_t *ctx = script_get_context();
		int        ok  = 1;
		for (int i = 0; i < 14; ++i) {
			context_select_tool(i);
			if (ctx->tool != i) {
				ok = 0;
			}
		}
		check(ok, "select all original tools");
		context_select_tool(TOOL_TYPE_BRUSH);
		slot_layer_t *layer = script_layer_new();
		check(layer != NULL, "transparent paint layer created");
		script_layer_set_name(layer, "test_paint");
	}
	else if (s == 1) {
		check(capture("empty"), "baseline pixels captured");
		context_t *ctx      = script_get_context();
		ctx->brush_radius   = 0.5;
		ctx->brush_opacity  = 1.0;
		ctx->brush_hardness = 0.8;
		ctx->brush_scale    = 1.0;
		for (int i = 0; i < 8; ++i) {
			float t = i * 0.125;
			script_paint(0.35 + t * 0.3, 0.3 + t * 0.4);
		}
		script_paint_end();
	}
	else if (s == 2) {
		check(capture("paint"), "brush pixels captured for change assertion");
		script_undo();
	}
	else if (s == 3) {
		check(capture("undo"), "undo pixels captured for baseline assertion");
		script_redo();
	}
	else if (s == 4) {
		check(capture("redo"), "redo pixels captured for painted assertion");
		context_select_tool(TOOL_TYPE_ERASER);
		script_paint(0.45, 0.43);
		script_paint(0.50, 0.50);
		script_paint_end();
	}
	else if (s == 5) {
		check(capture("erased"), "eraser pixels captured for alpha assertion");
		context_select_tool(TOOL_TYPE_BRUSH);
		script_paint_world(0.0, 0.0, 0.0);
		script_paint_end();
	}
	else if (s == 6) {
		check(capture("world"), "world stroke pixels captured for change assertion");
		script_fill_layer();
	}
	else if (s == 7) {
		check(capture("filled"), "fill pixels captured for change assertion");
		create_material();
	}
	else if (s == 8) {
		create_shapes();
		slot_layer_t *layer = script_layer_get("test_paint");
		if (layer != NULL) {
			layer->mask_opacity = 0.625;
		}
	}
	else if (s == 9) {
		// Export only the reference layer so blending with painted normals cannot
		// change the known values. Restore the two saved layers afterwards.
		slot_layer_t *background = script_layer_at(0);
		slot_layer_t *paint      = script_layer_get("test_paint");
		background->visible      = false;
		if (paint != NULL) {
			paint->visible = false;
		}
		slot_layer_t *reference = script_layer_new_color(0xff336699, 1.0, 0.25, 0.0);
		check(reference != NULL, "known color export layer created");
		check(export_texture_run(string("%s/known", tmp_root), false), "known color texture export completed");
		script_layer_delete(reference);
		background->visible = true;
		if (paint != NULL) {
			paint->visible = true;
		}
		check(script_layer_count() == 2, "temporary export layer removed");
		check(export_texture_run(string("%s/textures", tmp_root), false), "texture export completed");
		script_export_mesh(string("%s/test_mesh", tmp_root));
		check(iron_file_exists(string("%s/test_mesh.obj", tmp_root)), "mesh export exists");
		script_export_material(string("%s/test_material.arm", tmp_root));
		check(iron_file_exists(string("%s/test_material.arm", tmp_root)), "material export exists");
		project_filepath_set(file_project);
		sys_title_set("test_pending* - ArmorPaint");
		project_save(false);
		check(string_equals(sys_title(), "test_pending* - ArmorPaint"), "deferred save retains dirty title");
	}
	else if (s == 10) {
		check(iron_file_exists(file_project) && string_index_of(sys_title(), "* - ") < 0, "save completed and cleared dirty title");
		check(string_equals(string_replace_all(project_filepath_get(), "\\", "/"), file_project), "saved filepath committed");
		script_project_new();
	}
	else if (s == 11) {
		check(script_layer_get("test_paint") == NULL && script_get_material("test_noise") == NULL, "new project clears saved fixture");
		script_project_open(file_project);
	}
	else if (s == 12) {
		verify_state(0.625);
		check(capture("reopened"), "reopened layer pixels captured");
		check(export_texture_run(string("%s/after", tmp_root), false), "reopened texture export completed");
		slot_layer_t *layer = script_layer_get("test_paint");
		if (layer != NULL) {
			script_layer_set(layer);
			layer->mask_opacity = 0.75;
		}
		context_select_tool(TOOL_TYPE_ERASER);
		script_paint(0.45, 0.45);
		script_paint(0.5, 0.5);
		script_paint_end();
	}
	else if (s == 13) {
		check(capture("resaved"), "edited reopened pixels captured");
		check(export_texture_run(string("%s/resaved", tmp_root), false), "edited texture export completed");
		sys_title_set("test_pending* - ArmorPaint");
		project_save(false);
	}
	else if (s == 14) {
		check(iron_file_exists(file_project) && string_index_of(sys_title(), "* - ") < 0, "replacement save completed");
		script_import_asset(string("%s/textures/untitled_base.png", tmp_root), false);
	}
	else if (s == 15) {
		gpu_texture_t *image = script_get_texture("untitled_base.png");
		check(image != NULL && image->width == 2048 && image->height == 2048, "exported texture imported with dimensions");
		script_texture_delete("untitled_base.png");
		check(script_get_texture("untitled_base.png") == NULL, "texture deleted");
		script_import_asset(string("%s/textures/untitled_base.png", tmp_root), false);
	}
	else if (s == 16) {
		check(script_get_texture("untitled_base.png") != NULL, "texture reimported");
		script_project_new();
	}
	else if (s == 17) {
		script_import_asset(string("%s/test_mesh.obj", tmp_root), false);
	}
	else if (s == 18) {
		check(script_get_object("test_shape") != NULL && script_get_object("test_shape_copy") != NULL, "exported named mesh objects imported");
		check(script_get_material("test_noise") == NULL, "material absent before import");
		script_import_asset(string("%s/test_material.arm", tmp_root), false);
	}
	else if (s == 19) {
		check(script_get_material("test_noise") != NULL, "exported named material imported");
	}
}

void next_frame() {
	printf("[test] step %d of 20", step + 1);
	run_step(step);
	step++;
	if (step < 20) {
		script_notify_on_next_frame(next_frame);
	}
	else {
		finished(43);
	}
}

void deliberate_failure() {
	check(0, "deliberate assertion failure in a frame callback");
	finished(1);
}

void test_begin(char *output_root, char *test_mode) {
	tmp_root     = output_root;
	mode         = test_mode;
	file_project = string_copy(string("%s/test_project.arm", tmp_root));
	if (string_equals(mode, "failure")) {
		script_notify_on_next_frame(deliberate_failure);
	}
	else if (string_equals(mode, "reopen")) {
		script_project_open(file_project);
		script_notify_on_next_frame(verify_fresh_process);
	}
	else {
		script_notify_on_next_frame(next_frame);
	}
}
