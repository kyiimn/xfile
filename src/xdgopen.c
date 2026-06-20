/*
 * Copyright (C) 2025-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * XDG open implementation using GLib/GIO.
 * When USE_GIO is defined, provides MIME detection and default
 * application launching via g_content_type_guess() and g_app_info_*
 * functions. When USE_GIO is not defined, all functions are no-ops
 * returning failure so the caller can fall back to its own logic.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef USE_GIO
#include <glib.h>
#include <gio/gio.h>
#endif

#include "xdgopen.h"
#include "debug.h"

#ifdef USE_GIO

/**
 * Initializes the GIO subsystem.
 * Calls g_type_init() on GLib < 2.36 (older systems);
 * modern GLib auto-initializes.
 *
 * @return 0 on success, non-zero on failure
 * @throws None
 */
int xdg_open_init(void)
{
	#if !GLIB_CHECK_VERSION(2, 36, 0)
	g_type_init();
	#endif
	return 0;
}

/**
 * Detects MIME type of a file using GIO content type guessing.
 * Uses g_content_type_guess() which examines the filename and
 * optionally the file contents.
 *
 * @param path  Fully qualified file path to inspect
 * @return      Newly allocated MIME type string (free with g_free()),
 *              or NULL on failure
 * @throws None
 */
char* xdg_detect_mime(const char *path)
{
	GFile *gfile;
	GFileInfo *info;
	char *mime_type;
	char *basename;

	if(!path) return NULL;

	basename = g_path_get_basename(path);
	if(!basename) return NULL;

	gfile = g_file_new_for_path(path);
	if(!gfile) {
		g_free(basename);
		return NULL;
	}

	info = g_file_query_info(gfile,
		G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
		G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE,
		G_FILE_QUERY_INFO_NONE, NULL, NULL);

	if(info) {
		const char *content_type = g_file_info_get_content_type(info);
		if(content_type) {
			mime_type = g_content_type_get_mime_type(content_type);
			if(!mime_type) {
				mime_type = g_strdup(content_type);
			}
		} else {
			gboolean uncertain;
			char *ctype = g_content_type_guess(
				basename, NULL, 0, &uncertain);
			mime_type = g_content_type_get_mime_type(ctype);
			if(!mime_type) {
				mime_type = g_strdup(ctype);
			}
			g_free(ctype);
		}
		g_object_unref(info);
	} else {
		gboolean uncertain;
		char *ctype = g_content_type_guess(
			basename, NULL, 0, &uncertain);
		mime_type = g_content_type_get_mime_type(ctype);
		if(!mime_type) {
			mime_type = g_strdup(ctype);
		}
		g_free(ctype);
	}

	g_object_unref(gfile);
	g_free(basename);

	return mime_type;
}

/**
 * Resolves .desktop field codes in an Exec line.
 * Replaces %f, %F, %u, %U with the file path; strips %d, %D,
 * %n, %N, %i, %c, %k, %v, %m (deprecated codes per spec).
 *
 * @param exec_template  Exec line from a .desktop file
 * @param file_path       Path to substitute for field codes
 * @return               Newly allocated command line string,
 *                       or NULL on failure (caller must free)
 * @throws None
 * @example
 *   char *cmd = resolve_exec_codes("eog %f", "/tmp/img.png");
 *   // cmd == "eog /tmp/img.png"
 */
