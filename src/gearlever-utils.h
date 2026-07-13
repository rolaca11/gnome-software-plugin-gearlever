#pragma once

#include <gio/gio.h>

typedef struct {
	gchar *name;
	gchar *path;
	gchar *desktop_id;
	gchar *current_version;
	gchar *available_version;
	gchar *manager;
	guint64 download_size;
	gboolean has_download_size;
	gboolean embedded_source;
	gboolean running;
	gboolean has_running;
} GearleverUpdate;

typedef enum {
	GEARLEVER_COMMAND_NONE,
	GEARLEVER_COMMAND_NATIVE,
	GEARLEVER_COMMAND_FLATPAK,
	GEARLEVER_COMMAND_FLATPAK_BUILD,
} GearleverCommand;

void gearlever_update_free (GearleverUpdate *update);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GearleverUpdate, gearlever_update_free)
GPtrArray *gearlever_parse_updates_json (const gchar *output, GError **error);
GPtrArray *gearlever_parse_installed_json (const gchar *output, GError **error);
GPtrArray *gearlever_parse_updates (const gchar *output);
GPtrArray *gearlever_parse_installed (const gchar *output);
GearleverCommand gearlever_find_command (void);
gboolean gearlever_run (GearleverCommand command,
			const gchar * const *arguments,
			GCancellable *cancellable,
			gchar **stdout_text,
			GError **error);
