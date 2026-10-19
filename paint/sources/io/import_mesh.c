
#include "../global.h"

bool import_mesh_clear_layers  = true;
bool import_mesh_needs_unwrap  = false;
bool import_mesh_no_reset      = false;
bool import_mesh_no_scale      = false;
bool import_mesh_keep_timeline = false;
bool import_mesh_append        = false;

static mesh_object_t *import_mesh_appended = NULL;

static bool import_mesh_valid(raw_mesh_t *mesh) {
	if (mesh == NULL || mesh->posa == NULL || mesh->nora == NULL || mesh->inda == NULL || mesh->posa->buffer == NULL || mesh->nora->buffer == NULL ||
	    mesh->inda->buffer == NULL || mesh->posa->length == 0 || mesh->posa->length % 4 != 0 || mesh->inda->length == 0 || mesh->inda->length % 3 != 0 ||
	    !isfinite(mesh->scale_pos) || !isfinite(mesh->scale_tex))
		return false;
	u32 vertices = mesh->posa->length / 4;
	if (mesh->nora->length != vertices * 2 || mesh->texa != NULL && (mesh->texa->buffer == NULL || mesh->texa->length != vertices * 2) ||
	    mesh->texa1 != NULL && (mesh->texa1->buffer == NULL || mesh->texa1->length != vertices * 2) ||
	    mesh->cola != NULL && (mesh->cola->buffer == NULL || mesh->cola->length != vertices * 4))
		return false;
	for (u32 i = 0; i < mesh->inda->length; ++i)
		if (mesh->inda->buffer[i] >= vertices)
			return false;
	return true;
}

static void import_mesh_discard(raw_mesh_t *mesh) {
	if (mesh == NULL)
		return;
	// Importers transfer their arrays to the scene on success. A rejected stream
	// must release those arrays before any scene owns them.
	any_array_t *arrays[] = {(any_array_t *)mesh->posa,  (any_array_t *)mesh->nora, (any_array_t *)mesh->texa,
	                         (any_array_t *)mesh->texa1, (any_array_t *)mesh->cola, (any_array_t *)mesh->inda};
	for (i32 i = 0; i < 6; ++i)
		if (arrays[i] != NULL) {
			array_free(arrays[i]);
			free(arrays[i]);
		}
	free(mesh);
}

static void import_mesh_prepare(bool clear_layers, bool replace_existing, bool keep_camera) {
	import_mesh_clear_layers = clear_layers;
	import_mesh_no_reset     = keep_camera;
	import_mesh_append       = !replace_existing;
	import_mesh_appended     = NULL;
	g_context->layer_filter  = 0;
}

bool import_mesh_run(char *path, bool _clear_layers, bool replace_existing, bool keep_camera) {
	if (!path_is_mesh(path)) {
		if (!context_enable_import_plugin(path)) {
			console_error(strings_unknown_asset_format());
			return false;
		}
	}

	char *p        = to_lower_case(path);
	bool  is_obj   = ends_with(p, ".obj");
	bool  is_blend = ends_with(p, ".blend");
	free(p);

	if (is_obj) {
		import_mesh_prepare(_clear_layers, replace_existing, keep_camera);
		import_obj_run(path, replace_existing);
	}
	else if (is_blend) {
		import_mesh_prepare(_clear_layers, replace_existing, keep_camera);
		import_blend_mesh_run(path, replace_existing);
	}
	else {
		char *ext                           = to_lower_case(path + string_last_index_of(path, ".") + 1);
		raw_mesh_t *(*importer)(char *path) = any_map_get(import_mesh_importers, ext);
		free(ext);
		if (importer == NULL) {
			console_error(strings_unknown_asset_format());
			return false;
		}

		raw_mesh_t_array_t *meshes = any_array_create(0);
		bool                has_next;
		do {
			raw_mesh_t *mesh      = importer(path);
			bool        duplicate = array_index_of(meshes, mesh) >= 0;
			if (!import_mesh_valid(mesh) || duplicate || meshes->length >= 65536) {
				if (!duplicate)
					import_mesh_discard(mesh);
				for (u32 i = 0; i < meshes->length; ++i)
					import_mesh_discard(meshes->buffer[i]);
				array_free(meshes);
				free(meshes);
				console_error(string("Invalid or incomplete mesh data in '%s'", path));
				return false;
			}
			has_next = mesh->has_next;
			any_array_push(meshes, mesh);
		} while (has_next);

		import_mesh_prepare(_clear_layers, replace_existing, keep_camera);
		for (u32 i = 0; i < meshes->length; ++i) {
			raw_mesh_t *mesh = meshes->buffer[i];
			if (mesh->name == NULL || string_equals(mesh->name, ""))
				mesh->name = string_copy(path_base_name(path));
			if (i == 0 && replace_existing)
				import_mesh_make_mesh(mesh);
			else
				import_mesh_add_mesh(mesh);
			free(mesh); // Geometry/name/blob have transferred to the mesh data.
		}
		array_free(meshes);
		free(meshes);
	}

	g_project->mesh_assets = any_array_create_from_raw(
	    (void *[]){
	        path,
	    },
	    1);

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	sys_title_set(substring(path, string_last_index_of(path, PATH_SEP) + 1, string_last_index_of(path, ".")));
#endif
	return true;
}

