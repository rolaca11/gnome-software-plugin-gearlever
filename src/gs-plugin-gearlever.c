#include "gs-api-23.h"
#include "gearlever-utils.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <gmodule.h>

#define GEARLEVER_PATH_KEY "Gearlever::AppImagePath"
#define GEARLEVER_FILE_ICON_SIZE 256

typedef struct _GsPluginGearlever {
	GsPlugin parent_instance;
} GsPluginGearlever;

typedef struct _GsPluginGearleverClass {
	GsPluginClass parent_class;
} GsPluginGearleverClass;

G_DEFINE_TYPE (GsPluginGearlever, gs_plugin_gearlever, GS_TYPE_PLUGIN)

typedef struct {
	GsAppList *apps;
	GsAppList *job_apps;
	GHashTable *completed_apps;
	GsPluginUpdateAppsFlags flags;
	GsPluginEventCallback event_callback;
	gpointer event_user_data;
	GsApp *failed_app;
} UpdateTaskData;

typedef enum {
	LIST_MODE_UPDATES,
	LIST_MODE_INSTALLED,
} ListMode;

static void
update_task_data_free (UpdateTaskData *data)
{
	g_clear_object (&data->apps);
	g_clear_object (&data->job_apps);
	g_clear_pointer (&data->completed_apps, g_hash_table_unref);
	g_clear_object (&data->failed_app);
	g_free (data);
}

static gboolean
desktop_file_for_path (const gchar *appimage_path,
		       gchar **desktop_id,
		       gchar **name,
		       gchar **summary,
		       gchar **version,
		       gchar **icon)
{
	g_autofree gchar *applications_dir = g_build_filename (g_get_user_data_dir (), "applications", NULL);
	g_autoptr(GDir) dir = NULL;
	const gchar *filename;

	dir = g_dir_open (applications_dir, 0, NULL);
	if (dir == NULL)
		return FALSE;

	while ((filename = g_dir_read_name (dir)) != NULL) {
		g_autofree gchar *desktop_path = NULL;
		g_autoptr(GKeyFile) key_file = NULL;
		g_autofree gchar *try_exec = NULL;

		if (!g_str_has_suffix (filename, ".desktop"))
			continue;

		desktop_path = g_build_filename (applications_dir, filename, NULL);
		key_file = g_key_file_new ();
		if (!g_key_file_load_from_file (key_file, desktop_path, G_KEY_FILE_NONE, NULL))
			continue;

		try_exec = g_key_file_get_string (key_file, G_KEY_FILE_DESKTOP_GROUP,
					      G_KEY_FILE_DESKTOP_KEY_TRY_EXEC, NULL);
		if (g_strcmp0 (try_exec, appimage_path) != 0)
			continue;

		*desktop_id = g_strdup (filename);
		*name = g_key_file_get_locale_string (key_file, G_KEY_FILE_DESKTOP_GROUP,
						      G_KEY_FILE_DESKTOP_KEY_NAME, NULL, NULL);
		*summary = g_key_file_get_locale_string (key_file, G_KEY_FILE_DESKTOP_GROUP,
							 G_KEY_FILE_DESKTOP_KEY_COMMENT, NULL, NULL);
		*version = g_key_file_get_string (key_file, G_KEY_FILE_DESKTOP_GROUP,
						 "X-AppImage-Version", NULL);
		*icon = g_key_file_get_string (key_file, G_KEY_FILE_DESKTOP_GROUP,
					      G_KEY_FILE_DESKTOP_KEY_ICON, NULL);
		return TRUE;
	}

	return FALSE;
}

