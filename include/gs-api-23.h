/*
 * Minimal declarations for GNOME Software plugin ABI 23.
 *
 * GNOME Software's plugin API is private and Ubuntu does not ship its headers.
 * Keep this file aligned with lib/gs-plugin.h from GNOME Software 50.0.  The
 * unused class slots are pointers so their ABI size and ordering are retained.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <gio/gio.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GsApp GsApp;
typedef struct _GsAppList GsAppList;
typedef struct _GsAppQuery GsAppQuery;
typedef struct _GsPlugin GsPlugin;
typedef struct _GsPluginEvent GsPluginEvent;

typedef enum {
	GS_APP_QUERY_TRISTATE_UNSET = -1,
	GS_APP_QUERY_TRISTATE_FALSE = 0,
	GS_APP_QUERY_TRISTATE_TRUE = 1,
} GsAppQueryTristate;

typedef enum {
	GS_APP_STATE_UNKNOWN,
	GS_APP_STATE_INSTALLED,
	GS_APP_STATE_AVAILABLE,
	GS_APP_STATE_AVAILABLE_LOCAL,
	GS_APP_STATE_UPDATABLE,
	GS_APP_STATE_UNAVAILABLE,
	GS_APP_STATE_QUEUED_FOR_INSTALL,
	GS_APP_STATE_INSTALLING,
	GS_APP_STATE_REMOVING,
	GS_APP_STATE_UPDATABLE_LIVE,
} GsAppState;

typedef enum {
	GS_APP_QUALITY_UNKNOWN,
	GS_APP_QUALITY_LOWEST,
	GS_APP_QUALITY_NORMAL,
	GS_APP_QUALITY_HIGHEST,
} GsAppQuality;

typedef enum {
	GS_SIZE_TYPE_UNKNOWN,
	GS_SIZE_TYPE_UNKNOWABLE,
	GS_SIZE_TYPE_VALID,
} GsSizeType;

/* AppStream 1.x enum values used by this plugin. */
typedef enum {
	AS_COMPONENT_KIND_UNKNOWN,
	AS_COMPONENT_KIND_GENERIC,
	AS_COMPONENT_KIND_DESKTOP_APP,
} AsComponentKind;
typedef enum {
	AS_COMPONENT_SCOPE_UNKNOWN,
	AS_COMPONENT_SCOPE_SYSTEM,
	AS_COMPONENT_SCOPE_USER,
} AsComponentScope;
typedef enum {
	AS_BUNDLE_KIND_UNKNOWN,
	AS_BUNDLE_KIND_PACKAGE,
	AS_BUNDLE_KIND_LIMBA,
	AS_BUNDLE_KIND_FLATPAK,
	AS_BUNDLE_KIND_APPIMAGE,
	AS_BUNDLE_KIND_SNAP,
	AS_BUNDLE_KIND_TARBALL,
	AS_BUNDLE_KIND_CABINET,
} AsBundleKind;

typedef enum {
	GS_PLUGIN_LIST_APPS_FLAGS_NONE = 0,
	GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE = 1 << 0,
} GsPluginListAppsFlags;
typedef enum {
	GS_PLUGIN_UPDATE_APPS_FLAGS_NONE = 0,
	GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE = 1 << 0,
	GS_PLUGIN_UPDATE_APPS_FLAGS_NO_DOWNLOAD = 1 << 1,
	GS_PLUGIN_UPDATE_APPS_FLAGS_NO_APPLY = 1 << 2,
} GsPluginUpdateAppsFlags;

typedef enum {
	GS_PLUGIN_ERROR_FAILED,
	GS_PLUGIN_ERROR_NOT_SUPPORTED,
	GS_PLUGIN_ERROR_CANCELLED,
	GS_PLUGIN_ERROR_NO_NETWORK,
	GS_PLUGIN_ERROR_NO_SECURITY,
	GS_PLUGIN_ERROR_NO_SPACE,
	GS_PLUGIN_ERROR_AUTH_REQUIRED,
	GS_PLUGIN_ERROR_AUTH_INVALID,
	GS_PLUGIN_ERROR_PLUGIN_DEPSOLVE_FAILED,
	GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
} GsPluginError;

#define GS_PLUGIN_ERROR (gs_plugin_error_quark ())
#define GS_APP_PROGRESS_UNKNOWN G_MAXUINT

typedef void (*GsPluginProgressCallback) (GsPlugin *plugin,
					 guint progress,
					 gpointer user_data);
typedef void (*GsPluginEventCallback) (GsPlugin *plugin,
				      GsPluginEvent *event,
				      gpointer user_data);
typedef void (*GsPluginAppNeedsUserActionCallback) (GsPlugin *plugin,
						   GsApp *app,
						   gpointer action_screenshot,
						   gpointer user_data);

typedef enum {
	GS_PLUGIN_EVENT_FLAG_NONE = 0,
	GS_PLUGIN_EVENT_FLAG_INVALID = 1 << 0,
	GS_PLUGIN_EVENT_FLAG_VISIBLE = 1 << 1,
	GS_PLUGIN_EVENT_FLAG_WARNING = 1 << 2,
	GS_PLUGIN_EVENT_FLAG_INTERACTIVE = 1 << 3,
} GsPluginEventFlag;

#define GS_TYPE_PLUGIN (gs_plugin_get_type ())
G_DECLARE_DERIVABLE_TYPE (GsPlugin, gs_plugin, GS, PLUGIN, GObject)

struct _GsPluginClass {
	GObjectClass parent_class;

