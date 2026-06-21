/*
 * Copyright (C) 2025-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * XDnD source-side drag support for the file list widget.
 * This runs in parallel with the existing Motif DnD implementation.
 */

#include <X11/Intrinsic.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <Xm/XmP.h>
#include <Xm/DragDrop.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "xdnd.h"
#include "dnd.h"
#include "mbstr.h"

#define XDND_DEBUG 0
#if XDND_DEBUG
#define xdnd_dbg(fmt,...) fprintf(stderr, "[XDnD] " fmt, ##__VA_ARGS__)
#else
#define xdnd_dbg(fmt,...) ((void)0)
#endif

#define XDND_PROTOCOL_VERSION 5
#define XDND_NUM_TYPES 4
#define XDND_POLL_MS 50
#define XDND_POSITION_MS 100
#define XDND_SESSION_TIMEOUT_MS 10000

/* XDnD atoms */
static Atom XA_XDND_AWARE = None;
static Atom XA_XDND_ENTER = None;
static Atom XA_XDND_POSITION = None;
static Atom XA_XDND_STATUS = None;
static Atom XA_XDND_LEAVE = None;
static Atom XA_XDND_DROP = None;
static Atom XA_XDND_FINISHED = None;
static Atom XA_XDND_SELECTION = None;
static Atom XA_XDND_TYPELIST = None;
static Atom XA_XDND_ACTIONLIST = None;
static Atom XA_XDND_ACTIONDESCRIPTION = None;

/* XDnD action atoms */
static Atom XA_XDND_ACTION_COPY = None;
static Atom XA_XDND_ACTION_MOVE = None;
static Atom XA_XDND_ACTION_LINK = None;
static Atom XA_XDND_ACTION_ASK = None;
static Atom XA_XDND_ACTION_PRIVATE = None;

struct xdnd_session {
	Widget source_widget;
	Window source_window;
	Display *display;
	Time start_time;

	/* Copied path data, independent of Motif drag context lifetime */
	unsigned int num_items;
	char **paths;
	char *dir_path;
	unsigned char operation;

	/* XDnD state */
	Window last_target;
	int target_version;
	Atom supported_types[XDND_NUM_TYPES];
	int num_types;
	Bool got_status;
	Atom status_action;
	Bool drop_sent;
	Bool pointer_grabbed;
	Bool left_sent;

	/* Last reported pointer position */
	int last_root_x;
	int last_root_y;
	Time last_position_time;

	/* Xt machinery */
	XtWorkProcId track_proc;
	XtIntervalId timeout_timer;
	XtIntervalId poll_timer;
};

static Display *xdnd_display = NULL;
static struct xdnd_session *session = NULL;

static void xdnd_free_session(struct xdnd_session *s);
static Boolean xdnd_track_drag(XtPointer client_data);
static void xdnd_poll_timeout_cb(XtPointer client_data, XtIntervalId *id);
static void xdnd_timeout_cb(XtPointer client_data, XtIntervalId *id);
static void xdnd_send_client_message(Window target, Atom message_type,
	unsigned long *data);
static void xdnd_send_enter(struct xdnd_session *s, Window target,
	int version);
static void xdnd_send_position(struct xdnd_session *s, Window target,
	int root_x, int root_y);
static void xdnd_send_leave(struct xdnd_session *s, Window target);
static void xdnd_send_drop(struct xdnd_session *s, Window target);
static int xdnd_get_target_version(Window target);
static void xdnd_lose_selection(Widget w, Atom *selection);

void
xdnd_init(Display *dpy)
{
	xdnd_display = dpy;

	if(dpy == NULL) return;

	XA_XDND_AWARE = XInternAtom(dpy, "_XdndAware", False);
	XA_XDND_ENTER = XInternAtom(dpy, "_XdndEnter", False);
	XA_XDND_POSITION = XInternAtom(dpy, "_XdndPosition", False);
	XA_XDND_STATUS = XInternAtom(dpy, "_XdndStatus", False);
	XA_XDND_LEAVE = XInternAtom(dpy, "_XdndLeave", False);
	XA_XDND_DROP = XInternAtom(dpy, "_XdndDrop", False);
	XA_XDND_FINISHED = XInternAtom(dpy, "_XdndFinished", False);
	XA_XDND_SELECTION = XInternAtom(dpy, "_XdndSelection", False);
	XA_XDND_TYPELIST = XInternAtom(dpy, "_XdndTypeList", False);
	XA_XDND_ACTIONLIST = XInternAtom(dpy, "_XdndActionList", False);
	XA_XDND_ACTIONDESCRIPTION = XInternAtom(dpy,
		"_XdndActionDescription", False);

	XA_XDND_ACTION_COPY = XInternAtom(dpy, "XdndActionCopy", False);
	XA_XDND_ACTION_MOVE = XInternAtom(dpy, "XdndActionMove", False);
	XA_XDND_ACTION_LINK = XInternAtom(dpy, "XdndActionLink", False);
	XA_XDND_ACTION_ASK = XInternAtom(dpy, "XdndActionAsk", False);
	XA_XDND_ACTION_PRIVATE = XInternAtom(dpy, "XdndActionPrivate", False);
}