static GsApp *
app_from_gearlever (GsPlugin *plugin, const GearleverUpdate *update, gboolean is_update)
{
	g_autofree gchar *desktop_id = NULL;
	g_autofree gchar *name = NULL;
	g_autofree gchar *summary = NULL;
	g_autofree gchar *version = NULL;
	g_autofree gchar *icon_name = NULL;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *fallback_id = NULL;
	g_autoptr(GIcon) icon = NULL;
	GStatBuf stat_buf;
	GsApp *app;

	desktop_file_for_path (update->path, &desktop_id, &name, &summary, &version, &icon_name);
	if (update->desktop_id != NULL && *update->desktop_id != '\0') {
		g_free (desktop_id);
		desktop_id = g_strdup (update->desktop_id);
	}
	if (desktop_id == NULL) {
		checksum = g_compute_checksum_for_string (G_CHECKSUM_SHA256, update->path, -1);
		fallback_id = g_strdup_printf ("gearlever-%.*s.desktop", 24, checksum);
		desktop_id = g_steal_pointer (&fallback_id);
	}
	if (name == NULL || *name == '\0')
		name = g_strdup (update->name);
	if (summary == NULL || *summary == '\0') {
		g_free (summary);
		summary = g_strdup ("AppImage managed by Gearlever");
	}

	app = gs_app_new (desktop_id);
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, name);
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, summary);
	gs_app_set_description (app, GS_APP_QUALITY_NORMAL,
				"This AppImage is installed and managed by Gearlever.");
	if (update->current_version != NULL && *update->current_version != '\0')
		gs_app_set_version (app, update->current_version);
	else if (version != NULL && *version != '\0')
		gs_app_set_version (app, version);
	if (is_update && update->available_version != NULL && *update->available_version != '\0')
		gs_app_set_update_version (app, update->available_version);
	if (is_update && update->manager != NULL && *update->manager != '\0') {
		g_autofree gchar *details = g_strdup_printf ("A new AppImage release is available through Gearlever (%s).",
							 update->manager);
		gs_app_set_update_details_text (app, details);
	} else if (is_update) {
		gs_app_set_update_details_text (app, "A new AppImage release is available through Gearlever.");
	}
	gs_app_set_kind (app, AS_COMPONENT_KIND_DESKTOP_APP);
	gs_app_set_scope (app, AS_COMPONENT_SCOPE_USER);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_APPIMAGE);
	gs_app_set_state (app, is_update ? GS_APP_STATE_UPDATABLE_LIVE : GS_APP_STATE_INSTALLED);
	gs_app_set_origin (app, "gearlever");
	gs_app_set_origin_ui (app, "Gearlever");
	gs_app_set_management_plugin (app, plugin);
	gs_app_set_metadata (app, GEARLEVER_PATH_KEY, update->path);
	gs_app_set_metadata (app, "GnomeSoftware::Creator", "gearlever");

	if (g_stat (update->path, &stat_buf) == 0)
		gs_app_set_size_installed (app, GS_SIZE_TYPE_VALID, stat_buf.st_size);
	else
		gs_app_set_size_installed (app, GS_SIZE_TYPE_UNKNOWN, 0);
	if (is_update && update->has_download_size)
		gs_app_set_size_download (app, GS_SIZE_TYPE_VALID, update->download_size);
	else
		gs_app_set_size_download (app, GS_SIZE_TYPE_UNKNOWABLE, 0);

	if (icon_name != NULL && *icon_name != '\0') {
		if (g_path_is_absolute (icon_name)) {
			if (g_file_test (icon_name, G_FILE_TEST_IS_REGULAR)) {
				g_autoptr(GFile) icon_file = g_file_new_for_path (icon_name);
				icon = g_file_icon_new (icon_file);

				/* GFileIcon does not carry dimensions. GNOME Software ignores
				 * file icons with an unknown width, so annotate Gearlever's
				 * extracted application icons with their conservative minimum
				 * display size. */
				gs_icon_set_width (icon, GEARLEVER_FILE_ICON_SIZE);
				gs_icon_set_height (icon, GEARLEVER_FILE_ICON_SIZE);
			}
		} else {
			icon = g_themed_icon_new (icon_name);
		}
	}
	if (icon == NULL)
		icon = g_themed_icon_new ("application-x-executable");
	gs_app_add_icon (app, icon);

	return app;
}

