#include "gs-api-23.h"

#include <fcntl.h>
#include <glib/gstdio.h>
#include <gmodule.h>
#include <sys/stat.h>
#include <unistd.h>

typedef GType (*QueryTypeFunc) (void);
typedef struct _GsPluginJob GsPluginJob;
typedef struct _GsPluginLoader GsPluginLoader;

GsPluginLoader *gs_plugin_loader_new (GDBusConnection *session_bus_connection,
                                    GDBusConnection *system_bus_connection);
GPtrArray *gs_plugin_loader_get_plugins (GsPluginLoader *loader);
GsPluginJob *gs_plugin_job_update_apps_new (GsAppList *apps, GsPluginUpdateAppsFlags flags);
void gs_plugin_job_run_async (GsPluginJob *job, GsPluginLoader *loader,
                             GCancellable *cancellable, GAsyncReadyCallback callback,
                             gpointer user_data);
gboolean gs_plugin_job_run_finish (GsPluginJob *job, GAsyncResult *result, GError **error);
void gs_plugin_set_enabled (GsPlugin *plugin, gboolean enabled);

typedef struct {
	GMainLoop *loop;
	GsAppList *apps;
	GError *error;
	gboolean success;
	gboolean finished;
} AsyncState;

typedef struct {
	GsPlugin *plugin;
	GsPluginLoader *loader;
	gchar *directory;
	gchar *update_log;
	gchar *mode_file;
	gchar *app_path;
} TestData;

static void
list_apps_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	AsyncState *state = user_data;
	GsPlugin *plugin = GS_PLUGIN (source_object);

	state->apps = GS_PLUGIN_GET_CLASS (plugin)->list_apps_finish (plugin, result, &state->error);
	g_main_loop_quit (state->loop);
}

static void
update_job_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	AsyncState *state = user_data;

	state->success = gs_plugin_job_run_finish ((GsPluginJob *) source_object, result, &state->error);
	state->finished = TRUE;
	g_main_loop_quit (state->loop);
}

static GsAppList *
query_apps (GsPlugin *plugin, const gchar *property, GsAppState expected_state)
{
	g_autoptr(GObject) query = g_object_new (gs_app_query_get_type (),
					       property, GS_APP_QUERY_TRISTATE_TRUE, NULL);
	AsyncState state = { .loop = g_main_loop_new (NULL, FALSE) };
	GsApp *app;

	GS_PLUGIN_GET_CLASS (plugin)->list_apps_async (plugin, (GsAppQuery *) query,
						     GS_PLUGIN_LIST_APPS_FLAGS_NONE,
						     NULL, NULL, NULL, list_apps_cb, &state);
	g_main_loop_run (state.loop);
	g_main_loop_unref (state.loop);
	g_assert_no_error (state.error);
	g_assert_nonnull (state.apps);
	g_assert_cmpuint (gs_app_list_length (state.apps), ==, 1);
	app = gs_app_list_index (state.apps, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "test-app.desktop");
	g_assert_cmpint (gs_app_get_state (app), ==, expected_state);
	g_assert_false (gs_app_has_quirk (app, GS_APP_QUIRK_DO_NOT_AUTO_UPDATE));

	return state.apps;
}

static void
run_update_job (TestData *data, GsAppList *apps, GsPluginUpdateAppsFlags flags)
{
	GsPluginJob *job = gs_plugin_job_update_apps_new (apps, flags);
	AsyncState state = { .loop = g_main_loop_new (NULL, FALSE) };

	gs_plugin_job_run_async (job, data->loader, NULL, update_job_cb, &state);
	g_main_loop_run (state.loop);
	g_main_loop_unref (state.loop);
	g_assert_no_error (state.error);
	g_assert_true (state.success);
	g_object_unref (job);
}

static void
set_mode (TestData *data, const gchar *mode)
{
	g_assert_true (g_file_set_contents (data->mode_file, mode, -1, NULL));
	g_assert_true (g_file_set_contents (data->app_path, "old appimage", -1, NULL));
	g_unlink (data->update_log);
}

static void
test_download_only (gconstpointer user_data)
{
	TestData *data = (TestData *) user_data;
	GsAppList *apps;

	set_mode (data, "success\n");
	apps = query_apps (data->plugin, "is-for-update", GS_APP_STATE_UPDATABLE_LIVE);
	run_update_job (data, apps, GS_PLUGIN_UPDATE_APPS_FLAGS_NO_APPLY);
	g_assert_cmpuint (gs_app_list_length (apps), ==, 1);
	g_assert_cmpint (gs_app_get_state (gs_app_list_index (apps, 0)), ==, GS_APP_STATE_UPDATABLE_LIVE);
	g_assert_false (g_file_test (data->update_log, G_FILE_TEST_EXISTS));
	g_object_unref (apps);
}

