/*
 * x11dnd_proxy.c - XdndProxy window support (best-effort).
 */
#include "x11dnd_proxy.h"
#include "x11dnd_util.h"

#include <X11/Xatom.h>
#include <stdlib.h>


Window
x11dnd_proxy_find_target(Display *dpy, Window win)
{
	Atom aware_atom;
	Atom proxy_atom;
	Window walk, parent, root;
	Window *children;
	unsigned int nchildren;
	unsigned long nitems;
	unsigned char *data;

	if (dpy == NULL || win == None) {
		return None;
	}

	aware_atom = XInternAtom(dpy, "XdndAware", True);
	proxy_atom = XInternAtom(dpy, "XdndProxy", True);
	if (aware_atom == None) {
		return None;
	}

	walk = win;
	while (walk != None) {
		if (proxy_atom != None) {
			Window proxy_win;
			proxy_win = x11dnd_validate_proxy(dpy, walk);
			if (proxy_win != None) {
				data = NULL;
				if (x11dnd_get_window_property(dpy, proxy_win,
					aware_atom, XA_ATOM, 32,
					&nitems, &data) == 0) {
					if (nitems >= 1 && data != NULL) {
						free(data);
						return proxy_win;
					}
					if (data) {
						free(data);
					}
				}
			}
		}

		data = NULL;
		if (x11dnd_get_window_property(dpy, walk, aware_atom, XA_ATOM,
			32, &nitems, &data) == 0) {
			if (nitems >= 1 && data != NULL) {
				free(data);
				return walk;
			}
			if (data) {
				free(data);
			}
		}

		if (!XQueryTree(dpy, walk, &root, &parent, &children,
			&nchildren)) {
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


int
x11dnd_proxy_set(Display *dpy, Window win, Window proxy)
{
	Atom proxy_atom;
	Window self_ref;

	if (dpy == NULL || win == None || proxy == None) {
		return 1;
	}

	proxy_atom = XInternAtom(dpy, "XdndProxy", False);
	if (proxy_atom == None) {
		return 1;
	}

	XChangeProperty(dpy, win, proxy_atom, XA_WINDOW, 32,
		PropModeReplace, (unsigned char *)&proxy, 1);

	self_ref = proxy;
	XChangeProperty(dpy, proxy, proxy_atom, XA_WINDOW, 32,
		PropModeReplace, (unsigned char *)&self_ref, 1);

	XFlush(dpy);
	return 0;
}


void
x11dnd_proxy_unset(Display *dpy, Window win)
{
	Atom proxy_atom;

	if (dpy == NULL || win == None) {
		return;
	}

	proxy_atom = XInternAtom(dpy, "XdndProxy", True);
	if (proxy_atom == None) {
		return;
	}

	XDeleteProperty(dpy, win, proxy_atom);
	XFlush(dpy);
}


Window
x11dnd_proxy_check(Display *dpy, Window win)
{
	if (dpy == NULL || win == None) {
		return None;
	}

	return x11dnd_validate_proxy(dpy, win);
}