static void
list_apps_thread (GTask *task,
		  gpointer source_object,
		  gpointer task_data,
		  GCancellable *cancellable)
{
	GsPlugin *plugin = GS_PLUGIN (source_object);
	ListMode mode = GPOINTER_TO_INT (task_data);
	const gchar *update_args[] = { "--list-updates", "--json", NULL };
	const gchar *installed_args[] = { "--list-installed", "--json", NULL };
	const gchar * const *args = mode == LIST_MODE_UPDATES ? update_args : installed_args;
	g_autofree gchar *output = NULL;
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GPtrArray) updates = NULL;
	GsAppList *list;
	GearleverCommand command = gearlever_find_command ();

	if (command == GEARLEVER_COMMAND_NONE) {
		g_task_return_pointer (task, gs_app_list_new (), g_object_unref);
		return;
	}
	if (!gearlever_run (command, args, cancellable, &output, &local_error)) {
		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
		} else {
			g_task_return_new_error (task, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_NO_NETWORK,
					 "Could not check Gearlever updates: %s", local_error->message);
		}
		return;
	}

	if (mode == LIST_MODE_INSTALLED) {
		updates = gearlever_parse_installed_json (output, &local_error);
	} else {
		updates = gearlever_parse_updates_json (output, &local_error);
	}
	if (updates == NULL) {
		const gchar *first_nonspace = output;
		while (first_nonspace != NULL && g_ascii_isspace (*first_nonspace))
			first_nonspace++;

		/* Gearlever releases before the JSON option simply ignore --json and
		 * print the legacy table. Keep them working during the transition. */
		if (first_nonspace != NULL && *first_nonspace != '{') {
			g_clear_error (&local_error);
			updates = mode == LIST_MODE_UPDATES ?
				gearlever_parse_updates (output) :
				gearlever_parse_installed (output);
		} else {
			g_task_return_new_error (task, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
						 "Invalid Gearlever update data: %s", local_error->message);
			return;
		}
	}
	list = gs_app_list_new ();
	for (guint i = 0; i < updates->len; i++) {
		GearleverUpdate *update = g_ptr_array_index (updates, i);
		GsApp *app = app_from_gearlever (plugin, update, mode == LIST_MODE_UPDATES);
		gs_app_list_add (list, app);
		g_object_unref (app);
	}

	g_task_return_pointer (task, list, g_object_unref);
}

static void
gs_plugin_gearlever_list_apps_async (GsPlugin *plugin,
				     GsAppQuery *query,
				     GsPluginListAppsFlags flags,
				     GsPluginEventCallback event_callback,
				     gpointer event_user_data,
				     GCancellable *cancellable,
				     GAsyncReadyCallback callback,
				     gpointer user_data)
{
	g_autoptr(GTask) task = g_task_new (plugin, cancellable, callback, user_data);
	ListMode mode;
	(void) flags;
	(void) event_callback;
	(void) event_user_data;

	g_task_set_source_tag (task, gs_plugin_gearlever_list_apps_async);
	if (query == NULL || gs_app_query_get_n_properties_set (query) != 1) {
		g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				 "Gearlever only supports installed-app and update queries");
		return;
	}
	if (gs_app_query_get_is_for_update (query) == GS_APP_QUERY_TRISTATE_TRUE)
		mode = LIST_MODE_UPDATES;
	else if (gs_app_query_get_is_installed (query) == GS_APP_QUERY_TRISTATE_TRUE)
		mode = LIST_MODE_INSTALLED;
	else {
		g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				 "Gearlever only supports installed-app and update queries");
		return;
	}

	g_task_set_task_data (task, GINT_TO_POINTER (mode), NULL);
	g_task_run_in_thread (task, list_apps_thread);
}

static GsAppList *
gs_plugin_gearlever_list_apps_finish (GsPlugin *plugin,
				      GAsyncResult *result,
				      GError **error)
{
	(void) plugin;
	return g_task_propagate_pointer (G_TASK (result), error);
}