void
xdnd_destroy(void)
{
	if(session != NULL) {
		xdnd_end_drag();
	}

	xdnd_display = NULL;

	XA_XDND_AWARE = None;
	XA_XDND_ENTER = None;
	XA_XDND_POSITION = None;
	XA_XDND_STATUS = None;
	XA_XDND_LEAVE = None;
	XA_XDND_DROP = None;
	XA_XDND_FINISHED = None;
	XA_XDND_SELECTION = None;
	XA_XDND_TYPELIST = None;
	XA_XDND_ACTIONLIST = None;
	XA_XDND_ACTIONDESCRIPTION = None;
	XA_XDND_ACTION_COPY = None;
	XA_XDND_ACTION_MOVE = None;
	XA_XDND_ACTION_LINK = None;
	XA_XDND_ACTION_ASK = None;
	XA_XDND_ACTION_PRIVATE = None;
}

static void
xdnd_copy_paths(struct xdnd_session *s, struct dnd_drag_context *ctx)
{
	unsigned int i;

	s->num_items = ctx->num_items;
	s->operation = ctx->operation;

	if(ctx->dir_path != NULL) {
		s->dir_path = XtMalloc(strlen(ctx->dir_path) + 1);
		if(s->dir_path != NULL) strcpy(s->dir_path, ctx->dir_path);
	}

	if(ctx->num_items == 0) return;

	s->paths = (char**)XtMalloc(sizeof(char*) * ctx->num_items);
	if(!s->paths) {
		s->num_items = 0;
		return;
	}

	for(i = 0; i < ctx->num_items; i++) {
		s->paths[i] = NULL;
	}

	for(i = 0; i < ctx->num_items; i++) {
		if(ctx->paths[i] != NULL) {
			s->paths[i] = XtMalloc(strlen(ctx->paths[i]) + 1);
			if(s->paths[i] != NULL)
				strcpy(s->paths[i], ctx->paths[i]);
		}
	}
}

static Atom
xdnd_default_action(struct xdnd_session *s)
{
	if(s->operation == XmDROP_MOVE) return XA_XDND_ACTION_MOVE;
	return XA_XDND_ACTION_COPY;
}