static mesh_object_t *import_mesh_make_root() {
	char          *name = string_copy(_import_mesh_unique_name("Root"));
	mesh_data_t   *md   = util_shape_make_data("empty");
	md->name            = name;
	mesh_object_t *mo   = scene_add_mesh_object(md, g_context->paint_object->material, NULL);
	mo->base->name      = name;
	mo->skip_context    = "paint";
	tab_stages_add_object(name);
	return mo;
}

i32 import_mesh_finish_import_sort(void **pa, void **pb) {
	mesh_object_t *a = *(pa);
	mesh_object_t *b = *(pb);
	return strcmp(a->base->name, b->base->name);
}

void import_mesh_finish_import(void *_) {
	if (g_context->merged_object != NULL) {
		mesh_data_delete(g_context->merged_object->data);
		mesh_object_remove(g_context->merged_object);
		g_context->merged_object = NULL;
	}

	context_select_paint_object(context_main_object());

	if (!import_mesh_append) {
		// No mask by default
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			mesh_object_t *p = g_project->_->paint_objects->buffer[i];
			p->base->visible = true;
		}
	}

	// Keep appended objects at scene root
	if (import_mesh_append) {
		g_project->mesh_parents = i32_array_create(0);
	}

	if (g_project->_->paint_objects->length > 1) {
		if (!import_mesh_append) {
			// Sort by name
			array_sort(g_project->_->paint_objects, &import_mesh_finish_import_sort);

			// Parent all meshes to an empty root
			mesh_object_t *root = import_mesh_make_root();
			for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
				mesh_object_t *p = g_project->_->paint_objects->buffer[i];
				object_set_parent(p->base, root->base);
				transform_reset(p->base->transform);
				transform_build_matrix(p->base->transform);
			}
			array_insert(g_project->_->paint_objects, 0, root);
			g_project->mesh_parents = i32_array_create(0);
		}
		context_select_paint_object(import_mesh_append ? context_main_object() : g_project->_->paint_objects->buffer[1]);

		if (g_context->merged_object == NULL) {
			util_mesh_merge(NULL);
		}
		if (g_context->merged_object != NULL) {
			g_context->paint_object->skip_context   = "paint";
			g_context->merged_object->base->visible = true;
		}
	}

	if (import_mesh_append && import_mesh_appended != NULL && array_index_of(g_project->_->paint_objects, import_mesh_appended) >= 0) {
		context_select_paint_object(import_mesh_appended);
	}
	import_mesh_appended = NULL;

	if (!import_mesh_no_scale && !import_mesh_append) {
		viewport_scale_to_bounds(2.0);
	}
	import_mesh_no_scale = false;

	if (string_equals(g_context->paint_object->base->name, "")) {
		g_context->paint_object->base->name = "Object";
	}

	make_material_parse_paint_material(true);
	make_material_parse_mesh_material();
	ui_view2d_hwnd->redraws    = 2;
	render_path_raytrace_ready = false;
	g_context->paint_body      = NULL;
	tab_meshes_reset_preview_map();
	base_update_tools();

	if (import_mesh_needs_unwrap) {
		import_mesh_needs_unwrap = false;
		project_unwrap_mesh_box();
	}

	import_mesh_append = false;
}

void _import_mesh_make_mesh_clear_layers(void *_) {
	layers_init();
}

bool _import_mesh_is_unique_name(char *s) {
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		if (string_equals(p->base->name, s)) {
			return false;
		}
	}
	return true;
}

char *_import_mesh_unique_name(char *name) {
	// Returns the name or the next free .00X variant
	char *base;
	i32   i   = strings_split_number_ext(name, &base);
	char *res = i == 0 ? base : string_tmp("%s%s", base, strings_number_ext(i));
	while (!_import_mesh_is_unique_name(res)) {
		res = string_tmp("%s%s", base, strings_number_ext(++i));
	}
	return res;
}

