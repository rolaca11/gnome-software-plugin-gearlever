#include "gearlever-utils.h"

#include <json-glib/json-glib.h>
#include <string.h>

#define GEARLEVER_APP_ID "it.mijorus.gearlever"

void
gearlever_update_free (GearleverUpdate *update)
{
	if (update == NULL)
		return;
	g_free (update->name);
	g_free (update->path);
	g_free (update->desktop_id);
	g_free (update->current_version);
	g_free (update->available_version);
	g_free (update->manager);
	g_free (update);
}

static gboolean
gearlever_json_error (GError **error, const gchar *message)
{
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, message);
	return FALSE;
}

static gboolean
json_dup_nullable_string_member (JsonObject *object,
				 const gchar *member_name,
				 gchar **value,
				 GError **error)
{
	JsonNode *node = json_object_get_member (object, member_name);

	if (node == NULL || JSON_NODE_HOLDS_NULL (node))
		return TRUE;
	if (!JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_STRING) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "Gearlever JSON member '%s' must be a string or null", member_name);
		return FALSE;
	}
	*value = g_strdup (json_node_get_string (node));
	return TRUE;
}

static gboolean
json_nullable_boolean_member (JsonObject *object,
			      const gchar *member_name,
			      gboolean *value,
			      gboolean *has_value,
			      GError **error)
{
	JsonNode *node = json_object_get_member (object, member_name);

	if (node == NULL || JSON_NODE_HOLDS_NULL (node)) {
		*has_value = FALSE;
		return TRUE;
	}
	if (!JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_BOOLEAN) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "Gearlever JSON member '%s' must be a boolean or null", member_name);
		return FALSE;
	}
	*value = json_node_get_boolean (node);
	*has_value = TRUE;
	return TRUE;
}

static gboolean
json_nullable_size_member (JsonObject *object,
			   const gchar *member_name,
			   guint64 *value,
			   gboolean *has_value,
			   GError **error)
{
	JsonNode *node = json_object_get_member (object, member_name);
	gint64 signed_value;

	if (node == NULL || JSON_NODE_HOLDS_NULL (node)) {
		*has_value = FALSE;
		return TRUE;
	}
	if (!JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_INT64) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "Gearlever JSON member '%s' must be an integer or null", member_name);
		return FALSE;
	}
	signed_value = json_node_get_int (node);
	if (signed_value < 0)
		return gearlever_json_error (error, "Gearlever download size cannot be negative");
	*value = signed_value;
	*has_value = TRUE;
	return TRUE;
}

static GPtrArray *
gearlever_parse_apps_json (const gchar *output, const gchar *array_member, GError **error)
{
	g_autoptr(JsonParser) parser = json_parser_new ();
	g_autoptr(GPtrArray) updates = g_ptr_array_new_with_free_func ((GDestroyNotify) gearlever_update_free);
	JsonNode *root_node;
	JsonObject *root;
	JsonNode *apps_node;
	JsonArray *apps_array;

	if (output == NULL)
		return NULL;
	if (!json_parser_load_from_data (parser, output, -1, error))
		return NULL;

	root_node = json_parser_get_root (parser);
	if (root_node == NULL || !JSON_NODE_HOLDS_OBJECT (root_node)) {
		gearlever_json_error (error, "Gearlever JSON root must be an object");
		return NULL;
	}
	root = json_node_get_object (root_node);
	if (json_object_get_int_member_with_default (root, "schema_version", -1) != 1) {
		gearlever_json_error (error, "Unsupported Gearlever JSON schema version");
		return NULL;
	}
	apps_node = json_object_get_member (root, array_member);
	if (apps_node == NULL || !JSON_NODE_HOLDS_ARRAY (apps_node)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "Gearlever JSON is missing its '%s' array", array_member);
		return NULL;
	}

	apps_array = json_node_get_array (apps_node);
	for (guint i = 0; i < json_array_get_length (apps_array); i++) {
		JsonNode *update_node = json_array_get_element (apps_array, i);
		JsonObject *update_object;
		g_autoptr(GearleverUpdate) update = g_new0 (GearleverUpdate, 1);
		gboolean ignored_has_value = FALSE;

		if (update_node == NULL || !JSON_NODE_HOLDS_OBJECT (update_node)) {
			gearlever_json_error (error, "Every Gearlever update must be an object");
			return NULL;
		}
		update_object = json_node_get_object (update_node);

		if (!json_dup_nullable_string_member (update_object, "name", &update->name, error) ||
		    !json_dup_nullable_string_member (update_object, "path", &update->path, error) ||
		    !json_dup_nullable_string_member (update_object, "desktop_id", &update->desktop_id, error) ||
		    !json_dup_nullable_string_member (update_object, "current_version", &update->current_version, error) ||
		    !json_dup_nullable_string_member (update_object, "available_version", &update->available_version, error) ||
		    !json_dup_nullable_string_member (update_object, "manager", &update->manager, error) ||
		    !json_nullable_size_member (update_object, "download_size",
						&update->download_size, &update->has_download_size, error) ||
		    !json_nullable_boolean_member (update_object, "embedded_source",
						   &update->embedded_source, &ignored_has_value, error) ||
		    !json_nullable_boolean_member (update_object, "running",
						   &update->running, &update->has_running, error))
			return NULL;
		if (update->name == NULL || update->path == NULL || !g_path_is_absolute (update->path)) {
			gearlever_json_error (error, "Gearlever update is missing a name or absolute path");
			return NULL;
		}
		g_ptr_array_add (updates, g_steal_pointer (&update));
	}

	return g_steal_pointer (&updates);
}

