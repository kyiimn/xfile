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
#include <X11/extensions/shape.h>
#include <X11/keysym.h>
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

static const X11DndDragIcon *xt_drag_icon = NULL;
static unsigned int xt_poll_ms = 16;

static void xt_create_icon_window(X11DndSourceSession *sess);
static void xt_destroy_icon_window(X11DndSourceSession *sess);
static void xt_move_icon_window(X11DndSourceSession *sess);

/* Forward declaration for the Xt selection callback */
static void xt_selection_callback(Widget w, XtPointer client_data,
    Atom *selection, Atom *type, XtPointer value,
    unsigned long *length, int *format);

/* Event mask for XDnD events on the shell */
#define XT_DND_EVENT_MASK (NoEventMask)
/* Events we grab with nonmaskable=True: ClientMessage */
/* PropertyNotify for INCR transfers is also nonmaskable via XtAddEventHandler */

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

	if (ev == NULL) {
		if (continue_to_dispatch)
			*continue_to_dispatch = True;
		return;
	}

	if (x11dnd_xt_process_event(w, ev)) {
		if (continue_to_dispatch)
			*continue_to_dispatch = False;
	} else {
		if (continue_to_dispatch)
			*continue_to_dispatch = True;
	}
}

/* -------------------------------------------------------------------
 * Work procedure: poll pointer position during drag
 * ------------------------------------------------------------------- */
static Boolean
xt_drag_work_proc(XtPointer client_data)
{
	X11DndSourceSession *sess;

	sess = (X11DndSourceSession *)client_data;
	if (sess == NULL)
		return True;

	x11dnd_source_track_motion(sess, 0, 0, CurrentTime);

	return False;
}

/* -------------------------------------------------------------------
 * Timer callback: periodic drag position updates
 * ------------------------------------------------------------------- */
static void
xt_poll_timer_cb(XtPointer client_data, XtIntervalId *id)
{
	X11DndSourceSession *sess;

	(void)id;

	sess = (X11DndSourceSession *)client_data;
	if (sess == NULL)
		return;

	x11dnd_source_track_motion(sess, 0, 0, CurrentTime);

	xt_move_icon_window(sess);

	/* ESC cancel check */
	if (sess->icon_flags & X11DND_ICON_CANCEL_ESC) {
		char keys[32];
		KeyCode esc;
		XQueryKeymap(sess->dpy, keys);
		esc = XKeysymToKeycode(sess->dpy, XK_Escape);
		if (esc != 0 && (keys[esc >> 3] & (1 << (esc & 7)))) {
			x11dnd_xt_cancel_drag();
			return;
		}
	}

	xt_poll_timer_id = XtAppAddTimeOut(
		XtWidgetToApplicationContext(xt_shell),
		xt_poll_ms, xt_poll_timer_cb, client_data);
}

/* ===================================================================
 * Drag icon management
 * =================================================================== */

static void
xt_create_icon_window(X11DndSourceSession *sess)
{
	Display *dpy;
	Window root, icon_win;
	Pixmap icon_pixmap, icon_mask;
	XSetWindowAttributes attr;
	unsigned long attr_mask;
	int x, y;
	Window dummy_root, dummy_child;
	int dummy_x, dummy_y;
	unsigned int dummy_mask;

	if (sess == NULL || sess->icon_bits == NULL)
		return;

	dpy = x11dnd_source_get_display(sess);
	if (dpy == NULL)
		return;

	root = DefaultRootWindow(dpy);

	icon_pixmap = XCreatePixmapFromBitmapData(dpy, root,
		(char *)sess->icon_bits, sess->icon_width, sess->icon_height,
		sess->icon_fg, sess->icon_bg,
		DefaultDepth(dpy, DefaultScreen(dpy)));
	if (icon_pixmap == None)
		return;
	sess->icon_pixmap = icon_pixmap;

	icon_mask = None;
	if (sess->icon_mask_bits != NULL
		&& (sess->icon_flags & X11DND_ICON_SHAPE_MASK)) {
		icon_mask = XCreatePixmapFromBitmapData(dpy, root,
			(char *)sess->icon_mask_bits,
			sess->icon_width, sess->icon_height,
			1, 0, 1);
		sess->icon_mask = icon_mask;
	}

	attr.background_pixmap = icon_pixmap;
	attr.border_pixel = 0;
	attr.event_mask = 0;
	attr.override_redirect = True;
	attr_mask = CWOverrideRedirect | CWBackPixmap | CWBorderPixel
		| CWEventMask;

	icon_win = XCreateWindow(dpy, root, 0, 0,
		sess->icon_width, sess->icon_height,
		0, CopyFromParent, InputOutput,
		CopyFromParent, attr_mask, &attr);
	sess->icon_win = icon_win;

	if (icon_mask != None) {
		XShapeCombineMask(dpy, icon_win, ShapeBounding,
			0, 0, icon_mask, ShapeSet);
	}

	XMapWindow(dpy, icon_win);

	if (XQueryPointer(dpy, root, &dummy_root, &dummy_child,
		&x, &y, &dummy_x, &dummy_y, &dummy_mask)) {
		;
	} else {
		x = 0;
		y = 0;
	}

	XMoveWindow(dpy, icon_win,
		x + sess->icon_hotspot_x,
		y + sess->icon_hotspot_y);
	XRaiseWindow(dpy, icon_win);
}