static void
test_success (gconstpointer user_data)
{
	TestData *data = (TestData *) user_data;
	GsAppList *apps;

	set_mode (data, "success\n");
	apps = query_apps (data->plugin, "is-for-update", GS_APP_STATE_UPDATABLE_LIVE);
	run_update_job (data, apps, GS_PLUGIN_UPDATE_APPS_FLAGS_NONE);
	g_assert_cmpuint (gs_app_list_length (apps), ==, 1);
	g_assert_cmpint (gs_app_get_state (gs_app_list_index (apps, 0)), ==, GS_APP_STATE_INSTALLED);
	g_assert_true (g_file_test (data->update_log, G_FILE_TEST_EXISTS));
	g_unlink (data->update_log);
	run_update_job (data, apps, GS_PLUGIN_UPDATE_APPS_FLAGS_NONE);
	g_assert_cmpuint (gs_app_list_length (apps), ==, 0);
	g_assert_false (g_file_test (data->update_log, G_FILE_TEST_EXISTS));
	g_object_unref (apps);
}

static void
test_interactive_update (gconstpointer user_data)
{
	TestData *data = (TestData *) user_data;
	GsAppList *apps;

	set_mode (data, "partial\n");
	apps = query_apps (data->plugin, "is-for-update", GS_APP_STATE_UPDATABLE_LIVE);
	run_update_job (data, apps, GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE);
	g_assert_cmpuint (gs_app_list_length (apps), ==, 1);
	g_assert_cmpint (gs_app_get_state (gs_app_list_index (apps, 0)), ==, GS_APP_STATE_UPDATABLE_LIVE);
	set_mode (data, "success\n");
	run_update_job (data, apps, GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE);
	g_assert_cmpuint (gs_app_list_length (apps), ==, 1);
	g_assert_cmpint (gs_app_get_state (gs_app_list_index (apps, 0)), ==, GS_APP_STATE_INSTALLED);
	g_object_unref (apps);
}

static void
test_mixed_results (gconstpointer user_data)
{
	TestData *data = (TestData *) user_data;
	g_autofree gchar *second_path = g_build_filename (data->directory, "Second.AppImage", NULL);
	GsAppList *apps;
	GsApp *completed;
	GsApp *skipped = gs_app_new ("skipped.desktop");
	GsApp *unprocessed = gs_app_new ("unprocessed.desktop");

	set_mode (data, "mixed\n");
	g_assert_true (g_file_set_contents (second_path, "old appimage", -1, NULL));
	apps = query_apps (data->plugin, "is-for-update", GS_APP_STATE_UPDATABLE_LIVE);
	completed = gs_app_list_index (apps, 0);
	gs_app_set_management_plugin (skipped, data->plugin);
	gs_app_set_management_plugin (unprocessed, data->plugin);
	gs_app_set_metadata (skipped, "Gearlever::AppImagePath", second_path);
	gs_app_set_metadata (unprocessed, "Gearlever::AppImagePath", second_path);
	gs_app_set_state (skipped, GS_APP_STATE_UPDATABLE_LIVE);
	gs_app_set_state (unprocessed, GS_APP_STATE_UPDATABLE_LIVE);
	gs_app_list_add (apps, skipped);
	gs_app_list_add (apps, unprocessed);
	run_update_job (data, apps, GS_PLUGIN_UPDATE_APPS_FLAGS_NONE);
	g_assert_cmpuint (gs_app_list_length (apps), ==, 1);
	g_assert_true (gs_app_list_index (apps, 0) == completed);
	g_assert_cmpint (gs_app_get_state (completed), ==, GS_APP_STATE_INSTALLED);
	g_assert_cmpint (gs_app_get_state (skipped), ==, GS_APP_STATE_UPDATABLE_LIVE);
	g_assert_cmpint (gs_app_get_state (unprocessed), ==, GS_APP_STATE_UPDATABLE_LIVE);
	g_object_unref (skipped);
	g_object_unref (unprocessed);
	g_object_unref (apps);
	g_unlink (second_path);
}

