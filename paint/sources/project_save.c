// Included by project.c; kept separate so the save lifecycle can be tested
// without linking an editor, renderer, or application entry point.
#include "functions.h"
#include <iron_file.h>
#include <iron_system.h>

extern project_t *g_project;
extern config_t  *g_config;
extern bool       agent_running;
extern char      *manifest_title;
extern char      *ui_files_filename;

static bool _project_save_and_quit;
static bool _project_save_pending;
static u32  _project_save_generation;
typedef struct project_save_data {
	project_t *project;
	char      *path;
	bool       save_and_quit;
	u32        generation;
} project_save_data_t;

void project_save_on_next_frame(void *data) {
	project_save_data_t *save = data;
	_project_save_pending     = false;
	// The request belongs to the project and destination selected when queued.
	if (save->project != g_project || save->generation != _project_save_generation || !export_arm_run_project(save->path)) {
		free(save->path);
		free(save);
		return;
	}

	g_project->_->filepath = save->path;
	ui_files_filename      = string_copy(substring(save->path, string_last_index_of(save->path, PATH_SEP) + 1, string_length(save->path) - 4));
#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS) || defined(IRON_WASM)
	sys_title_set(string("%s - %s", ui_files_filename, manifest_title));
#endif

#ifdef IRON_IOS
	char *recent_path = substring(save->path, string_last_index_of(save->path, "/") + 1, string_length(save->path));
#else
	char *recent_path = save->path;
#endif
#ifdef IRON_WINDOWS
	recent_path = string_copy(string_replace_all(recent_path, "\\", "/"));
#endif
	string_array_t *recent = g_config->recent_projects;
	string_array_remove(recent, recent_path);
	array_insert((any_array_t *)recent, 0, recent_path);
	config_save();
	console_info(tr("Project saved"));
	bool quit = save->save_and_quit;
	free(save);
	if (quit) {
		iron_stop();
	}
}

void project_save_to(char *path, bool save_and_quit) {
	if (_project_save_pending || agent_running) {
		return;
	}
	project_save_data_t *save =
	    ALLOC_INIT(project_save_data_t,
	               {.project = g_project, .path = string_copy(path), .save_and_quit = save_and_quit, .generation = _project_save_generation});
	_project_save_pending = true;
	sys_notify_on_next_frame(&project_save_on_next_frame, save);
}

void project_save(bool save_and_quit) {
	if (agent_running || _project_save_pending) {
		return;
	}

#ifdef IRON_WASM
	if (!box_projects_is_cloud_path(g_project->_->filepath) && !starts_with(g_project->_->filepath, "/files/")) {
		box_projects_cloud_save_show(save_and_quit);
		return;
	}
#endif

	if (string_equals(g_project->_->filepath, "")) {
#ifdef IRON_IOS
		char *document_directory = iron_save_dialog("", "");
		if (document_directory == NULL) {
			return;
		}
		document_directory = string_copy(substring(document_directory, 0, string_length(document_directory) - 8)); // Strip /"untitled"
		project_save_to(string("%s/%s.arm", document_directory, sys_title()), save_and_quit);
		return;
#elif defined(IRON_ANDROID)
		project_save_to(string("%s/%s.arm", iron_internal_save_path(), sys_title()), save_and_quit);
		return;
#else
		project_save_as(save_and_quit);
		return;
#endif
	}

	project_save_to(g_project->_->filepath, save_and_quit);
}

void project_save_as_on_file_picked(char *path) {
	char *f = ui_files_filename;
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
	char *destination = string("%s%s%s", path, PATH_SEP, f);
	if (!ends_with(destination, ".arm")) {
		destination = string("%s.arm", destination);
	}
	project_save_to(destination, _project_save_and_quit);
}

void project_save_as(bool save_and_quit) {
	if (agent_running || _project_save_pending) {
		return;
	}
	char *filename         = ui_files_filename;
	_project_save_and_quit = save_and_quit;
	ui_files_show("arm", true, false, &project_save_as_on_file_picked);
	ui_files_filename      = filename;
	_project_save_and_quit = false;
}
