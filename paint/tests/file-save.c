// Compile the production writer with deterministic I/O failures at its OS seams.
#include "../../base/sources/backends/windows_mini.h"
#include "../../base/sources/iron_file.h"
#include <errno.h>
#include <io.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <wchar.h>

enum {
	NONE,
	OPEN,
	SHORT_WRITE,
	FLUSH,
	SYNC,
	CLOSE,
	REPLACE,
	INTERRUPT
};
static int     fault;
static int     closes;
static int     collisions;
static wchar_t last_temp[4096];
static wchar_t collision_temp[4096];

static FILE *test_open(const wchar_t *path, const wchar_t *mode) {
	if (wcsstr(path, L".tmp.") != NULL && wcscmp(mode, L"wbx") == 0) {
		wcscpy(last_temp, path);
		if (fault == OPEN) {
			errno = EACCES;
			return NULL;
		}
		if (collisions > 0) {
			collisions--;
			FILE *stale = _wfopen(path, L"wb");
			if (stale == NULL) {
				return NULL;
			}
			fputs("stale", stale);
			fclose(stale);
			wcscpy(collision_temp, path);
		}
	}
	return _wfopen(path, mode);
}

static size_t test_write(const void *data, size_t size, size_t count, FILE *file) {
	if (fault == INTERRUPT) {
		fwrite(data, size, count / 2, file);
		fflush(file);
		_exit(73); // Simulate termination during the sibling write, before close/replace.
	}
	return fwrite(data, size, fault == SHORT_WRITE && count > 0 ? count - 1 : count, file);
}

static int test_flush(FILE *file) {
	int result = fflush(file);
	return fault == FLUSH ? EOF : result;
}

static int test_sync(int fd) {
	return fault == SYNC ? -1 : _commit(fd);
}

static int test_close(FILE *file) {
	closes++;
	int result = fclose(file);
	return fault == CLOSE ? EOF : result;
}

static BOOL WINAPI test_replace(LPCWSTR from, LPCWSTR to, DWORD flags) {
	return fault == REPLACE ? 0 : MoveFileExW(from, to, flags);
}

#define _wfopen     test_open
#define fwrite      test_write
#define fflush      test_flush
#define _commit     test_sync
#define fclose      test_close
#define MoveFileExW test_replace
#include "../../base/sources/iron_file_write.c"
#undef _wfopen
#undef fwrite
#undef fflush
#undef _commit
#undef fclose
#undef MoveFileExW

static int checks;
static int failures;

static void check(bool ok, const char *name) {
	checks++;
	if (!ok) {
		failures++;
	}
	printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

static bool contents(wchar_t *path, const char *expected, size_t length) {
	FILE *file = _wfopen(path, L"rb");
	if (file == NULL) {
		return false;
	}
	char   bytes[64];
	size_t n = fread(bytes, 1, sizeof(bytes), file);
	fclose(file);
	return n == length && memcmp(bytes, expected, length) == 0;
}

static void seed(wchar_t *path) {
	FILE *file = _wfopen(path, L"wb");
	if (file == NULL || fwrite("previous project", 1, 16, file) != 16 || fclose(file) != 0) {
		fprintf(stderr, "Fixture setup failed\n");
		exit(2);
	}
}

static bool temp_removed(void) {
	FILE *file = _wfopen(last_temp, L"rb");
	if (file != NULL) {
		fclose(file);
		return false;
	}
	return true;
}

int main(int argc, char **argv) {
	if (argc != 2 && argc != 3) {
		return 2;
	}
	char path[4096];
	snprintf(path, sizeof(path), "%s/save space \xc3\xa9.arm", argv[1]);
	wchar_t *wide = iron_file_write_wpath(path);
	buffer_t data = {.buffer = (u8 *)"new project", .length = 11};
	if (argc == 3 && strcmp(argv[2], "--interrupt") == 0) {
		seed(wide);
		fault = INTERRUPT;
		iron_file_save_bytes_atomic(path, &data, 0);
		return 2; // The interrupted writer must never get here.
	}
	check(iron_file_save_bytes_atomic(path, &data, 0) && contents(wide, "new project", 11), "create exact bytes with spaces and UTF-8");
	check(iron_file_save_bytes_atomic(path, &data, 999) && contents(wide, "new project", 11), "replace and clamp length to buffer");
	check(iron_file_save_bytes_atomic(path, &data, 3) && contents(wide, "new", 3), "explicit partial length");
	buffer_t empty = {0};
	check(iron_file_save_bytes_atomic(path, &empty, 0) && contents(wide, "", 0), "empty buffer");
	check(!iron_file_save_bytes_atomic(NULL, &data, 0) && !iron_file_save_bytes_atomic(path, NULL, 0), "invalid inputs fail");

	const char *names[] = {"", "open failure", "short write", "flush failure", "sync failure", "close failure", "replace failure"};
	for (int f = OPEN; f <= REPLACE; ++f) {
		seed(wide);
		fault      = f;
		closes     = 0;
		bool saved = iron_file_save_bytes_atomic(path, &data, 0);
		fault      = NONE;
		check(!saved && contents(wide, "previous project", 16) && temp_removed() && closes == (f == OPEN ? 0 : 1), names[f]);
	}

	seed(wide);
	HANDLE locked = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	check(locked != INVALID_HANDLE_VALUE, "lock fixture acquired");
	check(!iron_file_save_bytes_atomic(path, &data, 0) && contents(wide, "previous project", 16) && temp_removed(),
	      "locked destination retains previous bytes");
	CloseHandle(locked);
	check(_wchmod(wide, _S_IREAD) == 0, "read-only fixture set");
	check(!iron_file_save_bytes_atomic(path, &data, 0) && contents(wide, "previous project", 16) && temp_removed(),
	      "read-only destination retains previous bytes");
	check(_wchmod(wide, _S_IREAD | _S_IWRITE) == 0, "read-only fixture restored");

	collisions = 1;
	check(iron_file_save_bytes_atomic(path, &data, 0) && contents(collision_temp, "stale", 5) && contents(wide, "new project", 11),
	      "exclusive sibling creation preserves a collision");
	_wremove(collision_temp);
	check(temp_removed(), "successful replacement leaves no temporary file");

	char missing[4096];
	snprintf(missing, sizeof(missing), "%s/missing/project.arm", argv[1]);
	check(!iron_file_save_bytes_atomic(missing, &data, 0), "missing parent reports failure");
	for (int f = SHORT_WRITE; f <= CLOSE; ++f) {
		fault      = f;
		bool saved = iron_file_save_bytes(path, &data, 0);
		fault      = NONE;
		if (f != SYNC) {
			check(!saved, names[f]);
		}
	}
	check(!iron_file_save_bytes(missing, &data, 0), "direct writer reports open failure");
	free(wide);
	printf("FILE SAVE: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