static gboolean
update_completed (gchar *output, const gchar *path, const GStatBuf *before)
{
	g_autofree gchar *confirmation = g_strdup_printf ("%s updated successfully", path);
	const gchar *last_line;
	GStatBuf after;

	if (output == NULL)
		return FALSE;
	g_strchomp (output);
	last_line = strrchr (output, '\n');
	if (g_strcmp0 (last_line != NULL ? last_line + 1 : output, confirmation) != 0)
		return FALSE;
	if (g_stat (path, &after) != 0)
		return errno == ENOENT;

	return before->st_dev != after.st_dev ||
	       before->st_ino != after.st_ino ||
	       before->st_size != after.st_size ||
	       before->st_mtim.tv_sec != after.st_mtim.tv_sec ||
	       before->st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
	       before->st_ctim.tv_sec != after.st_ctim.tv_sec ||
	       before->st_ctim.tv_nsec != after.st_ctim.tv_nsec;
}

static void
update_apps_thread (GTask *task,
		    gpointer source_object,
		    gpointer task_data,
		    GCancellable *cancellable)
{
	GsPlugin *plugin = GS_PLUGIN (source_object);
	UpdateTaskData *data = task_data;
	GearleverCommand command;
	guint managed_count = 0;

	/* Gearlever has no separate download/cache operation. Defer all work until
	 * GNOME Software asks for the apply phase. */
	if ((data->flags & GS_PLUGIN_UPDATE_APPS_FLAGS_NO_APPLY) != 0) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	for (guint i = 0; i < gs_app_list_length (data->apps); i++) {
		if (gs_app_has_management_plugin (gs_app_list_index (data->apps, i), plugin))
			managed_count++;
	}
	if (managed_count == 0) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	command = gearlever_find_command ();
	if (command == GEARLEVER_COMMAND_NONE) {
		g_task_return_new_error (task, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "Gearlever is not installed");
		return;
	}

	for (guint i = 0; i < gs_app_list_length (data->apps); i++) {
		GsApp *app = gs_app_list_index (data->apps, i);
		const gchar *path;
		const gchar *args[4];
		g_autofree gchar *output = NULL;
		g_autoptr(GError) local_error = NULL;
		GStatBuf before;
		gboolean skipped;
		gboolean succeeded;

		if (!gs_app_has_management_plugin (app, plugin))
			continue;
		if (gs_app_get_state (app) == GS_APP_STATE_INSTALLED)
			continue;
		path = gs_app_get_metadata_item (app, GEARLEVER_PATH_KEY);
		if (path == NULL || g_stat (path, &before) != 0) {
			g_set_object (&data->failed_app, app);
			g_task_return_new_error (task, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
						 "Could not locate the AppImage for %s", gs_app_get_id (app));
			return;
		}

		args[0] = "--update";
		args[1] = path;
		args[2] = "--yes";
		args[3] = NULL;
		gs_app_set_state (app, GS_APP_STATE_INSTALLING);
		gs_app_set_progress (app, GS_APP_PROGRESS_UNKNOWN);

		succeeded = gearlever_run (command, args, cancellable, &output, &local_error);
		skipped = output != NULL && strstr (output, "was skipped because") != NULL;
		if (!succeeded || skipped || !update_completed (output, path, &before)) {
			g_set_object (&data->failed_app, app);
			gs_app_set_state (app, GS_APP_STATE_UPDATABLE_LIVE);
			gs_app_set_progress (app, 0);
			if (local_error != NULL) {
				g_task_return_new_error (task, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
						 "Could not update %s: %s", gs_app_get_id (app), local_error->message);
			} else if (skipped) {
				g_task_return_new_error (task, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
						 "%s is currently running; close it and try again", gs_app_get_id (app));
			} else {
				g_task_return_new_error (task, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
						 "%s was not updated: Gearlever did not confirm an AppImage replacement",
						 gs_app_get_id (app));
			}
			return;
		}

		gs_app_set_progress (app, 100);
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);
		g_hash_table_add (data->completed_apps, app);
	}

	g_task_return_boolean (task, TRUE);
}

