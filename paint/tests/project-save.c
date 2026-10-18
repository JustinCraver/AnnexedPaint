// Exercise the real deferred save and Save As callbacks with controlled writers
// and dialogs. No editor, renderer or application entry point is linked.
#include "../sources/project_save.c"
#include <stdio.h>

project_t *g_project;
config_t  *g_config;
bool       agent_running;
char      *manifest_title = "ArmorPaint";
char      *ui_files_filename;

static void (*queued)(void *);
static void *queued_data;
static bool  write_result;
static int   writes;
static int   quits;
static int   config_writes;
static int   saved_infos;
static char *title;
static char *written_path;
static char *picked_path;
static char *picked_name;
static int   checks;
static int   failures;

bool export_arm_run_project(char *path) {
	writes++;
	written_path = string_copy(path);
	return write_result;
}

void sys_notify_on_next_frame(void (*fn)(void *), void *data) {
	queued      = fn;
	queued_data = data;
}

void iron_stop(void) {
	quits++;
}

char *sys_title(void) {
	return title;
}

void sys_title_set(char *value) {
	title = string_copy(value);
}

char *tr(char *text) {
	return text;
}

void config_save(void) {
	config_writes++;
}

void console_info(char *text) {
	if (string_equals(text, "Project saved")) {
		saved_infos++;
	}
}

void ui_files_show(char *filters, bool save, bool multiple, void (*done)(char *)) {
	if (picked_path != NULL) {
		ui_files_filename = picked_name;
		done(picked_path);
	}
}

static void check(bool ok, char *name) {
	checks++;
	if (!ok) {
		failures++;
	}
	printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

static void reset(void) {
	static project_t         project;
	static project_runtime_t runtime;
	static config_t          config;
	memset(&project, 0, sizeof(project));
	memset(&runtime, 0, sizeof(runtime));
	memset(&config, 0, sizeof(config));
	project._              = &runtime;
	runtime.filepath       = "C:\\old\\original.arm";
	config.recent_projects = any_array_create(0);
	string_array_push(config.recent_projects, "C:/old/original.arm");
	g_project         = &project;
	g_config          = &config;
	ui_files_filename = "original";
	title             = "original* - ArmorPaint";
	agent_running     = false;
	queued            = NULL;
	writes = quits = config_writes = saved_infos = 0;
	write_result                                 = false;
	picked_path                                  = NULL;
	picked_name                                  = "replacement";
	_project_save_pending                        = false;
	_project_save_and_quit                       = false;
	_project_save_generation                     = 0;
}

static void next_frame(void) {
	void (*fn)(void *) = queued;
	void *data         = queued_data;
	queued             = NULL;
	fn(data);
}

int main(void) {
	reset();
	project_save(true);
	check(queued != NULL && writes == 0 && quits == 0 && string_equals(title, "original* - ArmorPaint"), "queue leaves dirty state and editor open");
	next_frame();
	check(writes == 1 && quits == 0 && config_writes == 0 && saved_infos == 0 && string_equals(title, "original* - ArmorPaint") &&
	          string_equals(g_project->_->filepath, "C:\\old\\original.arm") && g_config->recent_projects->length == 1,
	      "failed save-and-quit retains title, path and recent files");
	write_result = true;
	project_save(false);
	next_frame();
	check(writes == 2 && quits == 0 && config_writes == 1 && saved_infos == 1 && string_equals(title, "original - ArmorPaint"),
	      "retry can succeed without quitting");

	reset();
	write_result = true;
	project_save(true);
	project_save(false);
	next_frame();
	check(writes == 1 && quits == 1 && saved_infos == 1, "queued save-and-quit flag cannot be overwritten");
	project_save(false);
	next_frame();
	check(quits == 1 && writes == 2, "quit intent does not leak to later saves");

	reset();
	project_save_as(true);
	check(queued == NULL && !_project_save_and_quit && quits == 0 && string_equals(title, "original* - ArmorPaint") &&
	          string_equals(g_project->_->filepath, "C:\\old\\original.arm"),
	      "cancelled Save As retains project and clears quit intent");
	picked_path = "C:\\new";
	project_save_as(true);
	check(string_equals(g_project->_->filepath, "C:\\old\\original.arm") && string_equals(ui_files_filename, "original"),
	      "Save As waits before committing destination");
	next_frame();
	check(quits == 0 && saved_infos == 0 && config_writes == 0 && string_equals(written_path, "C:\\new\\replacement.arm") &&
	          string_equals(g_project->_->filepath, "C:\\old\\original.arm") && string_equals(title, "original* - ArmorPaint"),
	      "failed Save As retains previous project and stays open");
	write_result = true;
	project_save_as(false);
	next_frame();
	check(quits == 0 && saved_infos == 1 && config_writes == 1 && string_equals(g_project->_->filepath, "C:\\new\\replacement.arm") &&
	          string_equals(title, "replacement - ArmorPaint") && string_equals(ui_files_filename, "replacement") &&
	          string_equals(g_config->recent_projects->buffer[0], "C:/new/replacement.arm"),
	      "successful Save As commits path, title and recent files");

	reset();
	g_project->_->filepath = "";
	project_save(true);
	check(queued == NULL && quits == 0 && string_equals(g_project->_->filepath, ""), "cancelled first save stays open");
	reset();
	write_result = true;
	project_save(true);
	++_project_save_generation;
	next_frame();
	check(writes == 0 && quits == 0 && saved_infos == 0, "request for a replaced project is discarded");
	printf("PROJECT SAVE: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
