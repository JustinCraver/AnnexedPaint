// Interpreter tests with controlled host bindings, without an editor or GPU.
// Engine math uses production code. Scene lookup and sprintf are test doubles;
// this harness checks the VM's native ABI, not the application's API registry.
#include "engine.h"
#include "minic.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static transform_t fixture_transform;
static object_t    fixture_object;

void console_log(char *message) {
	puts(message);
}

static minic_val_t scene_fixture(minic_val_t *args, int argc) {
	char *name = minic_arg_p(args, argc, 0);
	return minic_val_ptr(name != NULL && strcmp(name, "Scene") == 0 ? &fixture_object : NULL);
}

static minic_val_t sprintf_fixture(minic_val_t *args, int argc) {
	char *format = minic_arg_p(args, argc, 1);
	if (argc != 4 || format == NULL || strcmp(format, "%s %d") != 0) {
		return minic_val_int(-1);
	}
	return minic_val_int(snprintf(minic_arg_p(args, argc, 0), 16, "%s %d", (char *)minic_arg_p(args, argc, 2), minic_arg_i(args, argc, 3)));
}

#define MATH_RESULT(type, expression)         \
	type *result = minic_alloc(sizeof(type)); \
	*result      = expression;                \
	return minic_val_ptr(result)

static minic_val_t multiply_vector(minic_val_t *args, int argc) {
	MATH_RESULT(vec4_t, vec4_mult(*(vec4_t *)minic_arg_p(args, argc, 0), minic_arg_f(args, argc, 1)));
}
static minic_val_t identity4(minic_val_t *args, int argc) {
	MATH_RESULT(mat4_t, mat4_identity());
}
static minic_val_t identity3(minic_val_t *args, int argc) {
	MATH_RESULT(mat3_t, mat3_identity());
}
static minic_val_t multiply_matrix(minic_val_t *args, int argc) {
	MATH_RESULT(mat4_t, mat4_mult_mat(*(mat4_t *)minic_arg_p(args, argc, 0), *(mat4_t *)minic_arg_p(args, argc, 1)));
}

void minic_register_builtins(void) {
	// Registration is idempotent and called for every evaluated test.
	minic_struct_begin("vec2_t", sizeof(vec2_t), MINIC_ALIGNOF(vec2_t));
	minic_struct_field("x", offsetof(vec2_t, x), MINIC_T_FLOAT, MINIC_T_FLOAT, NULL);
	minic_struct_field("y", offsetof(vec2_t, y), MINIC_T_FLOAT, MINIC_T_FLOAT, NULL);
	minic_struct_begin("vec4_t", sizeof(vec4_t), MINIC_ALIGNOF(vec4_t));
	const char *xyzw[] = {"x", "y", "z", "w"};
	for (int i = 0; i < 4; ++i) {
		minic_struct_field(xyzw[i], i * sizeof(float), MINIC_T_FLOAT, MINIC_T_FLOAT, NULL);
	}
	const char *fields[] = {"m00", "m01", "m02", "m03", "m10", "m11", "m12", "m13", "m20", "m21", "m22", "m23", "m30", "m31", "m32", "m33"};
	minic_struct_begin("mat4_t", sizeof(mat4_t), MINIC_ALIGNOF(mat4_t));
	for (int i = 0; i < 16; ++i) {
		minic_struct_field(fields[i], i * sizeof(float), MINIC_T_FLOAT, MINIC_T_FLOAT, NULL);
	}
	minic_struct_begin("mat3_t", sizeof(mat3_t), MINIC_ALIGNOF(mat3_t));
	const char *small[] = {"m00", "m01", "m02", "m10", "m11", "m12", "m20", "m21", "m22"};
	for (int i = 0; i < 9; ++i) {
		minic_struct_field(small[i], i * sizeof(float), MINIC_T_FLOAT, MINIC_T_FLOAT, NULL);
	}
	minic_struct_begin("transform_t", sizeof(transform_t), MINIC_ALIGNOF(transform_t));
	minic_struct_field("loc", offsetof(transform_t, loc), MINIC_T_EMBED, MINIC_T_EMBED, "vec4_t");
	minic_struct_begin("object_t", sizeof(object_t), MINIC_ALIGNOF(object_t));
	minic_struct_field("transform", offsetof(object_t, transform), MINIC_T_PTR, MINIC_T_PTR, "transform_t");
	minic_register("scene_get_child", "p:object_t(p)", scene_fixture);
	minic_register_native("sprintf", sprintf_fixture);
	minic_register("vec4_mult", "s:vec4_t(s:vec4_t,f)", multiply_vector);
	minic_register("mat4_identity", "s:mat4_t()", identity4);
	minic_register("mat3_identity", "s:mat3_t()", identity3);
	minic_register("mat4_mult_mat", "s:mat4_t(s:mat4_t,s:mat4_t)", multiply_matrix);
}

extern const char *test0;

int main(int argc, char **argv) {
	if (argc > 2 || (argc == 2 && strcmp(argv[1], "--inject-failure") != 0)) {
		return 2;
	}
	if (argc == 2) {
		test0 = "float main() { return 1; }";
	}
	fixture_object.transform = &fixture_transform;
	return minic_tests() == 0 ? 0 : 1;
}