static void
test_unsuccessful_update (gconstpointer user_data)
{
	TestData *data = (TestData *) user_data;
	const gchar *modes[] = { "running\n", "no-method\n", "partial\n", "failed\n", "interrupted\n" };

	for (guint i = 0; i < G_N_ELEMENTS (modes); i++) {
		GsAppList *apps;
		GsApp *app;
		GsApp *other_app = gs_app_new ("other-plugin.desktop");

		g_test_message ("Update mode: %s", modes[i]);
		set_mode (data, modes[i]);
		apps = query_apps (data->plugin, "is-for-update", GS_APP_STATE_UPDATABLE_LIVE);
		app = g_object_ref (gs_app_list_index (apps, 0));
		gs_app_list_add (apps, other_app);
		run_update_job (data, apps, GS_PLUGIN_UPDATE_APPS_FLAGS_NONE);
		g_assert_cmpint (gs_app_get_state (app), ==, GS_APP_STATE_UPDATABLE_LIVE);
		g_assert_cmpuint (gs_app_list_length (apps), ==, 1);
		g_assert_true (gs_app_list_index (apps, 0) == other_app);
		g_object_unref (other_app);
		g_object_unref (app);
		g_object_unref (apps);
	}
}

typedef struct {
	AsyncState *state;
	GsApp *app;
	const gchar *ready_path;
	gint release_fd;
	gint64 deadline;
	gboolean observed_progress;
} ProgressCheck;

static gboolean
release_update_cb (gpointer user_data)
{
	ProgressCheck *check = user_data;

	g_assert_false (check->state->finished);
	g_assert_cmpint (g_get_monotonic_time () < check->deadline, ==, TRUE);
	if (!g_file_test (check->ready_path, G_FILE_TEST_EXISTS))
		return G_SOURCE_CONTINUE;
	g_assert_cmpint (gs_app_get_state (check->app), ==, GS_APP_STATE_INSTALLING);
	check->observed_progress = TRUE;
	g_assert_cmpint (write (check->release_fd, "done\n", 5), ==, 5);
	return G_SOURCE_REMOVE;
}

static void
test_waits_for_completion (gconstpointer user_data)
{
	TestData *data = (TestData *) user_data;
	g_autofree gchar *ready_path = g_strconcat (data->update_log, ".ready", NULL);
	g_autofree gchar *release_path = g_strconcat (data->update_log, ".release", NULL);
	GsAppList *apps;
	GsPluginJob *job;
	AsyncState state = { .loop = g_main_loop_new (NULL, FALSE) };
	ProgressCheck check = {
		.state = &state,
		.ready_path = ready_path,
		.deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND,
	};

	set_mode (data, "delayed-success\n");
	g_assert_cmpint (mkfifo (release_path, 0600), ==, 0);
	check.release_fd = g_open (release_path, O_RDWR | O_NONBLOCK, 0);
	g_assert_cmpint (check.release_fd, >=, 0);
	apps = query_apps (data->plugin, "is-for-update", GS_APP_STATE_UPDATABLE_LIVE);
	check.app = gs_app_list_index (apps, 0);
	job = gs_plugin_job_update_apps_new (apps, GS_PLUGIN_UPDATE_APPS_FLAGS_NONE);
	g_timeout_add (10, release_update_cb, &check);
	gs_plugin_job_run_async (job, data->loader, NULL, update_job_cb, &state);
	g_main_loop_run (state.loop);
	g_main_loop_unref (state.loop);
	g_assert_no_error (state.error);
	g_assert_true (state.success);
	g_assert_true (check.observed_progress);
	g_assert_cmpint (gs_app_get_state (check.app), ==, GS_APP_STATE_INSTALLED);
	g_assert_cmpuint (gs_app_list_length (apps), ==, 1);
	g_object_unref (job);
	g_object_unref (apps);
	close (check.release_fd);
	g_unlink (release_path);
	g_unlink (ready_path);
}