static void
update_apps_worker_done (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	g_autoptr(GTask) outer_task = G_TASK (user_data);
	GsPlugin *plugin = GS_PLUGIN (source_object);
	UpdateTaskData *data = g_task_get_task_data (outer_task);
	g_autoptr(GError) local_error = NULL;

	if ((data->flags & (GS_PLUGIN_UPDATE_APPS_FLAGS_NO_APPLY |
			    GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE)) == 0) {
		for (guint i = 0; i < gs_app_list_length (data->apps); i++) {
			GsApp *app = gs_app_list_index (data->apps, i);
			if (gs_app_has_management_plugin (app, plugin) &&
			    !g_hash_table_contains (data->completed_apps, app))
				gs_app_list_remove (data->job_apps, app);
		}
	}
	if (g_hash_table_size (data->completed_apps) > 0)
		gs_plugin_updates_changed (plugin);

	if (!g_task_propagate_boolean (G_TASK (result), &local_error)) {
		if (data->event_callback != NULL) {
			GsPluginEvent *event = gs_plugin_event_new ("app", data->failed_app,
							     "error", local_error,
							     NULL);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
			if ((data->flags & GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE) != 0)
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
			data->event_callback (plugin, event, data->event_user_data);
			g_object_unref (event);
		}
		g_task_return_error (outer_task, g_steal_pointer (&local_error));
		return;
	}

	g_task_return_boolean (outer_task, TRUE);
}

static void
gs_plugin_gearlever_update_apps_async (GsPlugin *plugin,
				       GsAppList *apps,
				       GsPluginUpdateAppsFlags flags,
				       GsPluginProgressCallback progress_callback,
				       gpointer progress_user_data,
				       GsPluginEventCallback event_callback,
				       gpointer event_user_data,
				       GsPluginAppNeedsUserActionCallback app_needs_user_action_callback,
				       gpointer app_needs_user_action_data,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback,
				       gpointer user_data)
{
	g_autoptr(GTask) task = g_task_new (plugin, cancellable, callback, user_data);
	g_autoptr(GTask) worker_task = NULL;
	UpdateTaskData *data = g_new0 (UpdateTaskData, 1);
	(void) progress_callback;
	(void) progress_user_data;
	(void) app_needs_user_action_callback;
	(void) app_needs_user_action_data;

	data->apps = gs_app_list_copy (apps);
	data->job_apps = g_object_ref (apps);
	data->completed_apps = g_hash_table_new (g_direct_hash, g_direct_equal);
	data->flags = flags;
	data->event_callback = event_callback;
	data->event_user_data = event_user_data;
	g_task_set_task_data (task, data, (GDestroyNotify) update_task_data_free);
	g_task_set_source_tag (task, gs_plugin_gearlever_update_apps_async);

	worker_task = g_task_new (plugin, cancellable, update_apps_worker_done, g_object_ref (task));
	g_task_set_task_data (worker_task, data, NULL);
	g_task_run_in_thread (worker_task, update_apps_thread);
}

static gboolean
gs_plugin_gearlever_update_apps_finish (GsPlugin *plugin,
					GAsyncResult *result,
					GError **error)
{
	(void) plugin;
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_gearlever_class_init (GsPluginGearleverClass *klass)
{
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);
	plugin_class->list_apps_async = gs_plugin_gearlever_list_apps_async;
	plugin_class->list_apps_finish = gs_plugin_gearlever_list_apps_finish;
	plugin_class->update_apps_async = gs_plugin_gearlever_update_apps_async;
	plugin_class->update_apps_finish = gs_plugin_gearlever_update_apps_finish;
}

static void
gs_plugin_gearlever_init (GsPluginGearlever *self)
{
	(void) self;
}

G_MODULE_EXPORT GType
gs_plugin_query_type (void)
{
	return gs_plugin_gearlever_get_type ();
}