void
xdnd_start_drag(Widget source, XEvent *event, XtPointer drag_context)
{
	struct dnd_drag_context *ctx;
	struct xdnd_session *s;
	Display *dpy;
	Window source_window;
	unsigned long aware;
	int grab_status;
	Time start_time;

	if(source == NULL || drag_context == NULL) return;
	if(!XtIsRealized(source)) return;
	if(xdnd_display == NULL) return;

	/* Prevent re-entry if a session is already active */
	if(session != NULL) {
		xdnd_dbg("start_drag: session already active, skipping\n");
		return;
	}

	dpy = xdnd_display;
	ctx = (struct dnd_drag_context*)drag_context;
	source_window = XtWindow(source);

	s = (struct xdnd_session*)XtMalloc(sizeof(struct xdnd_session));
	if(!s) return;
	memset(s, 0, sizeof(struct xdnd_session));

	xdnd_dbg("start_drag: source_window=0x%lx, num_items=%u, operation=%u\n",
		(unsigned long)source_window, ctx->num_items, (unsigned)ctx->operation);

	s->source_widget = source;
	s->source_window = source_window;
	s->display = dpy;
	s->target_version = 0;
	s->status_action = None;

	if(event != NULL) {
		if(event->type == ButtonPress || event->type == ButtonRelease ||
			event->type == MotionNotify) {
			start_time = event->xbutton.time;
		} else if(event->type == KeyPress || event->type == KeyRelease) {
			start_time = event->xkey.time;
		} else {
			start_time = CurrentTime;
		}
	} else {
		start_time = CurrentTime;
	}
	s->start_time = start_time;

	xdnd_copy_paths(s, ctx);

	s->supported_types[0] = XA_TEXT_URI_LIST;
	s->supported_types[1] = XA_UTF8_STRING;
	s->supported_types[2] = XA_STRING;
	s->supported_types[3] = XA_FILE_NAME;
	s->num_types = XDND_NUM_TYPES;

	aware = XDND_PROTOCOL_VERSION;
	XChangeProperty(dpy, source_window, XA_XDND_AWARE, XA_CARDINAL, 32,
		PropModeReplace, (unsigned char*)&aware, 1);

	if(s->num_types > 3) {
		XChangeProperty(dpy, source_window, XA_XDND_TYPELIST, XA_ATOM, 32,
			PropModeReplace, (unsigned char*)s->supported_types,
			s->num_types);
	}

	grab_status = XGrabPointer(dpy, source_window, False,
		ButtonMotionMask | ButtonReleaseMask | PointerMotionMask,
		GrabModeAsync, GrabModeAsync, None, None, start_time);
	s->pointer_grabbed = (grab_status == GrabSuccess) ? True : False;
	xdnd_dbg("start_drag: XGrabPointer status=%d (%s)\n",
		grab_status, grab_status == GrabSuccess ? "Success" :
		grab_status == AlreadyGrabbed ? "AlreadyGrabbed" :
		grab_status == GrabInvalidTime ? "InvalidTime" :
		grab_status == GrabNotViewable ? "NotViewable" :
		grab_status == GrabFrozen ? "Frozen" : "Unknown");

	s->track_proc = XtAppAddWorkProc(
		XtWidgetToApplicationContext(source),
		xdnd_track_drag, (XtPointer)s);
	s->poll_timer = XtAppAddTimeOut(
		XtWidgetToApplicationContext(source),
		XDND_POLL_MS, xdnd_poll_timeout_cb, (XtPointer)s);
	s->timeout_timer = XtAppAddTimeOut(
		XtWidgetToApplicationContext(source),
		XDND_SESSION_TIMEOUT_MS, xdnd_timeout_cb, (XtPointer)s);

	xdnd_dbg("start_drag: session initialized, track_proc=%p\n", (void*)s->track_proc);

	session = s;
}

static void
xdnd_free_session(struct xdnd_session *s)
{
	unsigned int i;

	if(s == NULL) return;

	if(s->paths != NULL) {
		for(i = 0; i < s->num_items; i++) {
			if(s->paths[i] != NULL) XtFree(s->paths[i]);
		}
		XtFree((char*)s->paths);
	}

	if(s->dir_path != NULL) XtFree(s->dir_path);

	XtFree((char*)s);
}

static int
xdnd_get_target_version(Window target)
{
	Atom type;
	int fmt;
	unsigned long nitems;
	unsigned long bytes;
	unsigned char *prop;
	int version;

	if(target == None) return 0;

	version = 0;
	prop = NULL;

	if(XGetWindowProperty(xdnd_display, target, XA_XDND_AWARE, 0, 1, False,
		XA_CARDINAL, &type, &fmt, &nitems, &bytes, &prop) == Success) {
		if(type == XA_CARDINAL && fmt == 32 && nitems >= 1 && prop) {
			version = (int)((unsigned long*)prop)[0];
		}
		if(prop) XFree(prop);
	}

	return version;
}

static void
xdnd_send_client_message(Window target, Atom message_type,
	unsigned long *data)
{
	XClientMessageEvent e;
	Display *dpy;
	int i;

	if(target == None || xdnd_display == NULL) return;

	dpy = xdnd_display;
	e.type = ClientMessage;
	e.send_event = True;
	e.display = dpy;
	e.window = target;
	e.message_type = message_type;
	e.format = 32;
	for(i = 0; i < 5; i++) e.data.l[i] = data[i];

	XSendEvent(dpy, target, False, NoEventMask, (XEvent*)&e);
	XFlush(dpy);
}

static void
xdnd_send_enter(struct xdnd_session *s, Window target, int version)
{
	unsigned long data[5];

	if(target == None || s == NULL) return;

	s->target_version = version;
	data[0] = s->source_window;
	data[1] = ((unsigned long)XDND_PROTOCOL_VERSION << 24) |
		(s->num_types > 3 ? 1 : 0);
	data[2] = s->supported_types[0];
	data[3] = s->supported_types[1];
	data[4] = s->supported_types[2];

	xdnd_send_client_message(target, XA_XDND_ENTER, data);
}

