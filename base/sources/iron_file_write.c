#include "iron_file.h"
#include <stdio.h>
#include <string.h>
#ifndef IRON_WASM
#include <errno.h>
#ifdef IRON_WINDOWS
#include "backends/windows_mini.h"
#include <io.h>
#include <process.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#endif

static bool iron_file_write_checked(FILE *file, buffer_t *bytes, u64 length, bool sync) {
	u64  byte_length = length > 0 && length < bytes->length ? length : bytes->length;
	bool ok          = fwrite(bytes->buffer, 1, (size_t)byte_length, file) == byte_length;
#ifndef IRON_WASM
	if (ferror(file) || fflush(file) != 0) {
		ok = false;
	}
	if (ok && sync) {
#ifdef IRON_WINDOWS
		ok = _commit(_fileno(file)) == 0;
#else
		ok = fsync(fileno(file)) == 0;
#endif
	}
#else
	(void)sync;
#endif
	// Always close, including a stream that failed during write or flush.
	if (fclose(file) != 0) {
		ok = false;
	}
	return ok;
}

static bool iron_file_write_valid(char *path, buffer_t *bytes) {
	return path != NULL && path[0] != 0 && bytes != NULL && (bytes->length == 0 || bytes->buffer != NULL);
}

#ifdef IRON_WINDOWS
static wchar_t *iron_file_write_wpath(char *path) {
	i32 count = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
	if (count == 0) {
		return NULL;
	}
	wchar_t *wide = malloc(count * sizeof(wchar_t));
	if (wide != NULL && MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, count) == 0) {
		free(wide);
		return NULL;
	}
	return wide;
}
#endif

bool iron_file_save_bytes(char *path, buffer_t *bytes, u64 length) {
	if (!iron_file_write_valid(path, bytes)) {
		return false;
	}
#ifdef IRON_WINDOWS
	wchar_t *wide = iron_file_write_wpath(path);
	FILE    *file = wide != NULL ? _wfopen(wide, L"wb") : NULL;
	free(wide);
#else
	FILE *file = fopen(path, "wb");
#endif
	return file != NULL && iron_file_write_checked(file, bytes, length, false);
}

bool iron_file_save_bytes_atomic(char *path, buffer_t *bytes, u64 length) {
#ifdef IRON_WASM
	// Browser writes have their own routing and asynchronous completion contract.
	// A synchronous atomic save cannot confirm a disk download or cloud upload.
	(void)path;
	(void)bytes;
	(void)length;
	return false;
#else
	if (!iron_file_write_valid(path, bytes)) {
		return false;
	}
	static u32 serial = 0;
	char      *temp   = malloc(strlen(path) + 64);
	if (temp == NULL) {
		return false;
	}
	FILE *file = NULL;
#ifdef IRON_WINDOWS
	wchar_t *wide  = iron_file_write_wpath(path);
	wchar_t *wtemp = NULL;
	if (wide == NULL) {
		free(temp);
		return false;
	}
#endif
	// Exclusive creation protects leftovers and other writers. Never truncate an
	// existing temporary file, and never fall back to truncating the destination.
	for (i32 attempt = 0; attempt < 32; ++attempt) {
#ifdef IRON_WINDOWS
		snprintf(temp, strlen(path) + 64, "%s.tmp.%u.%u", path, (u32)_getpid(), ++serial);
		free(wtemp);
		wtemp = iron_file_write_wpath(temp);
		file  = wtemp != NULL ? _wfopen(wtemp, L"wbx") : NULL;
#else
		snprintf(temp, strlen(path) + 64, "%s.tmp.%u.%u", path, (u32)getpid(), ++serial);
		file = fopen(temp, "wbx");
#endif
		if (file != NULL || errno != EEXIST) {
			break;
		}
	}
	bool ok = false;
	if (file != NULL) {
#ifndef IRON_WINDOWS
		struct stat previous;
		if (stat(path, &previous) == 0 && fchmod(fileno(file), previous.st_mode & 0777) != 0) {
			fclose(file);
		}
		else
#endif
		{
			ok = iron_file_write_checked(file, bytes, length, true);
		}
		if (ok) {
#ifdef IRON_WINDOWS
			ok = MoveFileExW(wtemp, wide, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
			ok = rename(temp, path) == 0;
#endif
		}
		if (!ok) {
#ifdef IRON_WINDOWS
			_wremove(wtemp);
#else
			remove(temp);
#endif
		}
	}
#ifdef IRON_WINDOWS
	free(wtemp);
	free(wide);
#endif
	free(temp);
	return ok;
#endif
}