void import_mesh_make_mesh(raw_mesh_t *mesh) {
	if (mesh == NULL || mesh->posa == NULL || mesh->nora == NULL || mesh->inda == NULL || mesh->posa->length == 0) {
		console_error(strings_failed_to_read_mesh_data());
		return;
	}

	import_mesh_needs_unwrap = mesh->texa == NULL;
	if (mesh->texa == NULL) {
		i32 verts  = mesh->posa->length / 4;
		mesh->texa = i16_array_create(verts * 2);
	}

	mesh_data_t *raw = import_mesh_raw_mesh(mesh);

	mesh_data_t *md    = mesh_data_create(raw);
	md->_->skin_blob   = mesh->blob;
	md->_->skin_frame  = -1;
	md->_->owns_arrays = true;

	g_context->paint_object = context_main_object();

	context_select_paint_object(context_main_object());
	if (!import_mesh_no_reset) {
		viewport_reset();
	}

	nodes_mesh_reset();
	util_mesh_remove_objects(g_project->_->paint_objects, g_context->paint_object);

	char *handle = g_context->paint_object->data->_->handle;
	if (!string_equals(handle, "SceneSphere") && !string_equals(handle, "ScenePlane")) {
		sys_notify_on_next_frame(&util_mesh_delete_data_uncache, g_context->paint_object->data);
	}

	mesh_object_set_data(g_context->paint_object, md);
	g_context->paint_object->base->name = mesh->name;

	mesh_object_t_array_t *old_paint_objects = g_project->_->paint_objects;
	g_project->_->paint_objects              = any_array_create_from_raw(
        (void *[]){
            g_context->paint_object,
        },
        1);
	array_delete(old_paint_objects);

	if (!import_mesh_keep_timeline) {
		tab_timeline_reset();
	}

	g_context->ddirty = 4;

	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	util_uv_uvmap_cached                              = false;
	util_uv_trianglemap_cached                        = false;
	util_uv_dilatemap_cached                          = false;

	if (import_mesh_clear_layers) {
		while (g_project->_->layers->length > 0) {
			slot_layer_t *l = array_pop(g_project->_->layers);
			slot_layer_unload(l);
		}
		layers_new_layer(false, -1, NULL);
		sys_notify_on_next_frame(&_import_mesh_make_mesh_clear_layers, NULL);
		history_reset();
	}

	g_project->stages                      = NULL;
	g_context->paint_object->base->visible = true;
	tab_stages_init();

	// Wait for add_mesh calls to finish
	sys_notify_on_next_frame(&import_mesh_finish_import, NULL);
}

void import_mesh_add_mesh(raw_mesh_t *mesh) {
	if (mesh->texa == NULL) {
		i32 verts  = mesh->posa->length / 4;
		mesh->texa = i16_array_create(verts * 2);
	}

	mesh_data_t *raw = import_mesh_raw_mesh(mesh);

	mesh_data_t *md    = mesh_data_create(raw);
	md->_->skin_blob   = mesh->blob;
	md->_->skin_frame  = -1;
	md->_->owns_arrays = true;

	object_t      *parent = import_mesh_append ? NULL : g_context->paint_object->base;
	mesh_object_t *object = scene_add_mesh_object(md, g_context->paint_object->material, parent);
	object->base->name    = mesh->name;
	object->skip_context  = "paint";

	// Ensure unique names
	char *uname = _import_mesh_unique_name(object->base->name);
	if (!string_equals(uname, object->base->name)) {
		object->base->name = string_copy(uname);
		raw->name          = string_copy(uname);
	}

	any_array_push(g_project->_->paint_objects, object);
	tab_stages_add_object(object->base->name);
	if (import_mesh_append && import_mesh_appended == NULL) {
		import_mesh_appended = object;
	}

	g_context->ddirty = 4;

	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	util_uv_uvmap_cached                              = false;
	util_uv_trianglemap_cached                        = false;
	util_uv_dilatemap_cached                          = false;
}

mesh_data_t *import_mesh_raw_mesh(raw_mesh_t *mesh) {
	mesh_data_t *raw = ALLOC_INIT(mesh_data_t, {.name          = mesh->name,
	                                            .vertex_arrays = any_array_create_from_raw(
	                                                (void *[]){
	                                                    ALLOC_INIT(vertex_array_t, {.values = mesh->posa, .attrib = "pos", .data = "short4norm"}),
	                                                    ALLOC_INIT(vertex_array_t, {.values = mesh->nora, .attrib = "nor", .data = "short2norm"}),
	                                                    ALLOC_INIT(vertex_array_t, {.values = mesh->texa, .attrib = "tex", .data = "short2norm"}),
	                                                },
	                                                3),
	                                            .index_array = mesh->inda,
	                                            .scale_pos   = mesh->scale_pos,
	                                            .scale_tex   = mesh->scale_tex});
	if (mesh->texa1 != NULL) {
		vertex_array_t *va = ALLOC_INIT(vertex_array_t, {.values = mesh->texa1, .attrib = "tex1", .data = "short2norm"});
		any_array_push(raw->vertex_arrays, va);
	}
	if (mesh->cola != NULL) {
		vertex_array_t *va = ALLOC_INIT(vertex_array_t, {.values = mesh->cola, .attrib = "col", .data = "short4norm"});
		any_array_push(raw->vertex_arrays, va);
	}
	return raw;
}