static void
xt_destroy_icon_window(X11DndSourceSession *sess)
{
	Display *dpy;

	if (sess == NULL)
		return;

	dpy = x11dnd_source_get_display(sess);

	if (sess->icon_win != None && dpy != NULL) {
		XUnmapWindow(dpy, sess->icon_win);
		XDestroyWindow(dpy, sess->icon_win);
		sess->icon_win = None;
	}
	if (sess->icon_pixmap != None && dpy != NULL) {
		XFreePixmap(dpy, sess->icon_pixmap);
		sess->icon_pixmap = None;
	}
	if (sess->icon_mask != None && dpy != NULL) {
		XFreePixmap(dpy, sess->icon_mask);
		sess->icon_mask = None;
	}
}

static void
xt_move_icon_window(X11DndSourceSession *sess)
{
	Display *dpy;
	int x, y;

	if (sess == NULL || sess->icon_win == None)
		return;

	dpy = x11dnd_source_get_display(sess);
	if (dpy == NULL)
		return;

	if (x11dnd_source_get_root_xy(sess, &x, &y) == 0) {
		XMoveWindow(dpy, sess->icon_win,
			x + sess->icon_hotspot_x,
			y + sess->icon_hotspot_y);
	}
}

void
x11dnd_xt_set_drag_icon(const X11DndDragIcon *icon)
{
	xt_drag_icon = icon;
}

void
x11dnd_xt_set_poll_interval(unsigned int ms)
{
	if (ms < 1) ms = 1;
	if (ms > 1000) ms = 1000;
	xt_poll_ms = ms;
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
			if (xt_active_source->icon_win != None) {
				xt_destroy_icon_window(xt_active_source);
			}
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

	actions[0] = atoms->XdndActionMove;
	actions[1] = atoms->XdndActionCopy;

	sess = x11dnd_start_drag(dpy, source_win, callbacks, start_time,
		types, 4, actions, 2, NULL);
	if (sess == NULL)
		return NULL;

	xt_active_source = sess;

	/* Copy icon configuration into session */
	if (xt_drag_icon != NULL && xt_drag_icon->bits != NULL) {
		sess->icon_bits = xt_drag_icon->bits;
		sess->icon_mask_bits = xt_drag_icon->mask_bits;
		sess->icon_width = xt_drag_icon->width;
		sess->icon_height = xt_drag_icon->height;
		sess->icon_hotspot_x = xt_drag_icon->hotspot_x;
		sess->icon_hotspot_y = xt_drag_icon->hotspot_y;
		sess->icon_fg = xt_drag_icon->fg_pixel;
		sess->icon_bg = xt_drag_icon->bg_pixel;
		sess->icon_flags = xt_drag_icon->flags;
		sess->icon_win = None;
		sess->icon_pixmap = None;
		sess->icon_mask = None;
		xt_create_icon_window(sess);
	} else {
		sess->icon_win = None;
		sess->icon_pixmap = None;
		sess->icon_mask = None;
		sess->icon_bits = NULL;
		sess->icon_mask_bits = NULL;
		sess->icon_flags = 0;
	}

	xt_work_proc_id = XtAppAddWorkProc(
		XtWidgetToApplicationContext(w),
		xt_drag_work_proc, (XtPointer)sess);

	xt_poll_timer_id = XtAppAddTimeOut(
		XtWidgetToApplicationContext(w),
		xt_poll_ms, xt_poll_timer_cb, (XtPointer)sess);

	return sess;
}