GPtrArray *
gearlever_parse_updates_json (const gchar *output, GError **error)
{
	return gearlever_parse_apps_json (output, "updates", error);
}

GPtrArray *
gearlever_parse_installed_json (const gchar *output, GError **error)
{
	return gearlever_parse_apps_json (output, "installed", error);
}

GPtrArray *
gearlever_parse_updates (const gchar *output)
{
	g_autoptr(GPtrArray) updates = g_ptr_array_new_with_free_func ((GDestroyNotify) gearlever_update_free);
	g_auto(GStrv) lines = NULL;

	if (output == NULL || *output == '\0')
		return g_steal_pointer (&updates);

	lines = g_strsplit (output, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		const gchar *marker = g_strstr_len (lines[i], -1, "[Update available,");
		const gchar *status_end;
		g_autofree gchar *name = NULL;
		g_autofree gchar *path = NULL;
		GearleverUpdate *update;

		if (marker == NULL)
			continue;
		status_end = strchr (marker, ']');
		if (status_end == NULL)
			continue;

		name = g_strndup (lines[i], marker - lines[i]);
		path = g_strdup (status_end + 1);
		g_strstrip (name);
		g_strstrip (path);
		if (*name == '\0' || !g_path_is_absolute (path))
			continue;

		update = g_new0 (GearleverUpdate, 1);
		update->name = g_steal_pointer (&name);
		update->path = g_steal_pointer (&path);
		g_ptr_array_add (updates, update);
	}

	return g_steal_pointer (&updates);
}

GPtrArray *
gearlever_parse_installed (const gchar *output)
{
	g_autoptr(GPtrArray) apps = g_ptr_array_new_with_free_func ((GDestroyNotify) gearlever_update_free);
	g_auto(GStrv) lines = NULL;

	if (output == NULL || *output == '\0')
		return g_steal_pointer (&apps);

	lines = g_strsplit (output, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		const gchar *manager_end = strrchr (lines[i], ']');
		const gchar *manager_start = NULL;
		const gchar *version_end = NULL;
		const gchar *version_start = NULL;
		g_autofree gchar *name = NULL;
		g_autofree gchar *path = NULL;
		g_autofree gchar *version = NULL;
		g_autofree gchar *manager = NULL;
		GearleverUpdate *app;

		if (manager_end == NULL)
			continue;
		for (const gchar *p = manager_end; p > lines[i];) {
			p--;
			if (*p == '[') {
				manager_start = p;
				break;
			}
		}
		if (manager_start == NULL)
			continue;
		version_end = manager_start;
		while (version_end > lines[i] && g_ascii_isspace (version_end[-1]))
			version_end--;
		if (version_end <= lines[i] || version_end[-1] != ']')
			continue;
		version_end--;
		for (const gchar *p = version_end; p > lines[i];) {
			p--;
			if (*p == '[') {
				version_start = p;
				break;
			}
		}
		if (version_start == NULL)
			continue;

		name = g_strndup (lines[i], version_start - lines[i]);
		version = g_strndup (version_start + 1, version_end - version_start - 1);
		manager = g_strndup (manager_start + 1, manager_end - manager_start - 1);
		path = g_strdup (manager_end + 1);
		g_strstrip (name);
		g_strstrip (version);
		g_strstrip (manager);
		g_strstrip (path);
		if (*name == '\0' || !g_path_is_absolute (path))
			continue;

		app = g_new0 (GearleverUpdate, 1);
		app->name = g_steal_pointer (&name);
		app->path = g_steal_pointer (&path);
		if (!g_str_equal (version, "Not specified"))
			app->current_version = g_steal_pointer (&version);
		if (!g_str_equal (manager, "UpdatesNotAvailable")) {
			gchar *embedded_marker = strstr (manager, "|embedded");
			if (embedded_marker != NULL) {
				*embedded_marker = '\0';
				app->embedded_source = TRUE;
			}
			app->manager = g_steal_pointer (&manager);
		}
		g_ptr_array_add (apps, app);
	}

	return g_steal_pointer (&apps);
}

