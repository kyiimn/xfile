/*
 * x11dnd_xt.c — Xt/Motif integration wrapper for libx11dnd
 *
 * Convenience functions for Xt-based applications. This file is the
 * ONLY file in libx11dnd that includes Xt headers; the core library
 * remains pure Xlib.
 *
 * Copyright (C) 2025-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <string.h>
#include <X11/Intrinsic.h>
#include <X11/Xatom.h>
#include "x11dnd_xt.h"
#include "x11dnd_atoms.h"
#include "x11dnd_source.h"
#include "x11dnd_target.h"

/* Module-level state for the Xt integration */
static Display *xt_dpy = NULL;
static Widget xt_shell = NULL;
static XtWorkProcId xt_work_proc_id = None;
static XtIntervalId xt_poll_timer_id = None;
static X11DndSourceSession *xt_active_source = NULL;

/* Event mask for XDnD events on the shell */
#define XT_DND_EVENT_MASK (NoEventMask)
/* Events we grab with nonmaskable=True: ClientMessage */
/* PropertyNotify for INCR transfers is also nonmaskable via XtAddEventHandler */

/* Poll interval for drag tracking (ms) */
#define XT_DND_POLL_MS 50

/* -------------------------------------------------------------------
 * Helper: walk up the widget hierarchy to find the shell widget
 * ------------------------------------------------------------------- */
static Widget
find_shell(Widget w)
{
	while (w != NULL && !XtIsShell(w))
		w = XtParent(w);
	return w;
}

/* -------------------------------------------------------------------
 * Event handler: dispatch X events to libx11dnd
 * ------------------------------------------------------------------- */
static void
xt_event_handler(Widget w, XtPointer client_data, XEvent *ev,
	Boolean *continue_to_dispatch)
{
	(void)w;
	(void)client_data;
	(void)continue_to_dispatch;

	if (ev == NULL)
		return;

	(void)x11dnd_xt_process_event(w, ev);
}

/* -------------------------------------------------------------------
 * Work procedure: poll pointer position during drag
 * ------------------------------------------------------------------- */
static Boolean
xt_drag_work_proc(XtPointer client_data)
{
	X11DndSourceSession *sess;
	Display *dpy;
	Window root, child;
	int root_x, root_y, child_x, child_y;
	unsigned int mask;
	Bool query_ok;
	XEvent ev;

	sess = (X11DndSourceSession *)client_data;
	if (sess == NULL)
		return True;

	dpy = x11dnd_source_get_display(sess);
	if (dpy == NULL)
		return True;

	query_ok = XQueryPointer(dpy, DefaultRootWindow(dpy),
		&root, &child, &root_x, &root_y,
		&child_x, &child_y, &mask);

	if (query_ok) {
		memset(&ev, 0, sizeof(ev));
		ev.type = MotionNotify;
		ev.xmotion.root = root;
		ev.xmotion.subwindow = child;
		ev.xmotion.x_root = root_x;
		ev.xmotion.y_root = root_y;
		ev.xmotion.state = mask;
		(void)x11dnd_source_process_event(&ev);
	}

	return False;
}

/* -------------------------------------------------------------------
 * Timer callback: periodic drag position updates
 * ------------------------------------------------------------------- */
static void
xt_poll_timer_cb(XtPointer client_data, XtIntervalId *id)
{
	X11DndSourceSession *sess;
	Display *dpy;
	Window root, child;
	int root_x, root_y, child_x, child_y;
	unsigned int mask;
	Bool query_ok;
	XEvent ev;

	(void)id;

	sess = (X11DndSourceSession *)client_data;
	if (sess == NULL)
		return;

	dpy = x11dnd_source_get_display(sess);
	if (dpy == NULL)
		return;

	query_ok = XQueryPointer(dpy, DefaultRootWindow(dpy),
		&root, &child, &root_x, &root_y,
		&child_x, &child_y, &mask);

	if (query_ok) {
		memset(&ev, 0, sizeof(ev));
		ev.type = MotionNotify;
		ev.xmotion.root = root;
		ev.xmotion.subwindow = child;
		ev.xmotion.x_root = root_x;
		ev.xmotion.y_root = root_y;
		ev.xmotion.state = mask;
		(void)x11dnd_source_process_event(&ev);
	}

	xt_poll_timer_id = XtAppAddTimeOut(
		XtWidgetToApplicationContext(xt_shell),
		XT_DND_POLL_MS, xt_poll_timer_cb, client_data);
}

