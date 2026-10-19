// Checked project decoding is kept separate from the positional engine decoder.
#include "../functions.h"
#include <iron_lz4.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>

extern char *manifest_version_project;

typedef struct arm_schema arm_schema_t;
typedef struct {
	const char         *name;
	size_t              offset;
	char                type;
	const arm_schema_t *child;
} arm_field_t;

struct arm_schema {
	size_t             size;
	const arm_field_t *fields;
};

// Lowercase: scalar. Uppercase: typed array. a: records, v/t: nested arrays.
#define ARM_FIELD(st, member, kind)     {#member, offsetof(st, member), kind, NULL}
#define ARM_RECORDS(st, member, schema) {#member, offsetof(st, member), 'a', &schema}
#define ARM_SCHEMA(name, st, ...)                                      \
	static const arm_field_t  name##_fields[] = {__VA_ARGS__, {NULL}}; \
	static const arm_schema_t name            = {sizeof(st), name##_fields}

ARM_SCHEMA(arm_socket, ui_node_socket_t, ARM_FIELD(ui_node_socket_t, id, 'i'), ARM_FIELD(ui_node_socket_t, node_id, 'i'),
           ARM_FIELD(ui_node_socket_t, name, 's'), ARM_FIELD(ui_node_socket_t, type, 's'), ARM_FIELD(ui_node_socket_t, color, 'i'),
           ARM_FIELD(ui_node_socket_t, default_value, 'F'), ARM_FIELD(ui_node_socket_t, min, 'f'), ARM_FIELD(ui_node_socket_t, max, 'f'),
           ARM_FIELD(ui_node_socket_t, precision, 'f'), ARM_FIELD(ui_node_socket_t, display, 'i'));
ARM_SCHEMA(arm_button, ui_node_button_t, ARM_FIELD(ui_node_button_t, name, 's'), ARM_FIELD(ui_node_button_t, type, 's'),
           ARM_FIELD(ui_node_button_t, output, 'i'), ARM_FIELD(ui_node_button_t, default_value, 'F'), ARM_FIELD(ui_node_button_t, data, 'B'),
           ARM_FIELD(ui_node_button_t, min, 'f'), ARM_FIELD(ui_node_button_t, max, 'f'), ARM_FIELD(ui_node_button_t, precision, 'f'),
           ARM_FIELD(ui_node_button_t, height, 'f'));
ARM_SCHEMA(arm_node, ui_node_t, ARM_FIELD(ui_node_t, id, 'i'), ARM_FIELD(ui_node_t, name, 's'), ARM_FIELD(ui_node_t, type, 's'), ARM_FIELD(ui_node_t, x, 'f'),
           ARM_FIELD(ui_node_t, y, 'f'), ARM_FIELD(ui_node_t, color, 'i'), ARM_RECORDS(ui_node_t, inputs, arm_socket),
           ARM_RECORDS(ui_node_t, outputs, arm_socket), ARM_RECORDS(ui_node_t, buttons, arm_button), ARM_FIELD(ui_node_t, width, 'f'),
           ARM_FIELD(ui_node_t, flags, 'i'));
ARM_SCHEMA(arm_link, ui_node_link_t, ARM_FIELD(ui_node_link_t, id, 'i'), ARM_FIELD(ui_node_link_t, from_id, 'i'), ARM_FIELD(ui_node_link_t, from_socket, 'i'),
           ARM_FIELD(ui_node_link_t, to_id, 'i'), ARM_FIELD(ui_node_link_t, to_socket, 'i'));
ARM_SCHEMA(arm_canvas, ui_node_canvas_t, ARM_FIELD(ui_node_canvas_t, name, 's'), ARM_RECORDS(ui_node_canvas_t, nodes, arm_node),
           ARM_RECORDS(ui_node_canvas_t, links, arm_link));
ARM_SCHEMA(arm_vertex, vertex_array_t, ARM_FIELD(vertex_array_t, attrib, 's'), ARM_FIELD(vertex_array_t, data, 's'), ARM_FIELD(vertex_array_t, values, 'H'));
ARM_SCHEMA(arm_mesh, mesh_data_t, ARM_FIELD(mesh_data_t, name, 's'), ARM_FIELD(mesh_data_t, scale_pos, 'f'), ARM_FIELD(mesh_data_t, scale_tex, 'f'),
           ARM_RECORDS(mesh_data_t, vertex_arrays, arm_vertex), ARM_FIELD(mesh_data_t, index_array, 'I'));
ARM_SCHEMA(arm_packed, packed_asset_t, ARM_FIELD(packed_asset_t, name, 's'), ARM_FIELD(packed_asset_t, bytes, 'B'));
ARM_SCHEMA(arm_swatch, swatch_color_t, ARM_FIELD(swatch_color_t, base, 'i'), ARM_FIELD(swatch_color_t, opacity, 'f'), ARM_FIELD(swatch_color_t, occlusion, 'f'),
           ARM_FIELD(swatch_color_t, roughness, 'f'), ARM_FIELD(swatch_color_t, metallic, 'f'), ARM_FIELD(swatch_color_t, normal, 'i'),
           ARM_FIELD(swatch_color_t, emission, 'f'), ARM_FIELD(swatch_color_t, height, 'f'), ARM_FIELD(swatch_color_t, subsurface, 'f'));
ARM_SCHEMA(arm_material, material_data2_t, ARM_FIELD(material_data2_t, paint_base, 'b'), ARM_FIELD(material_data2_t, paint_opac, 'b'),
           ARM_FIELD(material_data2_t, paint_occ, 'b'), ARM_FIELD(material_data2_t, paint_rough, 'b'), ARM_FIELD(material_data2_t, paint_met, 'b'),
           ARM_FIELD(material_data2_t, paint_nor, 'b'), ARM_FIELD(material_data2_t, paint_height, 'b'), ARM_FIELD(material_data2_t, paint_emis, 'b'),
           ARM_FIELD(material_data2_t, paint_subs, 'b'), ARM_FIELD(material_data2_t, opac_mode, 'i'));
ARM_SCHEMA(arm_layer, layer_data_t, ARM_FIELD(layer_data_t, name, 's'), ARM_FIELD(layer_data_t, res, 'i'), ARM_FIELD(layer_data_t, bpp, 'i'),
           ARM_FIELD(layer_data_t, texpaint, 'B'), ARM_FIELD(layer_data_t, uv_scale, 'f'), ARM_FIELD(layer_data_t, uv_rot, 'f'),
           ARM_FIELD(layer_data_t, uv_type, 'i'), ARM_FIELD(layer_data_t, decal_mat, 'F'), ARM_FIELD(layer_data_t, opacity_mask, 'f'),
           ARM_FIELD(layer_data_t, fill_material, 'i'), {"fill_layer", offsetof(layer_data_t, fill_material), 'i', NULL},
           ARM_FIELD(layer_data_t, object_mask, 'i'), ARM_FIELD(layer_data_t, blending, 'i'), ARM_FIELD(layer_data_t, parent, 'i'),
           ARM_FIELD(layer_data_t, visible, 'b'), ARM_FIELD(layer_data_t, texpaint_nor, 'B'), ARM_FIELD(layer_data_t, texpaint_pack, 'B'),
           ARM_FIELD(layer_data_t, texpaint_sculpt, 'B'), ARM_FIELD(layer_data_t, paint_base, 'b'), ARM_FIELD(layer_data_t, paint_opac, 'b'),
           ARM_FIELD(layer_data_t, paint_occ, 'b'), ARM_FIELD(layer_data_t, paint_rough, 'b'), ARM_FIELD(layer_data_t, paint_met, 'b'),
           ARM_FIELD(layer_data_t, paint_nor, 'b'), ARM_FIELD(layer_data_t, paint_nor_blend, 'b'), ARM_FIELD(layer_data_t, paint_height, 'b'),
           ARM_FIELD(layer_data_t, paint_height_blend, 'b'), ARM_FIELD(layer_data_t, paint_emis, 'b'), ARM_FIELD(layer_data_t, paint_subs, 'b'),
           ARM_FIELD(layer_data_t, uv_map, 'i'), ARM_FIELD(layer_data_t, path_points, 'F'), ARM_FIELD(layer_data_t, path_points_world, 'F'),
           ARM_FIELD(layer_data_t, path_points_camera, 'F'), ARM_FIELD(layer_data_t, path_points_parent, 'I'), ARM_FIELD(layer_data_t, path_tool, 'i'),
           ARM_FIELD(layer_data_t, path_curved, 'b'), ARM_FIELD(layer_data_t, path_material, 'i'), ARM_FIELD(layer_data_t, path_text, 'b'));
ARM_SCHEMA(arm_layer_key, timeline_layer_keyframe_data_t, ARM_FIELD(timeline_layer_keyframe_data_t, frame, 'i'),
           ARM_FIELD(timeline_layer_keyframe_data_t, layer_index, 'i'), ARM_FIELD(timeline_layer_keyframe_data_t, texpaint, 'B'),
           ARM_FIELD(timeline_layer_keyframe_data_t, texpaint_nor, 'B'), ARM_FIELD(timeline_layer_keyframe_data_t, texpaint_pack, 'B'),
           ARM_FIELD(timeline_layer_keyframe_data_t, texpaint_sculpt, 'B'), ARM_FIELD(timeline_layer_keyframe_data_t, path_points, 'F'),
           ARM_FIELD(timeline_layer_keyframe_data_t, path_points_world, 'F'), ARM_FIELD(timeline_layer_keyframe_data_t, path_points_camera, 'F'),
           ARM_FIELD(timeline_layer_keyframe_data_t, path_points_parent, 'I'), ARM_FIELD(timeline_layer_keyframe_data_t, tween, 'b'));
ARM_SCHEMA(arm_mesh_key, timeline_mesh_keyframe_data_t, ARM_FIELD(timeline_mesh_keyframe_data_t, stage_index, 'i'),
           ARM_FIELD(timeline_mesh_keyframe_data_t, frame, 'i'), ARM_FIELD(timeline_mesh_keyframe_data_t, mesh_index, 'i'),
           ARM_FIELD(timeline_mesh_keyframe_data_t, transform, 'F'), ARM_FIELD(timeline_mesh_keyframe_data_t, tween, 'b'));
ARM_SCHEMA(arm_stage, stage_t, ARM_FIELD(stage_t, name, 's'), ARM_FIELD(stage_t, objects, 'S'), ARM_FIELD(stage_t, layers, 'S'),
           ARM_FIELD(stage_t, hidden, 'S'), ARM_FIELD(stage_t, nested_mesh, 's'));
ARM_SCHEMA(arm_project, project_t, ARM_FIELD(project_t, version, 's'), ARM_FIELD(project_t, assets, 'S'), ARM_FIELD(project_t, is_bgra, 'b'),
           ARM_RECORDS(project_t, packed_assets, arm_packed), ARM_FIELD(project_t, envmap, 's'), ARM_FIELD(project_t, envmap_strength, 'f'),
           ARM_FIELD(project_t, envmap_angle, 'f'), ARM_FIELD(project_t, envmap_blur, 'b'), ARM_FIELD(project_t, camera_world, 'F'),
           ARM_FIELD(project_t, camera_origin, 'F'), ARM_FIELD(project_t, camera_fov, 'f'), ARM_RECORDS(project_t, swatches, arm_swatch),
           ARM_RECORDS(project_t, brush_nodes, arm_canvas), ARM_FIELD(project_t, brush_icons, 'v'), ARM_RECORDS(project_t, material_nodes, arm_canvas),
           ARM_RECORDS(project_t, material_groups, arm_canvas), ARM_FIELD(project_t, material_icons, 'v'), ARM_RECORDS(project_t, material_datas, arm_material),
           ARM_FIELD(project_t, font_assets, 'S'), ARM_FIELD(project_t, sound_assets, 'S'), ARM_RECORDS(project_t, layer_datas, arm_layer),
           ARM_RECORDS(project_t, mesh_datas, arm_mesh), ARM_FIELD(project_t, mesh_assets, 'S'), ARM_FIELD(project_t, mesh_icons, 'v'),
           ARM_FIELD(project_t, mesh_transforms, 't'), ARM_FIELD(project_t, mesh_materials, 'I'), ARM_FIELD(project_t, mesh_parents, 'I'),
           ARM_FIELD(project_t, mesh_physics_shapes, 'I'), ARM_FIELD(project_t, mesh_physics_masses, 'F'), ARM_FIELD(project_t, mesh_skins, 'v'),
           ARM_FIELD(project_t, atlas_objects, 'I'), ARM_FIELD(project_t, atlas_names, 'S'), ARM_FIELD(project_t, script_datas, 'S'),
           ARM_FIELD(project_t, script_names, 'S'), ARM_FIELD(project_t, timeline_frame_rate, 'i'), ARM_FIELD(project_t, timeline_max_frames, 'i'),
           ARM_RECORDS(project_t, timeline_layers, arm_layer_key), ARM_RECORDS(project_t, timeline_meshes, arm_mesh_key),
           ARM_RECORDS(project_t, stages, arm_stage), ARM_RECORDS(project_t, mesh_nodes, arm_canvas), ARM_FIELD(project_t, mesh_nodes_objects, 'I'),
           ARM_FIELD(project_t, mesh_paths, 't'));

#undef ARM_FIELD
#undef ARM_RECORDS
#undef ARM_SCHEMA

typedef struct {
	const u8 *bytes;
	size_t    length;
	size_t    pos;
	void    **allocations;
	size_t    count;
	size_t    capacity;
} arm_reader_t;

static void *arm_allocate(arm_reader_t *r, size_t size) {
	if (r->count == r->capacity) {
		size_t cap = r->capacity == 0 ? 64 : r->capacity * 2;
		void **p   = realloc(r->allocations, cap * sizeof(void *));
		if (p == NULL)
			return NULL;
		r->allocations = p;
		r->capacity    = cap;
	}
	void *p = calloc(1, size == 0 ? 1 : size);
	if (p != NULL)
		r->allocations[r->count++] = p;
	return p;
}

static bool arm_take(arm_reader_t *r, size_t size, const u8 **bytes) {
	if (size > r->length - r->pos)
		return false;
	*bytes = r->bytes + r->pos;
	r->pos += size;
	return true;
}

static bool arm_count(arm_reader_t *r, u32 *count) {
	const u8 *p;
	if (!arm_take(r, 4, &p))
		return false;
	memcpy(count, p, 4);
	return *count <= INT_MAX;
}

static bool arm_read(arm_reader_t *r, char type, const arm_schema_t *schema, void *out, i32 depth);

static bool arm_read_map(arm_reader_t *r, const arm_schema_t *schema, void *out, i32 depth) {
	u32 count;
	if (!arm_count(r, &count) || count > (r->length - r->pos) / 6)
		return false;
	// Schemas have fewer than 64 fields. Duplicate fields cannot replace owned pointers.
	u64 seen = 0;
	if (schema == &arm_layer && out != NULL) {
		layer_data_t *l = out;
		l->parent = l->fill_material = l->path_material = -1;
	}
	for (u32 i = 0; i < count; ++i) {
		const u8 *tag, *key;
		u32       length;
		if (!arm_take(r, 1, &tag) || *tag != 0xdb || !arm_count(r, &length) || !arm_take(r, length, &key) || memchr(key, 0, length))
			return false;
		const arm_field_t *field = NULL;
		if (schema != NULL) {
			for (i32 j = 0; schema->fields[j].name != NULL; ++j) {
				const arm_field_t *f = &schema->fields[j];
				if (strlen(f->name) == length && memcmp(f->name, key, length) == 0) {
					if ((seen & ((u64)1 << j)) != 0)
						return false;
					seen |= (u64)1 << j;
					field = f;
					break;
				}
			}
		}
		if (!arm_read(r, field == NULL ? 0 : field->type, field == NULL ? NULL : field->child, field == NULL || out == NULL ? NULL : (u8 *)out + field->offset,
		              depth + 1))
			return false;
	}
	return true;
}

static bool arm_matrix(f32_array_t *a, u32 count) {
	return a != NULL && a->length == count;
}

static bool arm_canvases(ui_node_canvas_t_array_t *canvases) {
	if (canvases == NULL)
		return true;
	for (u32 i = 0; i < canvases->length; ++i) {
		ui_node_canvas_t *c = canvases->buffer[i];
		if (c->name == NULL || c->nodes == NULL || c->links == NULL)
			return false;
		for (u32 j = 0; j < c->nodes->length; ++j) {
			ui_node_t *n = c->nodes->buffer[j];
			if (n->name == NULL || n->type == NULL || n->inputs == NULL || n->outputs == NULL || n->buttons == NULL)
				return false;
			for (u32 k = 0; k < j; ++k)
				if (c->nodes->buffer[k]->id == n->id)
					return false;
			for (i32 side = 0; side < 2; ++side) {
				ui_node_socket_t_array_t *sockets = side == 0 ? n->inputs : n->outputs;
				for (u32 k = 0; k < sockets->length; ++k) {
					ui_node_socket_t *s = sockets->buffer[k];
					if (s->name == NULL || s->type == NULL || s->node_id != n->id)
						return false;
					if (side == 0) {
						// Brush vectors can contain two UV components; material vectors
						// contain three. Both have an existing supported representation.
						if (s->default_value == NULL || s->default_value->length == 0)
							return false;
					}
				}
			}
			for (u32 k = 0; k < n->buttons->length; ++k) {
				ui_node_button_t *b = n->buttons->buffer[k];
				if (b->name == NULL || b->type == NULL)
					return false;
			}
			if (string_equals(n->type, "TEX_IMAGE") && (n->buttons->length == 0 || n->buttons->buffer[0]->data == NULL))
				return false;
		}
		for (u32 j = 0; j < c->links->length; ++j) {
			ui_node_link_t *l    = c->links->buffer[j];
			ui_node_t      *from = NULL, *to = NULL;
			for (u32 k = 0; k < c->nodes->length; ++k) {
				ui_node_t *n = c->nodes->buffer[k];
				if (n->id == l->from_id)
					from = n;
				if (n->id == l->to_id)
					to = n;
			}
			if (from == NULL || to == NULL || l->from_socket < 0 || l->to_socket < 0 || l->from_socket >= from->outputs->length ||
			    l->to_socket >= to->inputs->length)
				return false;
		}
	}
	return true;
}

static bool arm_mesh_link(char *name, i32 *index) {
	char *sep = NULL;
	if (name != NULL)
		for (char *c = name; *c != '\0'; ++c)
			if (*c == '@')
				sep = c;
	if (sep == NULL || sep[1] == '\0')
		return false;
	u32  n        = 0;
	bool overflow = false;
	for (char *c = sep + 1; *c != '\0'; ++c) {
		if (*c < '0' || *c > '9')
			return false;
		u32 digit = *c - '0';
		if (n > (INT_MAX - digit) / 10)
			overflow = true;
		if (!overflow)
			n = n * 10 + digit;
	}
	*index = overflow ? -1 : (i32)n;
	return true;
}

static bool arm_meshes(project_t *p) {
	if (p->mesh_datas == NULL || p->mesh_datas->length == 0)
		return false;
	for (u32 i = 0; i < p->mesh_datas->length; ++i) {
		mesh_data_t *m = p->mesh_datas->buffer[i];
		if (m->name == NULL || m->vertex_arrays == NULL || m->index_array == NULL)
			return false;
		i32 source;
		if (p->version != NULL && i > 0 && arm_mesh_link(m->name, &source)) {
			if (source < 0 || source >= p->mesh_datas->length || source == i)
				return false;
			if (source > 0 && arm_mesh_link(p->mesh_datas->buffer[source]->name, &source))
				return false;
			continue;
		}
		if (m->vertex_arrays->length == 0 || m->vertex_arrays->length > GPU_MAX_VERTEX_ELEMENTS || m->index_array->length % 3 != 0)
			return false;
		vertex_array_t *pos = NULL, *nor = NULL, *tex = NULL;
		for (u32 j = 0; j < m->vertex_arrays->length; ++j) {
			vertex_array_t *v = m->vertex_arrays->buffer[j];
			if (v->attrib == NULL || v->data == NULL || v->values == NULL || !string_equals(v->data, "short4norm") && !string_equals(v->data, "short2norm"))
				return false;
			if (string_equals(v->attrib, "pos"))
				pos = v;
			if (string_equals(v->attrib, "nor"))
				nor = v;
			if (string_equals(v->attrib, "tex"))
				tex = v;
			for (u32 k = 0; k < j; ++k)
				if (string_equals(v->attrib, m->vertex_arrays->buffer[k]->attrib))
					return false;
		}
		if (pos == NULL || nor == NULL || tex == NULL || !string_equals(pos->data, "short4norm") || !string_equals(nor->data, "short2norm") ||
		    !string_equals(tex->data, "short2norm") || pos->values->length % 4 != 0)
			return false;
		u32 vertices = pos->values->length / 4;
		for (u32 j = 0; j < m->vertex_arrays->length; ++j) {
			vertex_array_t *v     = m->vertex_arrays->buffer[j];
			u32             width = string_equals(v->data, "short4norm") ? 4 : 2;
			if (v->values->length != (u64)vertices * width)
				return false;
		}
		for (u32 j = 0; j < m->index_array->length; ++j)
			if (m->index_array->buffer[j] >= vertices)
				return false;
	}
	return true;
}

static bool arm_read(arm_reader_t *r, char type, const arm_schema_t *schema, void *out, i32 depth) {
	const u8 *tag, *p;
	if (depth > 64 || !arm_take(r, 1, &tag))
		return false;
	if (*tag == 0xc0)
		return type == 0 || type == 's' || type >= 'A' && type <= 'Z' || type == 'a' || type == 'v' || type == 't';
	if (*tag == 0xdb && (type == 0 || type == 's')) {
		u32 length;
		if (!arm_count(r, &length) || !arm_take(r, length, &p) || memchr(p, 0, length))
			return false;
		if (out != NULL) {
			char *s = arm_allocate(r, (size_t)length + 1);
			if (s == NULL)
				return false;
			memcpy(s, p, length);
			*(char **)out = s;
		}
		return true;
	}
	if ((*tag == 0xc2 || *tag == 0xc3) && (type == 0 || type == 'b')) {
		if (out != NULL)
			*(bool *)out = *tag == 0xc3;
		return true;
	}
	if ((*tag == 0xd2 && (type == 0 || type == 'i' || type == 'b') || *tag == 0xca && (type == 0 || type == 'f')) && arm_take(r, 4, &p)) {
		if (*tag == 0xca) {
			f32 f;
			memcpy(&f, p, 4);
			if (!isfinite(f))
				return false;
		}
		if (type == 'b') {
			i32 i;
			memcpy(&i, p, 4);
			if (i != 0 && i != 1)
				return false;
			if (out != NULL)
				*(bool *)out = i != 0;
		}
		else if (out != NULL)
			memcpy(out, p, 4);
		return true;
	}
	if (*tag == 0xdf && (type == 0 || type == 'o'))
		return arm_read_map(r, schema, out, depth);
	if (*tag != 0xdd || type == 's' || type == 'i' || type == 'f' || type == 'b' || type == 'o')
		return false;
	u32 count;
	if (!arm_count(r, &count))
		return false;
	u8  element  = count == 0 ? 0 : r->pos < r->length ? r->bytes[r->pos] : 0;
	u32 width    = element == 0xca || element == 0xd2 ? 4 : element == 0xd1 ? 2 : element == 0xc4 ? 1 : 0;
	u8  expected = type == 'F'                  ? 0xca
	               : type == 'I'                ? 0xd2
	               : type == 'H'                ? 0xd1
	               : type == 'B'                ? 0xc4
	               : type == 'S'                ? 0xdb
	               : type == 'a'                ? 0xdf
	               : type == 'v' || type == 't' ? 0xdd
	                                            : element;
	if (count > 0 && (element != expected || element != 0xdf && element != 0xdb && element != 0xdd && width == 0))
		return false;
	size_t bytes = (size_t)count * (width > 0 ? width : sizeof(void *));
	if (count > 0 && (width > 0 ? (size_t)count * width + 1 > r->length - r->pos : count > (r->length - r->pos) / 5))
		return false;
	any_array_t *a = out == NULL ? NULL : arm_allocate(r, sizeof(any_array_t));
	if (out != NULL) {
		if (a == NULL || bytes > 0 && (a->buffer = arm_allocate(r, bytes)) == NULL)
			return false;
		a->length = a->capacity = count;
		*(any_array_t **)out    = a;
	}
	if (width > 0 && count > 0) {
		r->pos++;
		if (!arm_take(r, (size_t)count * width, &p))
			return false;
		if (element == 0xca)
			for (u32 i = 0; i < count; ++i) {
				f32 f;
				memcpy(&f, p + i * 4, 4);
				if (!isfinite(f))
					return false;
			}
		if (a != NULL)
			memcpy(a->buffer, p, bytes);
	}
	else
		for (u32 i = 0; i < count; ++i) {
			void *item = NULL;
			if (r->pos >= r->length || r->bytes[r->pos] != element)
				return false;
			if (a != NULL && element == 0xdf && (item = arm_allocate(r, schema->size)) == NULL)
				return false;
			if (!arm_read(r,
			              element == 0xdf   ? 'o'
			              : element == 0xdb ? 's'
			              : type == 'v'     ? 'B'
			              : type == 't'     ? 'F'
			                                : 0,
			              schema,
			              a == NULL         ? NULL
			              : element == 0xdf ? item
			                                : &item,
			              depth + 1))
				return false;
			if (a != NULL)
				a->buffer[i] = item;
		}
	return true;
}

static bool arm_layer_pixels(buffer_t *base, buffer_t *nor, buffer_t *pack, u32 size, bool optional) {
	if (base == NULL)
		return optional && nor == NULL && pack == NULL;
	return lz4_validate(base, size) && (nor == NULL || lz4_validate(nor, size)) && (pack == NULL || lz4_validate(pack, size));
}

static bool arm_validate_project(project_t *p) {
	if (!arm_canvases(p->material_nodes) || !arm_canvases(p->material_groups) || !arm_canvases(p->brush_nodes) || !arm_canvases(p->mesh_nodes))
		return false;
	if (p->camera_world != NULL && (!arm_matrix(p->camera_world, 16) || !arm_matrix(p->camera_origin, 3)))
		return false;
	if (p->packed_assets != NULL)
		for (u32 i = 0; i < p->packed_assets->length; ++i) {
			packed_asset_t *a = p->packed_assets->buffer[i];
			if (a->name == NULL || a->bytes == NULL || ((buffer_t *)a->bytes)->length == 0)
				return false;
		}
	if (p->version == NULL)
		return p->assets == NULL && p->layer_datas == NULL && p->material_nodes == NULL && p->brush_nodes == NULL && p->script_datas == NULL && arm_meshes(p);
	if (p->material_datas != NULL && (p->material_nodes == NULL || p->material_datas->length != p->material_nodes->length))
		return false;
	if (p->layer_datas == NULL)
		return p->mesh_datas == NULL && (p->material_nodes != NULL || p->brush_nodes != NULL && p->assets != NULL || p->swatches != NULL);
	if (p->assets == NULL)
		return false;
	if (!arm_meshes(p) || p->layer_datas->length == 0 || p->material_nodes == NULL || p->material_nodes->length == 0 || p->brush_nodes == NULL ||
	    p->brush_nodes->length == 0 || p->material_datas == NULL || p->material_datas->length != p->material_nodes->length || p->mesh_transforms == NULL ||
	    p->mesh_transforms->length != p->mesh_datas->length)
		return false;
	u32 meshes = p->mesh_datas->length, materials = p->material_nodes->length;
	for (u32 i = 0; i < meshes; ++i) {
		if (!arm_matrix(p->mesh_transforms->buffer[i], 16))
			return false;
		if (p->mesh_materials != NULL &&
		    (p->mesh_materials->length != meshes || p->mesh_materials->buffer[i] < -1 || p->mesh_materials->buffer[i] >= (i32)materials))
			return false;
		if (p->mesh_parents != NULL && p->mesh_parents->length != meshes)
			return false;
		if (p->mesh_parents != NULL) {
			i32 parent = p->mesh_parents->buffer[i];
			for (u32 steps = 0; parent >= 0; ++steps) {
				if (parent >= meshes || parent == i || steps >= meshes)
					return false;
				parent = p->mesh_parents->buffer[parent];
			}
		}
	}
	if (p->script_names != NULL && (p->script_datas == NULL || p->script_names->length != p->script_datas->length))
		return false;
	if (p->mesh_nodes != NULL && (p->mesh_nodes_objects == NULL || p->mesh_nodes_objects->length != p->mesh_nodes->length))
		return false;
	if (p->mesh_nodes_objects != NULL)
		for (u32 i = 0; i < p->mesh_nodes_objects->length; ++i)
			if (p->mesh_nodes_objects->buffer[i] < 0 || p->mesh_nodes_objects->buffer[i] >= meshes)
				return false;
	layer_data_t *first = p->layer_datas->buffer[0];
	if (first->res <= 0 || (first->bpp != 8 && first->bpp != 16 && first->bpp != 32) || (u64)first->res * first->res > INT_MAX / (4 * (first->bpp / 8)))
		return false;
	u32 pixels = first->res * first->res * 4, size = pixels * (first->bpp / 8);
	for (u32 i = 0; i < p->layer_datas->length; ++i) {
		layer_data_t *l    = p->layer_datas->buffer[i];
		bool          mask = l->texpaint != NULL && l->texpaint_nor == NULL;
		if (l->res != first->res || l->bpp != first->bpp || l->parent < -1 || l->parent >= (i32)p->layer_datas->length || l->fill_material < -1 ||
		    l->fill_material >= (i32)materials || l->path_material < -1 || l->path_material >= (i32)materials || l->object_mask < 0 ||
		    l->object_mask > meshes || l->decal_mat != NULL && !arm_matrix(l->decal_mat, 16))
			return false;
		i32 parent = l->parent;
		for (u32 steps = 0; parent >= 0; ++steps) {
			if (parent == i || steps >= p->layer_datas->length)
				return false;
			parent = p->layer_datas->buffer[parent]->parent;
			if (parent >= (i32)p->layer_datas->length)
				return false;
		}
		if (!arm_layer_pixels(l->texpaint, l->texpaint_nor, l->texpaint_pack, mask ? pixels : size, true) ||
		    !mask && l->texpaint != NULL && l->texpaint_pack == NULL || mask && (l->texpaint_pack != NULL || l->texpaint_sculpt != NULL) ||
		    l->texpaint_sculpt != NULL && (pixels > INT_MAX / 4 || !lz4_validate(l->texpaint_sculpt, pixels * 4)))
			return false;
	}
	if (p->timeline_layers != NULL)
		for (u32 i = 0; i < p->timeline_layers->length; ++i) {
			timeline_layer_keyframe_data_t *k = p->timeline_layers->buffer[i];
			if (k->layer_index < 0 || k->layer_index >= p->layer_datas->length || k->frame < 0 ||
			    !arm_layer_pixels(k->texpaint, k->texpaint_nor, k->texpaint_pack, size, true) ||
			    k->texpaint_sculpt != NULL && (pixels > INT_MAX / 4 || !lz4_validate(k->texpaint_sculpt, pixels * 4)))
				return false;
		}
	if (p->timeline_meshes != NULL)
		for (u32 i = 0; i < p->timeline_meshes->length; ++i) {
			timeline_mesh_keyframe_data_t *k = p->timeline_meshes->buffer[i];
			if (k->mesh_index < -1 || k->mesh_index >= (i32)meshes || !arm_matrix(k->transform, 16) || k->frame < 0 || k->stage_index < 0 ||
			    k->stage_index > (p->stages == NULL ? 0 : p->stages->length))
				return false;
		}
	if (p->stages != NULL)
		for (u32 i = 0; i < p->stages->length; ++i) {
			stage_t *s = p->stages->buffer[i];
			if (s->name == NULL || s->objects == NULL || s->layers == NULL)
				return false;
		}
	return true;
}

// Keep the compatibility defaults in import_legacy.c, using checked records
// instead of casting untyped map values while converting older projects.
static bool arm_upgrade(arm_reader_t *r, project_t *p, i32 v) {
	if (v < 19)
		p->mesh_paths = NULL;
	if (v < 18) {
		p->mesh_nodes         = NULL;
		p->mesh_nodes_objects = NULL;
	}
	if (v < 17 && p->timeline_layers != NULL)
		for (u32 i = 0; i < p->timeline_layers->length; ++i)
			p->timeline_layers->buffer[i]->texpaint_sculpt = NULL;
	if (v < 16 && p->timeline_meshes != NULL)
		for (u32 i = 0; i < p->timeline_meshes->length; ++i) {
			timeline_mesh_keyframe_data_t *k = p->timeline_meshes->buffer[i];
			k->stage_index                   = k->mesh_index == -1 && p->stages != NULL && p->stages->length > 0 ? 1 : 0;
		}
	if (p->stages != NULL)
		for (u32 i = 0; i < p->stages->length; ++i) {
			if (v < 16)
				p->stages->buffer[i]->nested_mesh = NULL;
			if (v < 15)
				p->stages->buffer[i]->hidden = NULL;
		}
	if (v < 13)
		p->mesh_skins = NULL;
	if (v < 12) {
		p->mesh_physics_shapes = NULL;
		p->mesh_physics_masses = NULL;
	}
	if (v < 11)
		p->sound_assets = NULL;
	if (v < 9) {
		p->mesh_materials = NULL;
		p->mesh_parents   = NULL;
		p->script_names   = NULL;
		p->stages         = NULL;
	}
	if (v < 8) {
		p->timeline_frame_rate = 24;
		p->timeline_max_frames = 200;
		p->timeline_layers     = NULL;
		p->timeline_meshes     = NULL;
	}
	if (p->layer_datas != NULL)
		for (u32 i = 0; i < p->layer_datas->length; ++i) {
			layer_data_t *l = p->layer_datas->buffer[i];
			if (v < 14)
				l->path_text = false;
			if (v < 10)
				l->texpaint_sculpt = NULL;
			if (v < 7) {
				l->path_points = l->path_points_world = l->path_points_camera = NULL;
				l->path_points_parent                                         = NULL;
				l->path_tool                                                  = 0;
				l->path_curved                                                = false;
				l->path_material                                              = -1;
			}
			if (v < 3)
				l->uv_map = 0;
		}
	if (v < 7 && p->material_nodes != NULL) {
		material_data2_t_array_t *a = arm_allocate(r, sizeof(any_array_t));
		if (a == NULL)
			return false;
		a->length = a->capacity = p->material_nodes->length;
		a->buffer               = arm_allocate(r, a->length * sizeof(void *));
		if (a->buffer == NULL)
			return false;
		for (u32 i = 0; i < a->length; ++i) {
			material_data2_t *m = arm_allocate(r, sizeof(material_data2_t));
			if (m == NULL)
				return false;
			m->paint_base = m->paint_opac = m->paint_occ = m->paint_rough = m->paint_met = m->paint_nor = m->paint_height = m->paint_emis = m->paint_subs =
			    true;
			a->buffer[i] = m;
		}
		p->material_datas = a;
	}
	if (v < 6 && p->mesh_datas != NULL) {
		f32_array_t_array_t *a = arm_allocate(r, sizeof(any_array_t));
		if (a == NULL)
			return false;
		a->length = a->capacity = p->mesh_datas->length;
		a->buffer               = arm_allocate(r, a->length * sizeof(void *));
		if (a->buffer == NULL)
			return false;
		for (u32 i = 0; i < a->length; ++i) {
			f32_array_t *m = arm_allocate(r, sizeof(f32_array_t));
			if (m == NULL)
				return false;
			m->length = m->capacity = 16;
			m->buffer               = arm_allocate(r, 16 * sizeof(f32));
			if (m->buffer == NULL)
				return false;
			for (i32 j = 0; j < 4; ++j)
				m->buffer[j * 5] = 1.0f;
			a->buffer[i] = m;
		}
		p->mesh_transforms = a;
	}
	if (v < 5 && p->camera_world != NULL) {
		if (!arm_matrix(p->camera_world, 16))
			return false;
		for (i32 i = 0; i < 4; ++i)
			for (i32 j = i + 1; j < 4; ++j) {
				f32 f                              = p->camera_world->buffer[i * 4 + j];
				p->camera_world->buffer[i * 4 + j] = p->camera_world->buffer[j * 4 + i];
				p->camera_world->buffer[j * 4 + i] = f;
			}
	}
	if (v < 4) {
		p->envmap_angle = 0;
		p->envmap_blur  = false;
	}
	return true;
}

project_t *import_arm_decode_project(buffer_t *b) {
	if (b == NULL || b->buffer == NULL || b->length < 5)
		return NULL;
	arm_reader_t r = {.bytes = b->buffer, .length = b->length};
	project_t   *p = arm_allocate(&r, sizeof(project_t));
	// Existing .arm writers include one zero byte after the encoded map.
	bool ok = p != NULL && arm_read(&r, 'o', &arm_project, p, 0) && (r.pos == r.length || r.length - r.pos == 1 && r.bytes[r.pos] == 0);
	if (ok && p->version != NULL) {
		char *end;
		long  v = strtol(p->version, &end, 10);
		ok      = p->version[0] >= '0' && p->version[0] <= '9' && *end == '\0' && v >= 0 && v <= strtol(manifest_version_project, NULL, 10);
		if (ok)
			ok = arm_upgrade(&r, p, v);
	}
	if (ok)
		ok = arm_validate_project(p);
	if (!ok)
		for (size_t i = 0; i < r.count; ++i)
			free(r.allocations[i]);
	free(r.allocations);
	return ok ? p : NULL;
}