void
x11dnd_xt_cancel_drag(void)
{
	if (xt_active_source == NULL)
		return;

	if (xt_active_source->icon_win != None) {
		xt_destroy_icon_window(xt_active_source);
	}

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

void
x11dnd_xt_stop_tracking(void)
{
	if (xt_work_proc_id != None) {
		XtRemoveWorkProc(xt_work_proc_id);
		xt_work_proc_id = None;
	}
	if (xt_poll_timer_id != None) {
		XtRemoveTimeOut(xt_poll_timer_id);
		xt_poll_timer_id = None;
	}
}

/* ===================================================================
 * Xt selection callback: receives data from XtGetSelectionValue
 *
 * Xt calls this when the selection data is available. This replaces
 * the raw SelectionNotify handler — Xt's internal selection
 * machinery delivers the converted property data directly here.
 * =================================================================== */

static void
xt_selection_callback(Widget w, XtPointer client_data, Atom *selection,
    Atom *type, XtPointer value, unsigned long *length, int *format)
{
	X11DndTargetSession *sess;
	const X11DndAtoms *atoms;

	(void)client_data;
	(void)selection;

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		if (value) XtFree((char *)value);
		return;
	}

	sess = x11dnd_find_target_session(XtDisplay(w), XtWindow(w));
	if (sess == NULL) {
		if (value) XtFree((char *)value);
		return;
	}

	if (sess->state != X11DND_TARGET_DROP_PENDING) {
		if (value) XtFree((char *)value);
		return;
	}

	if (value == NULL || *length == 0 || *type == None) {
		x11dnd_send_finished(sess->dpy, sess->source_win,
			sess->target_win, False, None);
		x11dnd_target_reset_session(sess);
		return;
	}

	if (sess->callbacks && sess->callbacks->drop_received) {
		sess->callbacks->drop_received(sess, *type,
			(unsigned char *)value, *length, *format);
	}

	x11dnd_send_finished(sess->dpy, sess->source_win,
		sess->target_win, True, sess->last_action);

	XtFree((char *)value);
	x11dnd_target_reset_session(sess);
}

/* ===================================================================
 * Public API: Event dispatch
 * =================================================================== */

int
x11dnd_xt_process_event(Widget w, XEvent *ev)
{
	int consumed = 0;
	Bool source_session_ended = False;
	const X11DndAtoms *atoms;

	if (ev == NULL)
		return 0;

	atoms = x11dnd_get_atoms();

	switch (ev->type) {
	case ClientMessage:
	{
		/* XdndFinished frees the source session via on_drag_end
		 * callback inside x11dnd_source_process_event. After that
		 * call returns, xt_active_source is a dangling pointer.
		 * Destroy the icon BEFORE dispatching so the session
		 * is still valid. */
		if (xt_active_source != NULL && atoms != NULL
			&& ev->xclient.message_type == atoms->XdndFinished) {
			if (xt_active_source->icon_win != None) {
				xt_destroy_icon_window(xt_active_source);
			}
		}

		consumed = x11dnd_target_process_event(ev);
		if (!consumed)
			consumed = x11dnd_source_process_event(ev);

		if (consumed && atoms != NULL
			&& ev->xclient.message_type == atoms->XdndDrop) {
			X11DndTargetSession *tsess;
			tsess = x11dnd_find_target_session(
				ev->xclient.display,
				ev->xclient.window);
			if (tsess != NULL
				&& tsess->state
					== X11DND_TARGET_DROP_PENDING) {
				XtGetSelectionValue(xt_shell,
					atoms->XdndSelection,
					tsess->requested_type,
					xt_selection_callback, NULL,
					tsess->drop_time);
			}
		}

		/* XdndFinished frees the source session, leaving
		 * xt_active_source dangling. Remove the Xt work proc
		 * and timer, and clear xt_active_source. */
		if (consumed && xt_active_source != NULL
			&& atoms != NULL
			&& ev->xclient.message_type
				== atoms->XdndFinished) {
			source_session_ended = True;
		}
	}
		break;

	case SelectionRequest:
	case SelectionClear:
		/* SelectionClear frees the source session via cancel_drag's
		 * on_drag_end callback. Destroy icon before dispatching. */
		if (xt_active_source != NULL
			&& ev->type == SelectionClear) {
			if (xt_active_source->icon_win != None) {
				xt_destroy_icon_window(xt_active_source);
			}
		}

		consumed = x11dnd_source_process_event(ev);
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