GearleverCommand
gearlever_find_command (void)
{
	const gchar *flatpak_build = g_getenv ("GEARLEVER_FLATPAK_EXECUTABLE");
	g_autofree gchar *native = g_find_program_in_path ("gearlever");
	g_autofree gchar *flatpak = g_find_program_in_path ("flatpak");
	g_autoptr(GSubprocess) process = NULL;

	if (flatpak_build != NULL && g_path_is_absolute (flatpak_build) &&
	    g_file_test (flatpak_build, G_FILE_TEST_IS_EXECUTABLE) && flatpak != NULL)
		return GEARLEVER_COMMAND_FLATPAK_BUILD;
	if (native != NULL)
		return GEARLEVER_COMMAND_NATIVE;
	if (flatpak == NULL)
		return GEARLEVER_COMMAND_NONE;

	process = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
				    G_SUBPROCESS_FLAGS_STDERR_SILENCE,
				    NULL, "flatpak", "info", GEARLEVER_APP_ID, NULL);
	if (process != NULL && g_subprocess_wait_check (process, NULL, NULL))
		return GEARLEVER_COMMAND_FLATPAK;

	return GEARLEVER_COMMAND_NONE;
}

gboolean
gearlever_run (GearleverCommand command,
	       const gchar * const *arguments,
	       GCancellable *cancellable,
	       gchar **stdout_text,
	       GError **error)
{
	g_autoptr(GPtrArray) argv = g_ptr_array_new ();
	g_autoptr(GSubprocess) process = NULL;
	g_autofree gchar *stderr_text = NULL;
	g_autofree gchar *flatpak_command_arg = NULL;

	g_return_val_if_fail (stdout_text != NULL, FALSE);
	*stdout_text = NULL;

	if (command == GEARLEVER_COMMAND_NATIVE) {
		g_ptr_array_add (argv, (gpointer) "gearlever");
	} else if (command == GEARLEVER_COMMAND_FLATPAK) {
		g_ptr_array_add (argv, (gpointer) "flatpak");
		g_ptr_array_add (argv, (gpointer) "run");
		g_ptr_array_add (argv, (gpointer) GEARLEVER_APP_ID);
	} else if (command == GEARLEVER_COMMAND_FLATPAK_BUILD) {
		const gchar *flatpak_build = g_getenv ("GEARLEVER_FLATPAK_EXECUTABLE");
		flatpak_command_arg = g_strdup_printf ("--command=%s", flatpak_build);
		g_ptr_array_add (argv, (gpointer) "flatpak");
		g_ptr_array_add (argv, (gpointer) "run");
		g_ptr_array_add (argv, flatpak_command_arg);
		g_ptr_array_add (argv, (gpointer) GEARLEVER_APP_ID);
	} else {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
				     "Gearlever is not installed");
		return FALSE;
	}

	for (guint i = 0; arguments != NULL && arguments[i] != NULL; i++)
		g_ptr_array_add (argv, (gpointer) arguments[i]);
	g_ptr_array_add (argv, NULL);

	process = g_subprocess_newv ((const gchar * const *) argv->pdata,
				     G_SUBPROCESS_FLAGS_STDOUT_PIPE |
				     G_SUBPROCESS_FLAGS_STDERR_PIPE,
				     error);
	if (process == NULL)
		return FALSE;

	if (!g_subprocess_communicate_utf8 (process, NULL, cancellable,
					    stdout_text, &stderr_text, error))
		return FALSE;

	if (!g_subprocess_get_successful (process)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     "Gearlever failed: %s",
			     (stderr_text != NULL && *stderr_text != '\0') ? stderr_text : *stdout_text);
		g_clear_pointer (stdout_text, g_free);
		return FALSE;
	}

	return TRUE;
}