static void
xdnd_send_position(struct xdnd_session *s, Window target,
	int root_x, int root_y)
{
	unsigned long data[5];

	if(target == None || s == NULL) return;

	data[0] = s->source_window;
	data[1] = 0;
	data[2] = (((unsigned long)(root_x & 0xFFFF)) << 16) |
		((unsigned long)(root_y & 0xFFFF));
	data[3] = s->start_time;
	data[4] = xdnd_default_action(s);

	xdnd_send_client_message(target, XA_XDND_POSITION, data);
}

static void
xdnd_send_leave(struct xdnd_session *s, Window target)
{
	unsigned long data[5];

	if(target == None || s == NULL) return;

	data[0] = s->source_window;
	data[1] = 0;
	data[2] = 0;
	data[3] = 0;
	data[4] = 0;

	xdnd_send_client_message(target, XA_XDND_LEAVE, data);
}

static void
xdnd_send_drop(struct xdnd_session *s, Window target)
{
	unsigned long data[5];

	if(target == None || s == NULL) return;

	data[0] = s->source_window;
	data[1] = 0;
	data[2] = s->start_time;
	data[3] = 0;
	data[4] = 0;

	xdnd_send_client_message(target, XA_XDND_DROP, data);
}

static void
xdnd_handle_status(struct xdnd_session *s, XClientMessageEvent *e)
{
	if(s == NULL || e == NULL) return;

	s->got_status = True;
	s->status_action = e->data.l[4];
	xdnd_dbg("handle_status: action=%lu, accept_bit=%d\n",
		(unsigned long)s->status_action, (int)(e->data.l[1] & 0x1));
}

static void
xdnd_handle_finished(struct xdnd_session *s)
{
	if(s == NULL) return;

	xdnd_dbg("handle_finished: received XdndFinished\n");

	s->drop_sent = True;

	XtDisownSelection(s->source_widget, XA_XDND_SELECTION, CurrentTime);

	/* Tear down the session after a brief delay so the target
	 * can finish reading the selection data. */
	if(s->timeout_timer) {
		XtRemoveTimeOut(s->timeout_timer);
		s->timeout_timer = 0;
	}
	s->timeout_timer = XtAppAddTimeOut(
		XtWidgetToApplicationContext(s->source_widget),
		500, xdnd_timeout_cb, (XtPointer)s);
}

static Window
xdnd_find_target(Display *dpy, Window root, int root_x, int root_y)
{
	Window target;
	Window child;
	int x, y;
	unsigned int mask;

	target = root;
	child = root;
	x = root_x;
	y = root_y;

	while(child != None) {
		Window new_child;

		if(!XQueryPointer(dpy, child, &target, &new_child,
			&x, &y, &x, &y, &mask)) {
			return None;
		}
		child = new_child;
	}

	return target;
}

static void
xdnd_poll_timeout_cb(XtPointer client_data, XtIntervalId *id)
{
	struct xdnd_session *s = (struct xdnd_session*)client_data;

	(void)id;

	if(s == NULL) return;
	if(s != session) return;

	s->poll_timer = 0;
	xdnd_track_drag((XtPointer)s);

	if(s == session) {
		s->poll_timer = XtAppAddTimeOut(
			XtWidgetToApplicationContext(s->source_widget),
			XDND_POLL_MS, xdnd_poll_timeout_cb, (XtPointer)s);
	}
}

static void
xdnd_timeout_cb(XtPointer client_data, XtIntervalId *id)
{
	struct xdnd_session *s = (struct xdnd_session*)client_data;

	(void)id;

	if(s == NULL) return;
	if(s != session) return;

	s->timeout_timer = 0;
	xdnd_end_drag();
}

static void
xdnd_process_client_messages(struct xdnd_session *s)
{
	XEvent ev;
	Display *dpy;

	if(s == NULL) return;
	dpy = s->display;

	while(XCheckTypedWindowEvent(dpy, s->source_window,
		ClientMessage, &ev)) {
		XClientMessageEvent *cm = (XClientMessageEvent*)&ev;

		if(cm->message_type == XA_XDND_STATUS) {
			xdnd_handle_status(s, cm);
		} else if(cm->message_type == XA_XDND_FINISHED) {
			xdnd_handle_finished(s);
		} else {
			XPutBackEvent(dpy, &ev);
			break;
		}
	}
}

