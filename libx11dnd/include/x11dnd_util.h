/*
 * x11dnd_util.h - Internal header for X11 window utility helpers.
 *
 * This is an INTERNAL header for the libx11dnd library, NOT the public API.
 * It declares utility functions for X11 window operations needed by the
 * XDnD protocol implementation.
 */
#ifndef X11DND_UTIL_H
#define X11DND_UTIL_H

#include <X11/Xlib.h>

/*
 * Find the deepest child window at the given coordinates.
 * Uses XQueryPointer() and recursively descends into child windows.
 * Returns the deepest child window, or root if no child at that point.
 * Returns root on error.
 */
Window x11dnd_find_window_at_point(Display *dpy, Window root, int x, int y);

/*
 * Walk up the window tree checking XdndAware property.
 * Returns the window with XdndAware set, or None if not found.
 * Uses x11dnd_get_window_property() internally.
 * Uses XInternAtom(dpy, "XdndAware", True) to avoid circular dependency
 * on the atoms module.
 */
Window x11dnd_find_aware_ancestor(Display *dpy, Window win);

/*
 * Check XdndProxy property on win and validate self-reference.
 * Returns the proxy window ID if valid (proxy points to itself),
 * or None if no proxy or invalid proxy.
 * Per Chromium's validation: proxy window must have XdndProxy
 * pointing to itself.
 */
Window x11dnd_validate_proxy(Display *dpy, Window win);

/*
 * Safe XGetWindowProperty wrapper.
 * Returns 0 on success, non-zero on failure.
 * On success, *data is malloc'd and must be freed by caller.
 * Handles None/empty properties gracefully (returns non-zero, *data=NULL).
 */
int x11dnd_get_window_property(Display *dpy, Window win, Atom prop,
    Atom type, int format, unsigned long *nitems, unsigned char **data);

/*
 * XSendEvent wrapper for XDnD ClientMessage messages.
 * Sends a ClientMessage with the given type and 5 long data fields.
 * Returns 0 on success, non-zero on failure.
 */
int x11dnd_send_client_message(Display *dpy, Window dest, Window source,
    Atom type, long data[5], Time timestamp);

/*
 * Get the root window of the screen containing the given window.
 * Returns the root window, or None on error.
 */
Window x11dnd_get_root_window(Display *dpy, Window win);

/*
 * Check if a window exists (is valid).
 * Returns True if the window exists, False otherwise.
 */
Bool x11dnd_window_exists(Display *dpy, Window win);

#endif /* X11DND_UTIL_H */