int
main (int argc, char **argv)
{
	GModule *module;
	QueryTypeFunc query_type = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *executable = NULL;
	g_autofree gchar *data_directory = NULL;
	g_autoptr(GTestDBus) bus = NULL;
	g_autoptr(GDBusConnection) connection = NULL;
	TestData data = { 0 };
	gint result;
	const gchar *script =
		"#!/bin/sh\n"
		"case \"$1\" in\n"
		"--list-installed) key=installed ;;\n"
		"--list-updates) key=updates ;;\n"
		"--update)\n"
		"  printf '%s\\n' \"$@\" > \"$GEARLEVER_TEST_UPDATE_LOG\"\n"
		"  read -r mode < \"$GEARLEVER_TEST_MODE\"\n"
		"  case \"$mode\" in\n"
		"  running) printf '%s was skipped because the application is running\\n' \"$2\"; exit 0 ;;\n"
		"  no-method) printf 'No update method was found for this AppImage\\n'; exit 0 ;;\n"
		"  partial) printf '\\rStatus: 1%%'; exit 0 ;;\n"
		"  failed) printf 'Download failed\\n' >&2; exit 1 ;;\n"
		"  interrupted) printf '\\rStatus: 1%%\\n%s updated successfully\\n' \"$2\"; exit 0 ;;\n"
		"  mixed)\n"
		"    if [ \"$2\" != \"$GEARLEVER_TEST_APP\" ]; then\n"
		"      printf '%s was skipped because the application is running\\n' \"$2\"; exit 0\n"
		"    fi ;;\n"
		"  delayed-success)\n"
		"    printf '\\rStatus: 1%%'\n"
		"    printf 'ready' > \"$GEARLEVER_TEST_UPDATE_LOG.ready\"\n"
		"    read -r reply < \"$GEARLEVER_TEST_UPDATE_LOG.release\" ;;\n"
		"  esac\n"
		"  printf 'new appimage contents' > \"$2\"\n"
		"  printf '\\n%s updated successfully\\n' \"$2\"; exit 0 ;;\n"
		"*) exit 1 ;;\n"
		"esac\n"
		"printf '{\"schema_version\":1,\"%s\":[{\"name\":\"Test app\","
		"\"path\":\"%s\",\"desktop_id\":\"test-app.desktop\","
		"\"current_version\":\"1.0\",\"available_version\":\"2.0\"}]}' \"$key\" \"$GEARLEVER_TEST_APP\"\n";

	g_test_init (&argc, &argv, NULL);
	if (argc != 2)
		return 2;

	data.directory = g_dir_make_tmp ("gearlever-updates-XXXXXX", &error);
	g_assert_no_error (error);
	executable = g_build_filename (data.directory, "gearlever", NULL);
	data.update_log = g_build_filename (data.directory, "update.log", NULL);
	data.mode_file = g_build_filename (data.directory, "mode", NULL);
	data.app_path = g_build_filename (data.directory, "Test App.AppImage", NULL);
	data_directory = g_build_filename (data.directory, "data", NULL);
	g_assert_true (g_file_set_contents (executable, script, -1, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_chmod (executable, 0700), ==, 0);
	g_setenv ("XDG_DATA_HOME", data_directory, TRUE);
	g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);
	g_setenv ("GEARLEVER_TEST_UPDATE_LOG", data.update_log, TRUE);
	g_setenv ("GEARLEVER_TEST_MODE", data.mode_file, TRUE);
	g_setenv ("GEARLEVER_TEST_APP", data.app_path, TRUE);
	g_unsetenv ("GEARLEVER_FLATPAK_EXECUTABLE");

	bus = g_test_dbus_new (G_TEST_DBUS_NONE);
	g_test_dbus_up (bus);
	g_setenv ("PATH", data.directory, TRUE);
	connection = g_dbus_connection_new_for_address_sync (g_test_dbus_get_bus_address (bus),
		G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
		NULL, NULL, &error);
	g_assert_no_error (error);

	module = g_module_open (argv[1], G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
	g_assert_nonnull (module);
	g_assert_true (g_module_symbol (module, "gs_plugin_query_type", (gpointer *) &query_type));
	g_module_make_resident (module);
	data.plugin = g_object_new (query_type (), "name", "gearlever",
				   "session-bus-connection", connection,
				   "system-bus-connection", connection, NULL);
	gs_plugin_set_enabled (data.plugin, TRUE);
	data.loader = gs_plugin_loader_new (connection, connection);
	g_ptr_array_add (gs_plugin_loader_get_plugins (data.loader), g_object_ref (data.plugin));

	g_test_add_data_func ("/gearlever/updates/download-only", &data, test_download_only);
	g_test_add_data_func ("/gearlever/updates/automatic-success", &data, test_success);
	g_test_add_data_func ("/gearlever/updates/interactive", &data, test_interactive_update);
	g_test_add_data_func ("/gearlever/updates/mixed-results", &data, test_mixed_results);
	g_test_add_data_func ("/gearlever/updates/unsuccessful", &data, test_unsuccessful_update);
	g_test_add_data_func ("/gearlever/updates/waits-for-completion", &data, test_waits_for_completion);
	result = g_test_run ();

	g_object_unref (data.loader);
	g_object_unref (data.plugin);
	g_dbus_connection_close_sync (connection, NULL, NULL);
	g_clear_object (&connection);
	g_test_dbus_down (bus);
	g_module_close (module);
	g_unlink (data.update_log);
	g_unlink (data.mode_file);
	g_unlink (data.app_path);
	g_unlink (executable);
	g_rmdir (data.directory);
	g_free (data.update_log);
	g_free (data.mode_file);
	g_free (data.app_path);
	g_free (data.directory);
	return result;
}