/* ===================================================================
 * Public API: Library lifecycle
 * =================================================================== */

int
x11dnd_xt_init(Widget app_shell)
{
	Display *dpy;
	Widget real_shell;
	int rc;

	if (app_shell == NULL)
		return -1;

	dpy = XtDisplay(app_shell);
	if (dpy == NULL)
		return -1;

	rc = x11dnd_init(dpy);
	if (rc != 0)
		return rc;

	real_shell = find_shell(app_shell);
	if (real_shell == NULL)
		return -1;

	xt_dpy = dpy;
	xt_shell = real_shell;

	/* Register event handlers on the shell for XDnD events.
	 * NoEventMask with grab=True catches non-maskable ClientMessage
	 * events. We also need PropertyNotify for INCR transfers and
	 * Selection events.
	 *
	 * XtAddEventHandler works on unrealized widgets; events will be
	 * dispatched once the shell is realized. */
	XtAddEventHandler(real_shell,
		PropertyChangeMask,
		True, /* non-maskable events too (ClientMessage) */
		xt_event_handler,
		NULL);

	return 0;
}

void
x11dnd_xt_destroy(void)
{
	if (xt_shell != NULL) {
		XtRemoveEventHandler(xt_shell,
			PropertyChangeMask,
			True,
			xt_event_handler,
			NULL);

		if (xt_active_source != NULL) {
			x11dnd_cancel_drag(xt_active_source);
			xt_active_source = NULL;
		}

		if (xt_work_proc_id != None) {
			XtRemoveWorkProc(xt_work_proc_id);
			xt_work_proc_id = None;
		}
		if (xt_poll_timer_id != None) {
			XtRemoveTimeOut(xt_poll_timer_id);
			xt_poll_timer_id = None;
		}
	}

	if (xt_dpy != NULL) {
		x11dnd_destroy(xt_dpy);
		xt_dpy = NULL;
	}

	xt_shell = NULL;
}

/* ===================================================================
 * Public API: Drop target registration
 * =================================================================== */

int
x11dnd_xt_register_target(Widget w, X11DndClass *callbacks)
{
	Widget shell;
	Window shell_win;
	Display *dpy;

	if (w == NULL || callbacks == NULL)
		return -1;

	shell = find_shell(w);
	if (shell == NULL)
		return -1;

	if (!XtIsRealized(shell))
		return -1;

	dpy = XtDisplay(shell);
	shell_win = XtWindow(shell);

	return x11dnd_register_target(dpy, shell_win, callbacks, NULL);
}

void
x11dnd_xt_unregister_target(Widget w)
{
	Widget shell;
	Window shell_win;
	Display *dpy;

	if (w == NULL)
		return;

	shell = find_shell(w);
	if (shell == NULL)
		return;

	dpy = XtDisplay(shell);
	shell_win = XtWindow(shell);

	x11dnd_unregister_target(dpy, shell_win);
}

/* ===================================================================
 * Public API: Drag source operations
 * =================================================================== */

