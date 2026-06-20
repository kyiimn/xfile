/*
 * Copyright (C) 2025-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * XDG open - MIME type detection and default application launching
 * using GLib/GIO. Provides fallback file opening when the internal
 * type database has no matching action.
 *
 * When USE_GIO is defined at compile time, GAppInfo is used to detect
 * MIME types and launch default applications per the freedesktop.org
 * specification. When USE_GIO is not defined, all functions become
 * no-ops that return failure, and no GLib dependency is required.
 */

#ifndef XDGOPEN_H
#define XDGOPEN_H

#include <Xm/Xm.h>

/**
 * Initializes the XDG open subsystem.
 * When compiled with USE_GIO, performs any one-time GIO setup required.
 * When compiled without USE_GIO, this is a no-op.
 *
 * @return 0 on success, non-zero on failure
 * @throws None
 */
int xdg_open_init(void);

/**
 * Detects the MIME type of the specified file using GIO.
 * Returns a string such as "image/png" or "application/pdf".
 *
 * @param path  Fully qualified file path to inspect
 * @return      MIME type string (caller must free with g_free()),
 *              or NULL on failure or when USE_GIO is not compiled
 * @throws None
 * @example
 *   char *mime = xdg_detect_mime("/home/user/photo.png");
 *   if (mime) { printf("MIME: %s\n", mime); g_free(mime); }
 */
char* xdg_detect_mime(const char *path);

/**
 * Finds the default application for a given MIME type using GIO.
 * Returns the Exec= command line from the .desktop file with
 * field codes (%f, %F, %u, %U) replaced by the file path.
 *
 * @param mime_type  MIME type string (e.g., "image/png")
 * @param file_path  File path to substitute for field codes
 * @param exec_out   Receives the resolved command line (caller must free)
 * @return           0 on success, ENOENT if no default app found,
 *                   or errno on other failure
 * @throws None
 * @example
 *   char *cmd = NULL;
 *   int rv = xdg_default_app("image/png", "/tmp/img.png", &cmd);
 *   if (rv == 0) { printf("Command: %s\n", cmd); free(cmd); }
 */
int xdg_default_app(const char *mime_type, const char *file_path,
	char **exec_out);

/**
 * Opens a file using the XDG default application.
 * Combines xdg_detect_mime() + xdg_default_app() + spawn.
 * This is the primary entry point for the double-click fallback path.
 *
 * @param path  Fully qualified file path to open
 * @return      0 on success, ENOENT if no default application is
 *              registered for the file's MIME type, or errno on failure
 * @throws None
 * @example
 *   int rv = xdg_open_file("/home/user/document.pdf");
 *   if (rv == ENOENT) { printf("No default app\n"); }
 */
int xdg_open_file(const char *path);

/**
 * Cleans up resources held by the XDG open subsystem.
 * When compiled with USE_GIO, releases any GIO resources.
 * When compiled without USE_GIO, this is a no-op.
 */
void xdg_open_cleanup(void);

#endif /* XDGOPEN_H */