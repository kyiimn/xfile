/*
 * x11dnd_xt.h — Xt/Motif integration wrapper for libx11dnd
 *
 * Convenience functions for Xt/Motif applications that want to use
 * libx11dnd without directly dealing with Xlib event dispatching.
 * This header includes <X11/Intrinsic.h> (Xt) and is NOT part of
 * the core Xlib-only API. Include it only in Xt-based code.
 *
 * Copyright (C) 2025-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef X11DND_XT_H
#define X11DND_XT_H

#include <X11/Intrinsic.h>
#include <X11/Xatom.h>
#include "x11dnd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Initialization / Cleanup
 *
 * x11dnd_xt_init() calls x11dnd_init() and registers Xt event handlers
 * on the application shell for XDnD protocol events. Call once at
 * startup after XtAppInitialize() / XtVaOpenApplication().
 *
 * x11dnd_xt_destroy() removes all Xt event handlers and calls
 * x11dnd_destroy(). Call at application shutdown.
 * ========================================================================= */

/**
 * @brief Initialize libx11dnd for Xt-based applications.
 *
 * Calls x11dnd_init() on the display of @p app_shell, then registers
 * Xt event handlers on the shell for ClientMessage (XDnD protocol)
 * and PropertyNotify (INCR transfer) events.
 *
 * @param app_shell  The application shell widget (must be realized).
 * @return 0 on success, non-zero on failure.
 */
int x11dnd_xt_init(Widget app_shell);

/**
 * @brief Shut down libx11dnd Xt integration.
 *
 * Removes event handlers registered by x11dnd_xt_init() and calls
 * x11dnd_destroy() on the display. Safe to call multiple times.
 */
void x11dnd_xt_destroy(void);

/* =========================================================================
 * Drop target registration (Xt convenience)
 *
 * These functions operate on the SHELL window of the widget hierarchy,
 * which is where XdndAware must be set per the XDnD specification.
 * The widget argument is used to find the shell via XtParent() walk.
 * ========================================================================= */

/**
 * @brief Register a widget's shell as an XDnD drop target.
 *
 * Walks up from @p w to its shell ancestor, sets the XdndAware property
 * (version 5) on the shell window, and registers @p callbacks for
 * dispatching XDnD events to that window.
 *
 * @param w         Any widget in the hierarchy (used to find the shell).
 * @param callbacks Callback table (must remain valid while registered).
 * @return 0 on success, non-zero on failure.
 */
int x11dnd_xt_register_target(Widget w, X11DndClass *callbacks);

/**
 * @brief Unregister a widget's shell as an XDnD drop target.
 *
 * Removes the XdndAware property from the shell window and discards
 * any pending target session.
 *
 * @param w  Any widget in the hierarchy (used to find the shell).
 */
void x11dnd_xt_unregister_target(Widget w);

/* =========================================================================
 * Drag source operations (Xt convenience)
 *
 * x11dnd_xt_start_drag() initiates a drag from an Xt widget and
 * installs a XtWorkProc for non-blocking drag tracking.
 * x11dnd_xt_cancel_drag() cancels the active drag.
 * ========================================================================= */

/**
 * @brief Initiate a drag from an Xt widget.
 *
 * Calls x11dnd_start_drag() on the widget's window and installs a
 * XtWorkProc that polls pointer position and dispatches XDnD events.
 * The @p event should be the ButtonPress event that triggered the drag.
 *
 * @param w         The widget initiating the drag (must be realized).
 * @param event     The ButtonPress event that started the drag.
 * @param callbacks Callback table (at least get_drag_data should be set).
 * @return Source session handle, or NULL on failure.
 */
X11DndSourceSession *x11dnd_xt_start_drag(Widget w, XButtonEvent *event,
                                           X11DndClass *callbacks);

/**
 * @brief Cancel an active drag initiated by x11dnd_xt_start_drag().
 *
 * Removes the XtWorkProc and event handler, then calls
 * x11dnd_cancel_drag() on the active session.
 */
void x11dnd_xt_cancel_drag(void);

/**
 * @brief Stop the Xt work proc and timer for drag tracking.
 *
 * Call after XdndDrop has been sent. The session remains alive
 * so that XdndFinished and SelectionRequest can still arrive.
 * The on_drag_end callback will clean up the session.
 */
void x11dnd_xt_stop_tracking(void);

/* =========================================================================
 * Event dispatch (Xt convenience)
 *
 * Feed Xt events into libx11dnd from your Xt event loop.
 * The handler registered by x11dnd_xt_init() calls this automatically,
 * but you can also call it manually if needed.
 * ========================================================================= */

/**
 * @brief Feed an X event into libx11dnd from Xt context.
 *
 * Dispatches ClientMessage, SelectionNotify, SelectionRequest, and
 * SelectionClear events to the appropriate source or target session.
 * This is called automatically by the Xt event handler installed
 * by x11dnd_xt_init(), but can also be called manually.
 *
 * @param w   The widget that received the event (may be NULL for
 *            Selection events).
 * @param ev  The X event.
 * @return 1 if the event was consumed, 0 otherwise.
 */
int x11dnd_xt_process_event(Widget w, XEvent *ev);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* X11DND_XT_H */