X11DndSourceSession *
x11dnd_xt_start_drag(Widget w, XButtonEvent *event, X11DndClass *callbacks)
{
	Display *dpy;
	Widget shell;
	Window source_win;
	X11DndSourceSession *sess;
	const X11DndAtoms *atoms;
	Atom types[4];
	Atom actions[2];
	Time start_time;

	if (w == NULL || event == NULL || callbacks == NULL)
		return NULL;

	if (!XtIsRealized(w))
		return NULL;

	dpy = XtDisplay(w);

	/* Use the shell window as the drag source, not the child widget.
	 * The shell window is where XdndAware is registered and where the
	 * Xt event handler is installed. Using a child widget window as
	 * the source conflicts with Motif DnD which may also try to own
	 * XdndSelection on that window. */
	shell = find_shell(w);
	if (shell == NULL || !XtIsRealized(shell))
		return NULL;

	source_win = XtWindow(shell);

	if (dpy == NULL || source_win == None)
		return NULL;

	start_time = event->time;

	atoms = x11dnd_get_atoms();
	if (atoms == NULL)
		return NULL;

	types[0] = atoms->text_uri_list;
	types[1] = atoms->UTF8_STRING;
	types[2] = atoms->STRING;
	types[3] = atoms->FILE_NAME;

	actions[0] = atoms->XdndActionCopy;
	actions[1] = atoms->XdndActionMove;

	sess = x11dnd_start_drag(dpy, source_win, callbacks, start_time,
		types, 4, actions, 2, NULL);
	if (sess == NULL)
		return NULL;

	xt_active_source = sess;

	xt_work_proc_id = XtAppAddWorkProc(
		XtWidgetToApplicationContext(w),
		xt_drag_work_proc, (XtPointer)sess);

	xt_poll_timer_id = XtAppAddTimeOut(
		XtWidgetToApplicationContext(w),
		XT_DND_POLL_MS, xt_poll_timer_cb, (XtPointer)sess);

	XtAddEventHandler(w,
		PropertyChangeMask,
		True, /* non-maskable: ClientMessage, SelectionRequest, etc. */
		xt_event_handler,
		NULL);

	return sess;
}

void
x11dnd_xt_cancel_drag(void)
{
	if (xt_active_source == NULL)
		return;

	if (xt_work_proc_id != None) {
		XtRemoveWorkProc(xt_work_proc_id);
		xt_work_proc_id = None;
	}
	if (xt_poll_timer_id != None) {
		XtRemoveTimeOut(xt_poll_timer_id);
		xt_poll_timer_id = None;
	}

	/* Remove event handler from source widget — we use the shell as
	 * a proxy since we may not have the original widget pointer.
	 * The shell-level handler from x11dnd_xt_init() will still catch
	 * any straggling events. */

	x11dnd_cancel_drag(xt_active_source);
	xt_active_source = NULL;
}

/* ===================================================================
 * Public API: Event dispatch
 * =================================================================== */

int
x11dnd_xt_process_event(Widget w, XEvent *ev)
{
	int consumed = 0;
	Bool source_session_ended = False;

	if (ev == NULL)
		return 0;

	switch (ev->type) {
	case ClientMessage:
		consumed = x11dnd_target_process_event(ev);
		if (!consumed) {
			consumed = x11dnd_source_process_event(ev);
			/* XdndFinished frees the source session, leaving
			 * xt_active_source dangling. Detect this so we
			 * can clean up the Xt work proc and timer. */
			if (consumed && xt_active_source != NULL) {
				const X11DndAtoms *atoms = x11dnd_get_atoms();
				if (atoms != NULL
					&& ev->xclient.message_type
						== atoms->XdndFinished) {
					source_session_ended = True;
				}
			}
		}
		break;

	case SelectionNotify:
		consumed = x11dnd_target_handle_selection_notify(ev);
		break;

	case SelectionRequest:
	case SelectionClear:
		consumed = x11dnd_source_process_event(ev);
		/* SelectionClear calls x11dnd_cancel_drag which frees
		 * the source session, leaving xt_active_source dangling. */
		if (consumed && ev->type == SelectionClear
			&& xt_active_source != NULL) {
			source_session_ended = True;
		}
		break;

	case PropertyNotify:
		consumed = x11dnd_target_process_event(ev);
		if (!consumed)
			consumed = x11dnd_source_process_event(ev);
		break;

	default:
		break;
	}

	/* If the source session was freed by the event handler (XdndFinished
	 * or SelectionClear), the Xt work proc and timer now hold dangling
	 * pointers. Remove them and clear xt_active_source. */
	if (source_session_ended) {
		if (xt_work_proc_id != None) {
			XtRemoveWorkProc(xt_work_proc_id);
			xt_work_proc_id = None;
		}
		if (xt_poll_timer_id != None) {
			XtRemoveTimeOut(xt_poll_timer_id);
			xt_poll_timer_id = None;
		}
		xt_active_source = NULL;
	}

	return consumed;
}