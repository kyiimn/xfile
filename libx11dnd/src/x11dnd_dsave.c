/*
 * x11dnd_dsave.c - XdndDirectSave (XDS) protocol implementation.
 *
 * Implements both source and target sides of the XDS protocol, which
 * allows a drop target to request the drag source to save data directly
 * to a file path instead of transferring via the X selection mechanism.
 */
#include "x11dnd_dsave.h"
#include "x11dnd_atoms.h"
#include "x11dnd_util.h"
#include "x11dnd_source.h"

#include <X11/Xatom.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

Bool
x11dnd_is_direct_save(Atom target)
{
	const X11DndAtoms *atoms;

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return False;
	}
	return (target == atoms->XdndDirectSave) ? True : False;
}

int
x11dnd_dsave_set_path(Display *dpy, Window win, const char *path)
{
	const X11DndAtoms *atoms;

	if (dpy == NULL || win == None || path == NULL) {
		return 1;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return 1;
	}

	XChangeProperty(dpy, win, atoms->XdndDirectSave0, XA_STRING, 8,
		PropModeReplace, (const unsigned char *)path,
		(int)strlen(path));
	XFlush(dpy);
	return 0;
}

char *
x11dnd_dsave_get_path(Display *dpy, Window win)
{
	const X11DndAtoms *atoms;
	unsigned long nitems;
	unsigned char *data;
	char *result;

	if (dpy == NULL || win == None) {
		return NULL;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return NULL;
	}

	data = NULL;
	if (x11dnd_get_window_property(dpy, win, atoms->XdndDirectSave0,
		XA_STRING, 8, &nitems, &data) != 0) {
		return NULL;
	}
	if (data == NULL || nitems == 0) {
		if (data) {
			free(data);
		}
		return NULL;
	}

	result = malloc(nitems + 1);
	if (result == NULL) {
		free(data);
		return NULL;
	}
	memcpy(result, data, nitems);
	result[nitems] = '\0';
	free(data);
	return result;
}

static int
write_file(const char *path, const unsigned char *data, unsigned long len)
{
	int fd;
	ssize_t written;
	unsigned long total;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		return -1;
	}

	total = 0;
	while (total < len) {
		written = write(fd, data + total, len - total);
		if (written < 0) {
			close(fd);
			return -1;
		}
		total += (unsigned long)written;
	}

	if (close(fd) < 0) {
		return -1;
	}
	return 0;
}

static int
set_result_property(Display *dpy, Window win, Atom prop,
	const char *result_str)
{
	const X11DndAtoms *atoms;

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return 1;
	}

	(void)atoms;
	XChangeProperty(dpy, win, prop, XA_STRING, 8, PropModeReplace,
		(const unsigned char *)result_str, (int)strlen(result_str));
	XFlush(dpy);
	return 0;
}

int
x11dnd_dsave_handle_request(Display *dpy, Window requestor,
	Atom property, Time time, void *source_sess)
{
	X11DndSourceSession *sess;
	char *save_path;
	unsigned char *drag_data;
	unsigned long drag_len;
	int drag_format;
	int save_ok;

	(void)time;

	if (dpy == NULL || requestor == None || property == None ||
		source_sess == NULL) {
		return 1;
	}

	sess = (X11DndSourceSession *)source_sess;

	save_path = x11dnd_dsave_get_path(dpy, requestor);
	if (save_path == NULL) {
		set_result_property(dpy, requestor, property, "!");
		return 0;
	}

	drag_data = NULL;
	drag_len = 0;
	drag_format = 8;

	if (sess->callbacks && sess->callbacks->get_drag_data) {
		sess->callbacks->get_drag_data(sess,
			x11dnd_get_atoms()->XdndDirectSave,
			&drag_data, &drag_len, &drag_format);
	}

	if (drag_data == NULL || drag_len == 0) {
		free(save_path);
		set_result_property(dpy, requestor, property, "!");
		return 0;
	}

	save_ok = write_file(save_path, drag_data, drag_len);

	XFree(drag_data);

	if (save_ok != 0) {
		free(save_path);
		set_result_property(dpy, requestor, property, "!");
		return 0;
	}

	{
		char *uri;
		size_t uri_len;

		uri_len = strlen("file://") + strlen(save_path) + 1;
		uri = malloc(uri_len);
		if (uri == NULL) {
			free(save_path);
			set_result_property(dpy, requestor, property, "!");
			return 0;
		}
		snprintf(uri, uri_len, "file://%s", save_path);
		set_result_property(dpy, requestor, property, uri);
		free(uri);
	}

	free(save_path);
	return 0;
}

char *
x11dnd_dsave_get_result(Display *dpy, Window win, Atom property)
{
	unsigned long nitems;
	unsigned char *data;
	char *result;

	if (dpy == NULL || win == None || property == None) {
		return NULL;
	}

	data = NULL;
	if (x11dnd_get_window_property(dpy, win, property, XA_STRING, 8,
		&nitems, &data) != 0) {
		return NULL;
	}
	if (data == NULL || nitems == 0) {
		if (data) {
			free(data);
		}
		return NULL;
	}

	result = malloc(nitems + 1);
	if (result == NULL) {
		free(data);
		return NULL;
	}
	memcpy(result, data, nitems);
	result[nitems] = '\0';
	free(data);
	return result;
}