/*
 * x11dnd_target.h - Internal header for XDnD drop target state machine.
 *
 * This is an INTERNAL header for the libx11dnd library, NOT the public API.
 * It declares the target session struct (completing the opaque type from
 * x11dnd.h) and the internal handler functions used by
 * x11dnd_target_process_event().
 *
 * The state machine implements the XDnD v5 target side:
 *
 *   IDLE  --XdndEnter-->  ENTERED  --XdndPosition-->  POSITION_RECEIVED
 *                                                              |
 *                                          XdndDrop            |
 *                                              v              v
 *                                          DROP_PENDING <-----+
 *                                              |
 *                                          (selection conversion)
 *                                              v
 *                                          CONVERTING
 *                                              |
 *                                          XdndLeave / completion
 *                                              v
 *                                            IDLE
 */
#ifndef X11DND_TARGET_H
#define X11DND_TARGET_H

#include <X11/Xlib.h>
#include "x11dnd.h"

/* Target state machine states */
typedef enum {
    X11DND_TARGET_IDLE,              /* No drag in progress */
    X11DND_TARGET_ENTERED,           /* XdndEnter received, waiting for Position */
    X11DND_TARGET_POSITION_RECEIVED, /* XdndPosition received, may get Drop */
    X11DND_TARGET_DROP_PENDING,      /* XdndDrop received, converting selection */
    X11DND_TARGET_CONVERTING         /* Selection conversion in progress */
} X11DndTargetState;

/*
 * Internal target session struct (definition of the opaque type
 * declared in x11dnd.h). One session exists per active drag over a
 * registered target window, created on XdndEnter and destroyed on
 * XdndLeave (or when the window is unregistered).
 */
struct X11DndTargetSession {
    Display *dpy;
    Window target_win;      /* our window */
    Window source_win;      /* drag source window */
    int negotiated_version; /* min(source_version, 5) */
    X11DndTargetState state;
    X11DndClass *callbacks;
    void *user_data;

    /* Type list from XdndEnter */
    Atom *types;
    int n_types;

    /* Current position info (from last XdndPosition) */
    int last_x, last_y;
    Time last_time;
    Atom last_action;

    /* Drop info */
    Time drop_time;
    Atom requested_type; /* type being converted */
};

/* Internal functions used by x11dnd_target_process_event() */
int x11dnd_target_handle_enter(X11DndTargetSession *sess,
    XClientMessageEvent *ev);
int x11dnd_target_handle_position(X11DndTargetSession *sess,
    XClientMessageEvent *ev);
int x11dnd_target_handle_drop(X11DndTargetSession *sess,
    XClientMessageEvent *ev);
int x11dnd_target_handle_leave(X11DndTargetSession *sess,
    XClientMessageEvent *ev);

X11DndTargetSession *x11dnd_find_target_session(Display *dpy, Window win);
void x11dnd_target_reset_session(X11DndTargetSession *sess);

/*
 * Request selection conversion after XdndDrop. The application should
 * call this from its drop callback to request the drag data. Selects
 * the best type from the source's type list (prefers text/uri-list,
 * then UTF8_STRING, STRING, text/plain, FILE_NAME).
 * Returns True on success, False if the session is invalid or not in
 * DROP_PENDING state. 'property' is the atom to use for the transfer
 * (typically XdndSelection or a dedicated property atom).
 */
Bool x11dnd_target_request_selection(X11DndTargetSession *sess,
    Atom property);

/*
 * Handle a SelectionNotify event for XdndSelection. Detects INCR
 * transfers and starts incremental collection if needed, or delivers
 * immediate data to the drop_received callback. Returns 1 if the
 * event was consumed, 0 otherwise.
 */
int x11dnd_target_handle_selection_notify(XEvent *ev);

/*
 * Get/set the current INCR target session. Used by the event loop
 * to track ongoing incremental transfers across PropertyNotify events.
 */
struct X11DndIncrTargetSession;
struct X11DndIncrTargetSession *x11dnd_target_get_incr_session(void);
void x11dnd_target_set_incr_session(struct X11DndIncrTargetSession *sess);

#endif /* X11DND_TARGET_H */