	/* Signals and vfuncs before list_apps_async. */
	gpointer updates_changed;
	gpointer reload;
	gpointer report_event;
	gpointer allow_updates;
	gpointer basic_auth_start;
	gpointer repository_changed;
	gpointer ask_untrusted;
	gpointer adopt_app;
	gpointer setup_async;
	gpointer setup_finish;
	gpointer shutdown_async;
	gpointer shutdown_finish;
	gpointer refine_async;
	gpointer refine_finish;

	void (*list_apps_async) (GsPlugin *plugin,
				 GsAppQuery *query,
				 GsPluginListAppsFlags flags,
				 GsPluginEventCallback event_callback,
				 gpointer event_user_data,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer user_data);
	GsAppList *(*list_apps_finish) (GsPlugin *plugin,
					GAsyncResult *result,
					GError **error);

	/* refresh, distro upgrades, four repository operations and categories */
	gpointer refresh_metadata_async;
	gpointer refresh_metadata_finish;
	gpointer list_distro_upgrades_async;
	gpointer list_distro_upgrades_finish;
	gpointer install_repository_async;
	gpointer install_repository_finish;
	gpointer remove_repository_async;
	gpointer remove_repository_finish;
	gpointer enable_repository_async;
	gpointer enable_repository_finish;
	gpointer disable_repository_async;
	gpointer disable_repository_finish;
	gpointer refine_categories_async;
	gpointer refine_categories_finish;

	void (*update_apps_async) (GsPlugin *plugin,
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
				   gpointer user_data);
	gboolean (*update_apps_finish) (GsPlugin *plugin,
					GAsyncResult *result,
					GError **error);

	/* Remaining ABI 23 vfunc pairs. */
	gpointer install_apps_async;
	gpointer install_apps_finish;
	gpointer uninstall_apps_async;
	gpointer uninstall_apps_finish;
	gpointer cancel_offline_update_async;
	gpointer cancel_offline_update_finish;
	gpointer download_upgrade_async;
	gpointer download_upgrade_finish;
	gpointer trigger_upgrade_async;
	gpointer trigger_upgrade_finish;
	gpointer launch_async;
	gpointer launch_finish;
	gpointer file_to_app_async;
	gpointer file_to_app_finish;
	gpointer url_to_app_async;
	gpointer url_to_app_finish;
	gpointer get_offline_update_state_async;
	gpointer get_offline_update_state_finish;
	gpointer set_offline_update_action_async;
	gpointer set_offline_update_action_finish;
	gpointer padding[19];
};

GQuark gs_plugin_error_quark (void);
void gs_plugin_updates_changed (GsPlugin *plugin);
GsPluginEvent *gs_plugin_event_new (const gchar *first_property_name, ...);
void gs_plugin_event_add_flag (GsPluginEvent *event, GsPluginEventFlag flag);

GsAppQueryTristate gs_app_query_get_is_for_update (GsAppQuery *query);
GsAppQueryTristate gs_app_query_get_is_installed (GsAppQuery *query);
guint gs_app_query_get_n_properties_set (GsAppQuery *query);
GType gs_app_query_get_type (void);

GsAppList *gs_app_list_new (void);
void gs_app_list_add (GsAppList *list, GsApp *app);
GsApp *gs_app_list_index (GsAppList *list, guint index);
guint gs_app_list_length (GsAppList *list);

GsApp *gs_app_new (const gchar *id);
const gchar *gs_app_get_id (GsApp *app);
void gs_app_set_name (GsApp *app, GsAppQuality quality, const gchar *name);
void gs_app_set_summary (GsApp *app, GsAppQuality quality, const gchar *summary);
void gs_app_set_description (GsApp *app, GsAppQuality quality, const gchar *description);
const gchar *gs_app_get_description (GsApp *app);
AsBundleKind gs_app_get_bundle_kind (GsApp *app);
GIcon *gs_app_get_icon_for_size (GsApp *app, guint size, guint scale,
				 const gchar *fallback_icon_name);
void gs_icon_set_width (GIcon *icon, guint width);
void gs_icon_set_height (GIcon *icon, guint height);
void gs_app_set_version (GsApp *app, const gchar *version);
void gs_app_set_update_version (GsApp *app, const gchar *update_version);
void gs_app_set_update_details_text (GsApp *app, const gchar *text);
void gs_app_set_kind (GsApp *app, AsComponentKind kind);
void gs_app_set_scope (GsApp *app, AsComponentScope scope);
void gs_app_set_bundle_kind (GsApp *app, AsBundleKind kind);
void gs_app_set_state (GsApp *app, GsAppState state);
void gs_app_set_progress (GsApp *app, guint percentage);
void gs_app_set_origin (GsApp *app, const gchar *origin);
void gs_app_set_origin_ui (GsApp *app, const gchar *origin_ui);
void gs_app_set_size_installed (GsApp *app, GsSizeType type, guint64 size_bytes);
void gs_app_set_size_download (GsApp *app, GsSizeType type, guint64 size_bytes);
void gs_app_set_management_plugin (GsApp *app, GsPlugin *plugin);
gboolean gs_app_has_management_plugin (GsApp *app, GsPlugin *plugin);
void gs_app_set_metadata (GsApp *app, const gchar *key, const gchar *value);
const gchar *gs_app_get_metadata_item (GsApp *app, const gchar *key);
void gs_app_add_icon (GsApp *app, GIcon *icon);

GType gs_plugin_query_type (void);

G_END_DECLS
