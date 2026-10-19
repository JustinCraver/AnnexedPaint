#include "../plugins/io_gltf/cgltf.c"
#include <stdio.h>

static raw_mesh_t *read_mesh(char *path) {
	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	long length = ftell(f);
	rewind(f);
	char *bytes = malloc(length);
	fread(bytes, 1, length, f);
	fclose(f);
	raw_mesh_t *m = io_gltf_parse(bytes, length, path);
	free(bytes);
	return m;
}
void iron_log(const char *format, ...) {
	puts(format);
}
buffer_t *util_skin_blob_create(char *name, int vc, float *pos, float *nor, float *tex, int ic, uint32_t *ind, uint16_t *joints, float *weights, int jc, int fc,
                                float *mats) {
	return NULL;
}

int main(int argc, char **argv) {
	if (argc < 3)
		return 2;
	if (argc > 3 && read_mesh(argv[3]) != NULL) {
		puts("FAIL malformed glTF accepted");
		return 1;
	}
	raw_mesh_t *a = read_mesh(argv[1]);
	if (a == NULL || a->posa == NULL || a->inda->length != 3 || !a->has_next || strcmp(a->name, "MeshA") != 0)
		return 1;
	raw_mesh_t *b = read_mesh(argv[1]);
	if (b == NULL || b->posa == NULL || b->inda->length != 3 || b->has_next || strcmp(b->name, "MeshB") != 0)
		return 1;
	if (a->posa->length != 12 || b->posa->length != 12 || a->inda->buffer[2] != 2 || b->inda->buffer[2] != 2 || b->scale_pos <= a->scale_pos)
		return 1;
	puts("GLTF IMPORT PASS: rejection resets reader; both object names, triangles and transformed bounds retained");
	return 0;
}
