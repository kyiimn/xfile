/*
 * x11dnd_util.c - X11 window utility helpers for XDnD.
 */
#include "x11dnd_util.h"

#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_window_exists_error;

static int
window_exists_error_handler(Display *dpy, XErrorEvent *ev)
{
	(void)dpy;
	(void)ev;
	g_window_exists_error = 1;
	return 0;
}


Window
x11dnd_find_window_at_point(Display *dpy, Window root, int x, int y)
{
	Window current, child, parent;
	int dest_x, dest_y;

	current = root;
	child = None;
	for (;;) {
		if (!XTranslateCoordinates(dpy, root, current, x, y,
			&dest_x, &dest_y, &child)) {
			return root;
		}
		if (child == None) {
			return current;
		}
		parent = current;
		current = child;
		(void)parent;
	}
}


Window
x11dnd_find_aware_ancestor(Display *dpy, Window win)
{
	Atom aware;
	Window walk, parent, root;
	Window *children;
	unsigned int nchildren;
	unsigned long nitems;
	unsigned char *data;

	aware = XInternAtom(dpy, "XdndAware", True);
	if (aware == None) {
		return None;
	}

	walk = win;
	while (walk != None) {
		data = NULL;
		if (x11dnd_get_window_property(dpy, walk, aware, XA_ATOM, 32,
			&nitems, &data) == 0) {
			if (nitems >= 1 && data != NULL) {
				free(data);
				return walk;
			}
			if (data) {
				free(data);
			}
		}

		if (!XQueryTree(dpy, walk, &root, &parent, &children, &nchildren)) {
			break;
		}
		if (children) {
			XFree(children);
		}
		if (parent == None || parent == root) {
			break;
		}
		walk = parent;
	}
	return None;
}


Window
x11dnd_validate_proxy(Display *dpy, Window win)
{
	Atom proxy_atom;
	Window proxy_win;
	unsigned long nitems;
	unsigned char *data;

	proxy_atom = XInternAtom(dpy, "XdndProxy", True);
	if (proxy_atom == None) {
		return None;
	}

	data = NULL;
	if (x11dnd_get_window_property(dpy, win, proxy_atom, XA_WINDOW, 32,
		&nitems, &data) != 0) {
		return None;
	}
	if (nitems < 1 || data == NULL) {
		if (data) {
			free(data);
		}
		return None;
	}

	proxy_win = ((Window *)data)[0];
	free(data);

	if (proxy_win == None) {
		return None;
	}

	if (!x11dnd_window_exists(dpy, proxy_win)) {
		return None;
	}

	data = NULL;
	if (x11dnd_get_window_property(dpy, proxy_win, proxy_atom, XA_WINDOW,
		32, &nitems, &data) != 0) {
		return None;
	}
	if (nitems < 1 || data == NULL) {
		if (data) {
			free(data);
		}
		return None;
	}

	if (((Window *)data)[0] == proxy_win) {
		free(data);
		return proxy_win;
	}

	free(data);
	return None;
}


int
x11dnd_get_window_property(Display *dpy, Window win, Atom prop, Atom type,
	int format, unsigned long *nitems, unsigned char **data)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems_ret, bytes_after;
	unsigned char *prop_ret;
	int status;

	if (nitems) {
		*nitems = 0;
	}
	if (data) {
		*data = NULL;
	}

	status = XGetWindowProperty(dpy, win, prop, 0, 0x7FFFFFFF, False,
		type, &actual_type, &actual_format, &nitems_ret, &bytes_after,
		&prop_ret);

	if (status != Success) {
		return 1;
	}
	if (actual_type != type || actual_format != format) {
		if (prop_ret) {
			XFree(prop_ret);
		}
		return 1;
	}
	if (nitems_ret == 0 || prop_ret == NULL) {
		if (prop_ret) {
			XFree(prop_ret);
		}
		return 1;
	}

	{
		size_t unit;
		if (format == 32) {
			unit = sizeof(long);
		} else {
			unit = (size_t)format / 8;
		}
		size_t sz = nitems_ret * unit;
		unsigned char *copy;

		copy = malloc(sz);
		if (!copy) {
			XFree(prop_ret);
			return 1;
		}
		memcpy(copy, prop_ret, sz);
		XFree(prop_ret);

		if (nitems) {
			*nitems = nitems_ret;
		}
		if (data) {
			*data = copy;
		} else {
			free(copy);
		}
	}

	return 0;
}


int
x11dnd_send_client_message(Display *dpy, Window dest, Window source,
	Atom type, long data[5], Time timestamp)
{
	XClientMessageEvent ev;
	int status;
	char *type_name;

	(void)timestamp;

	memset(&ev, 0, sizeof(ev));
	ev.type = ClientMessage;
	ev.display = dpy;
	ev.window = dest;
	ev.message_type = type;
	ev.format = 32;
	ev.data.l[0] = source;
	ev.data.l[1] = data[1];
	ev.data.l[2] = data[2];
	ev.data.l[3] = data[3];
	ev.data.l[4] = data[4];

	type_name = XGetAtomName(dpy, type);
	fprintf(stderr, "XSendEvent: type=%s dest=0x%lx src=0x%lx d1=%ld d2=0x%lx d3=%ld\n",
		type_name ? type_name : "(null)",
		(unsigned long)dest, (unsigned long)source,
		data[1], (unsigned long)data[2], data[3]);
	if (type_name) XFree(type_name);

	status = XSendEvent(dpy, dest, False, NoEventMask,
		(XEvent *)&ev);
	XFlush(dpy);

	fprintf(stderr, "XSendEvent returned: %d\n", status);

	return status ? 0 : 1;
}


Window
x11dnd_get_root_window(Display *dpy, Window win)
{
	Window root, parent;
	Window *children;
	unsigned int nchildren;

	if (!XQueryTree(dpy, win, &root, &parent, &children, &nchildren)) {
		return None;
	}
	if (children) {
		XFree(children);
	}
	return root;
}


Bool
x11dnd_window_exists(Display *dpy, Window win)
{
	XErrorHandler old_handler;
	Window root, parent;
	Window *children;
	unsigned int nchildren;
	Bool exists;

	g_window_exists_error = 0;
	XSync(dpy, False);

	old_handler = XSetErrorHandler(window_exists_error_handler);

	if (XQueryTree(dpy, win, &root, &parent, &children, &nchildren)) {
		if (children) {
			XFree(children);
		}
		exists = True;
	} else {
		exists = False;
	}

	XSync(dpy, False);
	XSetErrorHandler(old_handler);

	if (g_window_exists_error) {
		return False;
	}
	return exists;
}