static Boolean
xdnd_track_drag(XtPointer client_data)
{
	struct xdnd_session *s = (struct xdnd_session*)client_data;
	Display *dpy;
	Window root;
	Window child;
	int root_x, root_y, win_x, win_y;
	unsigned int mask;
	Window target;
	int version;
	int moved;
	Time now;

	if(s == NULL || s != session) return True;

	dpy = s->display;
	root = XDefaultRootWindow(dpy);

	xdnd_process_client_messages(s);

	if(!XQueryPointer(dpy, root, &root, &child,
		&root_x, &root_y, &win_x, &win_y, &mask)) {
		return False;
	}

	target = xdnd_find_target(dpy, root, root_x, root_y);

	if(!(mask & (Button1Mask | Button2Mask | Button3Mask))) {
		xdnd_dbg("track_drag: button released, last_target=0x%lx, got_status=%d, status_action=%lu, drop_sent=%d\n",
			(unsigned long)s->last_target, s->got_status,
			(unsigned long)s->status_action, s->drop_sent);
		if(s->last_target != None && s->got_status &&
			s->status_action != None &&
			s->status_action != XA_XDND_ACTION_PRIVATE) {
			if(!s->drop_sent) {
				s->drop_sent = True;
				xdnd_dbg("track_drag: sending drop to target 0x%lx\n",
					(unsigned long)s->last_target);
				xdnd_send_drop(s, s->last_target);
				xdnd_dbg("track_drag: owning selection for drop\n");
				XtOwnSelection(s->source_widget, XA_XDND_SELECTION,
					s->start_time, xdnd_convert_selection,
					xdnd_lose_selection, NULL);
			}
		} else {
			if(s->last_target != None && !s->left_sent) {
				xdnd_send_leave(s, s->last_target);
				s->left_sent = True;
			}
		}
		/* Button released — remove the work proc.  The poll timer
		 * and timeout will continue processing XdndStatus/XdndFinished
		 * messages and eventually call xdnd_end_drag(). */
		s->track_proc = 0;
		return True;
	}

	if(target == s->source_window || target == None) {
		if(s->last_target != None && !s->left_sent) {
			xdnd_send_leave(s, s->last_target);
			s->left_sent = True;
		}
		s->last_target = None;
		s->got_status = False;
		s->status_action = None;
		return False;
	}

	moved = (root_x != s->last_root_x) || (root_y != s->last_root_y);
	s->last_root_x = root_x;
	s->last_root_y = root_y;

	if(target != s->last_target) {
		if(s->last_target != None && !s->left_sent) {
			xdnd_dbg("track_drag: sending leave to old target 0x%lx\n",
				(unsigned long)s->last_target);
			xdnd_send_leave(s, s->last_target);
		}
		s->last_target = None;
		s->got_status = False;
		s->status_action = None;
		s->left_sent = False;

		version = xdnd_get_target_version(target);
		xdnd_dbg("track_drag: new target=0x%lx, version=%d\n",
			(unsigned long)target, version);
		if(version >= 3) {
			xdnd_send_enter(s, target, version);
			s->last_target = target;
			now = XtLastTimestampProcessed(dpy);
			if(now == 0) now = s->start_time;
			s->last_position_time = now;
			xdnd_send_position(s, target, root_x, root_y);
		}
	} else if(target == s->last_target) {
		now = XtLastTimestampProcessed(dpy);
		if(now == 0) now = s->start_time;

		if(moved || (now - s->last_position_time) >= XDND_POSITION_MS) {
			xdnd_send_position(s, target, root_x, root_y);
			s->last_position_time = now;
		}
	}

	return False;
}

void
xdnd_end_drag(void)
{
	struct xdnd_session *s;

	if(session == NULL) return;

	s = session;
	xdnd_dbg("end_drag: cleaning up session, source_window=0x%lx\n",
		(unsigned long)s->source_window);
	session = NULL;

	if(s->track_proc != 0) {
		XtRemoveWorkProc(s->track_proc);
		s->track_proc = 0;
	}
	if(s->poll_timer != 0) {
		XtRemoveTimeOut(s->poll_timer);
		s->poll_timer = 0;
	}
	if(s->timeout_timer != 0) {
		XtRemoveTimeOut(s->timeout_timer);
		s->timeout_timer = 0;
	}

	if(s->pointer_grabbed) {
		XUngrabPointer(s->display, CurrentTime);
		s->pointer_grabbed = False;
	}

	if(s->source_window != None) {
		if(s->last_target != None && !s->left_sent && !s->drop_sent) {
			xdnd_send_leave(s, s->last_target);
		}
	}

	/* Do NOT call XtDisownSelection here. Motif DnD still owns the
	 * selection at this point and may need it for data transfer.
	 * XtDisownSelection during an active Motif drag causes BadAtom
	 * errors when Motif tries XConvertSelection with _XT_SELECTION_0.
	 * The selection will be released when Motif calls dragFinishCallback
	 * and the drag context is destroyed (5-second cleanup). */
	if(s->drop_sent && s->source_widget != NULL) {
		XtDisownSelection(s->source_widget, XA_XDND_SELECTION,
			s->start_time);
	}

	xdnd_free_session(s);
}

