#include "gs-api-23.h"
#include <gmodule.h>

typedef GType (*QueryTypeFunc) (void);

typedef struct {
	GMainLoop *loop;
	GsPluginClass *plugin_class;
	guint result_count;
	GError *error;
} QueryState;

static void
list_apps_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	QueryState *state = user_data;
	GsAppList *apps = state->plugin_class->list_apps_finish (GS_PLUGIN (source_object), result,
								 &state->error);
	if (apps != NULL) {
		state->result_count = gs_app_list_length (apps);
		for (guint i = 0; i < state->result_count; i++) {
			GsApp *app = gs_app_list_index (apps, i);
			g_autoptr(GIcon) icon = NULL;
			g_assert_nonnull (gs_app_get_description (app));
			g_assert_cmpint (gs_app_get_bundle_kind (app), ==, AS_BUNDLE_KIND_APPIMAGE);
			icon = gs_app_get_icon_for_size (app, 64, 1, NULL);
			g_assert_nonnull (icon);
		}
		g_object_unref (apps);
	}
	g_main_loop_quit (state->loop);
}

int
main (int argc, char **argv)
{
	GModule *module;
	QueryTypeFunc query_type = NULL;
	GType plugin_type;
	GTypeQuery parent_query = { 0 };
	gpointer plugin_class;

	if (argc != 2)
		return 2;

	module = g_module_open (argv[1], G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
	if (module == NULL)
		g_error ("Could not load plugin: %s", g_module_error ());
	if (!g_module_symbol (module, "gs_plugin_query_type", (gpointer *) &query_type))
		g_error ("Plugin has no gs_plugin_query_type: %s", g_module_error ());

	plugin_type = query_type ();
	g_assert_true (g_type_is_a (plugin_type, G_TYPE_OBJECT));
	g_assert_cmpstr (g_type_name (plugin_type), ==, "GsPluginGearlever");
	g_type_query (g_type_parent (plugin_type), &parent_query);
	g_assert_cmpuint (parent_query.class_size, ==, sizeof (GsPluginClass));

	/* Class initialization writes the vfunc slots, so this also catches an
	 * incompatible or undersized parent-class ABI declaration. */
	plugin_class = g_type_class_ref (plugin_type);
	g_assert_nonnull (plugin_class);
	g_type_class_unref (plugin_class);

	if (g_getenv ("GEARLEVER_TEST_QUERY") != NULL) {
		const gchar *query_mode = g_getenv ("GEARLEVER_TEST_QUERY");
		const gchar *query_property = g_str_equal (query_mode, "installed") ? "is-installed" : "is-for-update";
		g_autoptr(GError) bus_error = NULL;
		g_autoptr(GDBusConnection) session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &bus_error);
		g_autoptr(GDBusConnection) system_bus = NULL;
		g_assert_no_error (bus_error);
		system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &bus_error);
		g_assert_no_error (bus_error);
		g_autoptr(GsPlugin) plugin = g_object_new (plugin_type,
						     "name", "gearlever",
						     "session-bus-connection", session_bus,
						     "system-bus-connection", system_bus,
						     NULL);
		GsAppQuery *query = g_object_new (gs_app_query_get_type (),
						  query_property, GS_APP_QUERY_TRISTATE_TRUE,
						  NULL);
		QueryState state = { g_main_loop_new (NULL, FALSE), GS_PLUGIN_GET_CLASS (plugin), 0, NULL };

		state.plugin_class->list_apps_async (plugin, query, GS_PLUGIN_LIST_APPS_FLAGS_NONE,
						    NULL, NULL, NULL, list_apps_cb, &state);
		g_object_unref (query);
		g_main_loop_run (state.loop);
		g_main_loop_unref (state.loop);
		if (state.error != NULL)
			g_error ("Live Gearlever plugin query failed: %s", state.error->message);
		g_print ("Gearlever plugin %s query returned %u app(s)\n", query_mode, state.result_count);
	}

	g_module_close (module);
	return 0;
}
