/*
 * x11dnd_source.h - Internal header for drag source state machine.
 *
 * This is an INTERNAL header for the libx11dnd library, NOT the public API.
 * It declares the source session struct (definition of the opaque
 * X11DndSourceSession type from x11dnd.h) and internal helpers used by
 * the source state machine implementation.
 */
#ifndef X11DND_SOURCE_H
#define X11DND_SOURCE_H

#include <X11/Xlib.h>
#include "x11dnd.h"

/* Source state machine states */
typedef enum {
	X11DND_SOURCE_IDLE,           /* No drag active */
	X11DND_SOURCE_DRAGGING,       /* Drag started, no target entered yet */
	X11DND_SOURCE_ENTERED,        /* Pointer entered a target, sent XdndEnter */
	X11DND_SOURCE_WAITING_STATUS, /* Sent XdndPosition, waiting for XdndStatus */
	X11DND_SOURCE_DROP_SENT,      /* Sent XdndDrop, waiting for XdndFinished */
	X11DND_SOURCE_FINISHED        /* XdndFinished received, cleanup */
} X11DndSourceState;

/*
 * Internal source session struct (definition of opaque type from x11dnd.h).
 *
 * One session per active drag operation. The library enforces a single
 * active source at a time via the static 'active_source' pointer in
 * x11dnd_source.c.
 */
struct X11DndSourceSession {
	Display *dpy;
	Window source_win;
	X11DndSourceState state;
	X11DndClass *callbacks;
	void *user_data;

	/* Drag info */
	Time start_time;
	Atom *types;
	int n_types;
	Atom *actions;
	int n_actions;

	/* Current target */
	Window current_target;
	int target_version;

	/* Status tracking */
	Bool waiting_for_status;
	int last_sent_x, last_sent_y;
	Time last_position_time;
	Atom last_action;

	/* Drop result */
	Bool drop_completed;
	Atom performed_action;

	/* Drag icon fields */
	Window icon_win;
	Pixmap icon_pixmap;
	Pixmap icon_mask;
	int icon_width;
	int icon_height;
	int icon_hotspot_x;
	int icon_hotspot_y;
	unsigned long icon_fg;
	unsigned long icon_bg;
	unsigned int icon_flags;
	const unsigned char *icon_bits;
	const unsigned char *icon_mask_bits;
	int root_x;           /* Last known root X coordinate */
	int root_y;           /* Last known root Y coordinate */
};

/* Internal functions (not part of public API) */
int x11dnd_source_handle_status(X11DndSourceSession *sess,
	XClientMessageEvent *ev);
int x11dnd_source_handle_finished(X11DndSourceSession *sess,
	XClientMessageEvent *ev);
void x11dnd_source_send_enter(X11DndSourceSession *sess, Window target);
void x11dnd_source_send_position(X11DndSourceSession *sess, Window target,
	int x, int y, Time time, Atom action);
void x11dnd_source_send_leave(X11DndSourceSession *sess, Window target);
void x11dnd_source_send_drop(X11DndSourceSession *sess, Window target,
	Time time);

/*
 * Handle a SelectionRequest event for XdndSelection. Converts the drag
 * data to the requested target type (text/uri-list, UTF8_STRING, STRING,
 * FILE_NAME, text/plain, TARGETS, TIMESTAMP) and sends SelectionNotify
 * back to the requestor. Returns 1 if the event was consumed, 0 otherwise.
 */
int x11dnd_source_handle_selection_request(XEvent *ev);

/*
 * Handle a SelectionClear event for XdndSelection. If we lost selection
 * ownership (e.g. another client took it), the drag is cancelled and the
 * on_error callback is invoked. Returns 1 if consumed, 0 otherwise.
 */
int x11dnd_source_handle_selection_clear(XEvent *ev);

void x11dnd_source_track_motion(X11DndSourceSession *sess, int x, int y,
	Time time);

int x11dnd_source_get_root_xy(X11DndSourceSession *sess, int *x, int *y);

#endif /* X11DND_SOURCE_H */