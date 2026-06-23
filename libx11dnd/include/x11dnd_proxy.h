/*
 * x11dnd_proxy.h - Internal header for XdndProxy window support.
 *
 * This is an INTERNAL header for the libx11dnd library, NOT the public API.
 * It declares higher-level proxy helpers that build on x11dnd_validate_proxy()
 * from x11dnd_util.c.
 *
 * Per the XDnD v5 spec and Chromium's validation, a valid proxy window must
 * have an XdndProxy property pointing to itself. This module provides:
 *   - find_target: walk up the window tree checking both XdndProxy and
 *     XdndAware, returning the effective drop target window
 *   - set/unset: convenience wrappers for setting/removing XdndProxy
 *   - check: thin wrapper around x11dnd_validate_proxy()
 */
#ifndef X11DND_PROXY_H
#define X11DND_PROXY_H

#include <X11/Xlib.h>

/*
 * Find the effective XDnD target window for a given window.
 * Walks up the window tree, checking for XdndAware and XdndProxy.
 * If a window has XdndProxy, validates the proxy (self-referential check),
 * and if valid, checks the proxy window for XdndAware.
 * Returns the effective target window, or None if no XDnD target found.
 */
Window x11dnd_proxy_find_target(Display *dpy, Window win);

/*
 * Set XdndProxy on a window to point to a proxy window.
 * Also sets XdndProxy on the proxy window to point to itself (required
 * for validity per Chromium's validation).
 * Returns 0 on success, non-zero on failure.
 */
int x11dnd_proxy_set(Display *dpy, Window win, Window proxy);

/*
 * Remove XdndProxy property from a window.
 */
void x11dnd_proxy_unset(Display *dpy, Window win);

/*
 * Check if a window is a valid proxy target.
 * Uses x11dnd_validate_proxy() from x11dnd_util.c.
 * Returns the proxy window if valid, None otherwise.
 */
Window x11dnd_proxy_check(Display *dpy, Window win);

#endif /* X11DND_PROXY_H */