static void
xdnd_lose_selection(Widget w, Atom *selection)
{
	(void)w;
	(void)selection;
}

Boolean
xdnd_convert_selection(Widget w, Atom *selection, Atom *target,
	Atom *type_ret, XtPointer *value_ret, unsigned long *length_ret,
	int *format_ret)
{
	struct xdnd_session *s;
	unsigned int i;
	char *data;
	size_t len;

	(void)w;
	(void)selection;

	s = session;

	xdnd_dbg("convert_selection: target=%lu, session=%p\n",
		(unsigned long)*target, (void*)s);
	if(s == NULL || target == NULL || type_ret == NULL ||
		value_ret == NULL || length_ret == NULL || format_ret == NULL) {
		return False;
	}

	if(*target == XA_TARGETS) {
		uint32_t *buf32;
		unsigned int n;

		n = XDND_NUM_TYPES + 2;
		buf32 = (uint32_t*)XtMalloc(n * sizeof(uint32_t));
		if(!buf32) return False;

		for(i = 0; i < XDND_NUM_TYPES; i++)
			buf32[i] = (uint32_t)s->supported_types[i];
		buf32[XDND_NUM_TYPES] = (uint32_t)XA_TARGETS;
		buf32[XDND_NUM_TYPES + 1] = (uint32_t)XA_TIMESTAMP;

		*type_ret = XA_ATOM;
		*format_ret = 32;
		*length_ret = (unsigned long)n;
		*value_ret = (XtPointer)buf32;
		return True;

	} else if(*target == XA_TIMESTAMP) {
		uint32_t *ts32;

		ts32 = (uint32_t*)XtMalloc(sizeof(uint32_t));
		if(!ts32) return False;

		*ts32 = (uint32_t)s->start_time;
		*type_ret = XA_INTEGER;
		*format_ret = 32;
		*length_ret = 1;
		*value_ret = (XtPointer)ts32;
		return True;

	} else if(*target == XA_TEXT_URI_LIST) {
		len = 0;
		for(i = 0; i < s->num_items; i++) {
			if(s->paths[i] == NULL) continue;
			len += strlen("file://") + strlen(s->paths[i]) + 2;
		}
		if(len == 0) return False;

		data = XtMalloc(len + 1);
		if(!data) return False;
		data[0] = '\0';

		for(i = 0; i < s->num_items; i++) {
			if(s->paths[i] == NULL) continue;
			if(strlen(data) > 0) strcat(data, "\r\n");
			strcat(data, "file://");
			strcat(data, s->paths[i]);
		}

		*type_ret = XA_TEXT_URI_LIST;
		*format_ret = 8;
		*length_ret = strlen(data);
		*value_ret = data;
		return True;

	} else if(*target == XA_STRING || *target == XA_UTF8_STRING) {
		len = 0;
		for(i = 0; i < s->num_items; i++) {
			if(s->paths[i] == NULL) continue;
			len += strlen(s->paths[i]) + 1;
		}
		if(len == 0) return False;

		data = XtMalloc(len + 1);
		if(!data) return False;
		data[0] = '\0';

		for(i = 0; i < s->num_items; i++) {
			if(s->paths[i] == NULL) continue;
			if(strlen(data) > 0) strcat(data, " ");
			strcat(data, s->paths[i]);
		}

		if(*target == XA_STRING)
			mbs_to_latin1(data, data);

		*type_ret = *target;
		*format_ret = 8;
		*length_ret = strlen(data);
		*value_ret = data;
		return True;

	} else if(*target == XA_FILE_NAME) {
		if(s->num_items == 0 || s->paths[0] == NULL) {
			return False;
		}

		data = XtMalloc(strlen(s->paths[0]) + 1);
		if(!data) return False;
		strcpy(data, s->paths[0]);

		*type_ret = XA_FILE_NAME;
		*format_ret = 8;
		*length_ret = strlen(data);
		*value_ret = data;
		return True;
	}

	return False;
}
