#include "gearlever-utils.h"

static void
test_parse_native_output (void)
{
	const gchar *output =
		"My Great App   [Update available, GithubUpdater|embedded]   /home/me/AppImages/my great.appimage\n"
		"Tool           [Update available, StaticFileUpdater]        /opt/apps/tool.AppImage\n";
	g_autoptr(GPtrArray) updates = gearlever_parse_updates (output);
	GearleverUpdate *first;

	g_assert_cmpuint (updates->len, ==, 2);
	first = g_ptr_array_index (updates, 0);
	g_assert_cmpstr (first->name, ==, "My Great App");
	g_assert_cmpstr (first->path, ==, "/home/me/AppImages/my great.appimage");
}

static void
test_parse_ignores_messages (void)
{
	const gchar *output =
		"No updates available\n"
		"Broken [Update available, GithubUpdater] relative.appimage\n"
		"Internet connection not available\n";
	g_autoptr(GPtrArray) updates = gearlever_parse_updates (output);
	g_assert_cmpuint (updates->len, ==, 0);
}

static void
test_parse_json_schema_v1 (void)
{
	const gchar *output =
		"{\"schema_version\":1,\"updates\":[{"
		"\"name\":\"My \\\"Great\\\" App\","
		"\"path\":\"/home/me/AppImages/my app.AppImage\","
		"\"desktop_id\":\"my-app.desktop\","
		"\"current_version\":\"1.0\","
		"\"available_version\":null,"
		"\"download_size\":12345,"
		"\"manager\":\"GithubUpdater\","
		"\"embedded_source\":true,"
		"\"running\":false,"
		"\"future_field\":{\"nested\":[1,true,null]}"
		"}]}";
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) updates = gearlever_parse_updates_json (output, &error);
	GearleverUpdate *update;

	g_assert_no_error (error);
	g_assert_nonnull (updates);
	g_assert_cmpuint (updates->len, ==, 1);
	update = g_ptr_array_index (updates, 0);
	g_assert_cmpstr (update->name, ==, "My \"Great\" App");
	g_assert_cmpstr (update->desktop_id, ==, "my-app.desktop");
	g_assert_cmpstr (update->current_version, ==, "1.0");
	g_assert_null (update->available_version);
	g_assert_true (update->has_download_size);
	g_assert_cmpuint (update->download_size, ==, 12345);
	g_assert_cmpstr (update->manager, ==, "GithubUpdater");
	g_assert_true (update->embedded_source);
	g_assert_true (update->has_running);
	g_assert_false (update->running);
}

static void
test_rejects_unknown_json_schema (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) updates = gearlever_parse_updates_json (
		"{\"schema_version\":2,\"updates\":[]}", &error);
	g_assert_null (updates);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
test_parse_installed_output (void)
{
	const gchar *output =
		"MCP Inspector       [1.4.1]          [GithubUpdater]                /home/me/AppImages/mcp inspector.appimage\n"
		"Cursor              [Not specified]  [StaticFileUpdater|embedded]   /home/me/AppImages/cursor.appimage\n";
	g_autoptr(GPtrArray) apps = gearlever_parse_installed (output);
	GearleverUpdate *first;
	GearleverUpdate *second;

	g_assert_cmpuint (apps->len, ==, 2);
	first = g_ptr_array_index (apps, 0);
	second = g_ptr_array_index (apps, 1);
	g_assert_cmpstr (first->name, ==, "MCP Inspector");
	g_assert_cmpstr (first->current_version, ==, "1.4.1");
	g_assert_cmpstr (first->manager, ==, "GithubUpdater");
	g_assert_cmpstr (first->path, ==, "/home/me/AppImages/mcp inspector.appimage");
	g_assert_null (second->current_version);
	g_assert_cmpstr (second->manager, ==, "StaticFileUpdater");
	g_assert_true (second->embedded_source);
}

static void
test_parse_installed_json (void)
{
	const gchar *output =
		"{\"schema_version\":1,\"installed\":[{"
		"\"name\":\"T3 Code (Nightly)\","
		"\"path\":\"/home/me/AppImages/t3.appimage\","
		"\"desktop_id\":\"t3.desktop\","
		"\"current_version\":\"0.0.29\","
		"\"available_version\":null,"
		"\"download_size\":null,"
		"\"manager\":\"GithubUpdater\","
		"\"embedded_source\":false,"
		"\"running\":true}]}";
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) apps = gearlever_parse_installed_json (output, &error);
	GearleverUpdate *app;

	g_assert_no_error (error);
	g_assert_nonnull (apps);
	g_assert_cmpuint (apps->len, ==, 1);
	app = g_ptr_array_index (apps, 0);
	g_assert_cmpstr (app->name, ==, "T3 Code (Nightly)");
	g_assert_cmpstr (app->desktop_id, ==, "t3.desktop");
	g_assert_cmpstr (app->current_version, ==, "0.0.29");
	g_assert_true (app->has_running);
	g_assert_true (app->running);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/gearlever/parser/native-output", test_parse_native_output);
	g_test_add_func ("/gearlever/parser/ignores-messages", test_parse_ignores_messages);
	g_test_add_func ("/gearlever/parser/json-schema-v1", test_parse_json_schema_v1);
	g_test_add_func ("/gearlever/parser/rejects-unknown-schema", test_rejects_unknown_json_schema);
	g_test_add_func ("/gearlever/parser/installed-output", test_parse_installed_output);
	g_test_add_func ("/gearlever/parser/installed-json", test_parse_installed_json);
	return g_test_run ();
}