static char* resolve_exec_codes(const char *exec_template,
	const char *file_path)
{
	GString *result;
	const char *p;

	if(!exec_template || !file_path) return NULL;

	result = g_string_new(NULL);
	p = exec_template;

	while(*p) {
		if(*p == '%') {
			p++;
			switch(*p) {
			case 'f': case 'F':
				g_string_append(result, file_path);
				break;
			case 'u': case 'U': {
				char *uri = g_filename_to_uri(file_path, NULL, NULL);
				if(uri) {
					g_string_append(result, uri);
					g_free(uri);
				} else {
					g_string_append(result, file_path);
				}
				break;
			}
			case 'd': case 'D': case 'n': case 'N':
			case 'i': case 'c': case 'k': case 'v': case 'm':
				/* deprecated or irrelevant field codes; skip */
				break;
			case '%':
				g_string_append_c(result, '%');
				break;
			default:
				/* unknown code; keep the percent + char */
				g_string_append_c(result, '%');
				if(*p) g_string_append_c(result, *p);
				break;
			}
			if(*p) p++;
		} else {
			g_string_append_c(result, *p);
			p++;
		}
	}

	return g_string_free(result, FALSE);
}

/**
 * Finds the default application for a MIME type via GIO.
 * Looks up GAppInfo for the MIME type and resolves its Exec
 * line with the provided file path.
 *
 * @param mime_type  MIME type string (e.g., "image/png")
 * @param file_path  File path to substitute for field codes
 * @param exec_out   Receives the resolved command line (caller must free)
 * @return           0 on success, ENOENT if no default app found,
 *                   or errno on other failure
 * @throws None
 */
int xdg_default_app(const char *mime_type, const char *file_path,
	char **exec_out)
{
	GAppInfo *app_info;
	char *exec_raw;
	char *exec_resolved;

	if(!mime_type || !file_path || !exec_out) return EINVAL;

	*exec_out = NULL;

	app_info = g_app_info_get_default_for_type(mime_type, FALSE);
	if(!app_info) return ENOENT;

	exec_raw = (char*)g_app_info_get_commandline(app_info);
	if(!exec_raw) {
		g_object_unref(app_info);
		return ENOENT;
	}

	exec_resolved = resolve_exec_codes(exec_raw, file_path);
	g_object_unref(app_info);

	if(!exec_resolved) return ENOENT;

	*exec_out = strdup(exec_resolved);
	g_free(exec_resolved);

	return *exec_out ? 0 : ENOMEM;
}

/**
 * Opens a file using the XDG default application.
 * Detects the MIME type with xdg_detect_mime(), then resolves
 * the default app with xdg_default_app(), and spawns it via
 * spawn_cs_command() from exec.c.
 *
 * @param path  Fully qualified file path to open
 * @return      0 on success, ENOENT if no default app registered,
 *              or errno on failure
 * @throws None
 */
int xdg_open_file(const char *path)
{
	char *mime_type;
	char *cmd = NULL;
	int rv;

	if(!path) return EINVAL;

	mime_type = xdg_detect_mime(path);
	if(!mime_type) return ENOENT;

	rv = xdg_default_app(mime_type, path, &cmd);
	g_free(mime_type);

	if(rv) return rv;

	/* cmd is already resolved with field codes replaced;
	 * spawn_cs_command handles argument splitting and vfork/exec */
	extern int spawn_cs_command(const char*);
	rv = spawn_cs_command(cmd);
	free(cmd);

	return rv;
}

/**
 * Cleans up GIO resources.
 * Currently a no-op since GIO uses refcounting via GObject;
 * retained for future resource management if needed.
 */
void xdg_open_cleanup(void)
{
	/* no-op; GIO manages its own lifecycle via GObject refcounting */
}

#else /* !USE_GIO */

/*
 * Stub implementations when compiled without GIO support.
 * All functions return failure so callers fall back gracefully.
 */

int xdg_open_init(void)
{
	return -1;
}

char* xdg_detect_mime(const char *path)
{
	(void)path;
	return NULL;
}

int xdg_default_app(const char *mime_type, const char *file_path,
	char **exec_out)
{
	(void)mime_type;
	(void)file_path;
	(void)exec_out;
	return ENOENT;
}

int xdg_open_file(const char *path)
{
	(void)path;
	return ENOENT;
}

void xdg_open_cleanup(void)
{
	/* no-op */
}

#endif /* USE_GIO */