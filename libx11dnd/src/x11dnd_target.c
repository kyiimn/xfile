/*
 * x11dnd_target.c - XDnD v5 drop target registration and state machine.
 *
 * Implements the target (receiver) side of the XDnD protocol:
 *   - Window registration via XdndAware property
 *   - Event dispatch for XdndEnter/Position/Drop/Leave ClientMessages
 *   - Version negotiation (clamps to min(source, 5), rejects < 3)
 *   - State machine: IDLE -> ENTERED -> POSITION_RECEIVED -> DROP_PENDING
 *
 * Per XDnD v5 spec, the XdndEnter message layout is:
 *   data.l[0] = source window
 *   data.l[1] = (version << 24) | (more_than_3_types ? 1 : 0)
 *   data.l[2] = type atom 1 (or None)
 *   data.l[3] = type atom 2 (or None)
 *   data.l[4] = type atom 3 (or None)
 *
 * XdndPosition:
 *   data.l[0] = source window
 *   data.l[1] = (reserved, 0)
 *   data.l[2] = (root_x << 16) | (root_y & 0xFFFF)
 *   data.l[3] = timestamp
 *   data.l[4] = action atom
 *
 * XdndDrop:
 *   data.l[0] = source window
 *   data.l[1] = (is_synchronous ? 1 : 0)
 *   data.l[2] = timestamp
 *
 * XdndLeave:
 *   data.l[0] = source window
 */
#include "x11dnd_target.h"
#include "x11dnd_atoms.h"
#include "x11dnd_incr.h"
#include "x11dnd_util.h"

#include <X11/Xatom.h>
#include <stdlib.h>
#include <string.h>

/* X ClientMessage data.l[] fields are 32-bit values that get sign-extended
 * to 64-bit long on LP64 platforms. Mask with XCM32 to strip the upper bits
 * when extracting Window, Atom, Time, or coordinate values. */
#define XCM32(l)  ((unsigned long)(l) & 0xFFFFFFFFUL)

/* Active INCR target session for the most recent selection conversion. */
static X11DndIncrTargetSession *g_incr_target = NULL;

#define X11DND_MAX_TARGETS 64

typedef struct {
    Display *dpy;
    Window win;
    X11DndClass *callbacks;
    void *user_data;
    X11DndTargetSession *active_session;
} X11DndTargetEntry;

static X11DndTargetEntry g_targets[X11DND_MAX_TARGETS];
static int g_n_targets = 0;

static Atom select_best_type(X11DndTargetSession *sess, const X11DndAtoms *atoms);

static X11DndTargetEntry *
find_target(Display *dpy, Window win)
{
    int i;
    for (i = 0; i < g_n_targets; i++) {
        if (g_targets[i].dpy == dpy && g_targets[i].win == win) {
            return &g_targets[i];
        }
    }
    return NULL;
}

static X11DndTargetEntry *
find_target_by_event(XClientMessageEvent *ev)
{
    int i;
    for (i = 0; i < g_n_targets; i++) {
        if (g_targets[i].win == ev->window) {
            return &g_targets[i];
        }
    }
    return NULL;
}

static void
free_session_types(X11DndTargetSession *sess)
{
    if (sess->types) {
        free(sess->types);
        sess->types = NULL;
    }
    sess->n_types = 0;
}

static void
reset_session(X11DndTargetSession *sess)
{
    free_session_types(sess);
    sess->source_win = None;
    sess->negotiated_version = 0;
    sess->state = X11DND_TARGET_IDLE;
    sess->last_x = 0;
    sess->last_y = 0;
    sess->last_time = 0;
    sess->last_action = None;
    sess->drop_time = 0;
    sess->requested_type = None;
}

static X11DndTargetSession *
create_session(X11DndTargetEntry *entry)
{
    X11DndTargetSession *sess;

    if (entry->active_session) {
        reset_session(entry->active_session);
        return entry->active_session;
    }

    sess = calloc(1, sizeof(*sess));
    if (!sess) {
        return NULL;
    }
    sess->dpy = entry->dpy;
    sess->target_win = entry->win;
    sess->callbacks = entry->callbacks;
    sess->user_data = entry->user_data;
    sess->state = X11DND_TARGET_IDLE;
    entry->active_session = sess;
    return sess;
}

static void
destroy_session(X11DndTargetEntry *entry)
{
    if (entry->active_session) {
        free_session_types(entry->active_session);
        free(entry->active_session);
        entry->active_session = NULL;
    }
}

Bool
x11dnd_set_aware(Display *dpy, Window win, int version)
{
    const X11DndAtoms *atoms;
    Atom aware_atom;
    long val;

    if (dpy == NULL || win == None) {
        return False;
    }

    atoms = x11dnd_get_atoms();
    if (atoms) {
        aware_atom = atoms->XdndAware;
    } else {
        aware_atom = XInternAtom(dpy, "XdndAware", False);
        if (aware_atom == None) {
            return False;
        }
    }

    val = (long)version;
    XChangeProperty(dpy, win, aware_atom, XA_ATOM, 32,
        PropModeReplace, (unsigned char *)&val, 1);
    XFlush(dpy);
    return True;
}

int
x11dnd_get_aware_version(Display *dpy, Window win)
{
    const X11DndAtoms *atoms;
    Atom aware_atom;
    unsigned long nitems;
    unsigned char *data;
    int version;

    if (dpy == NULL || win == None) {
        return -1;
    }

    atoms = x11dnd_get_atoms();
    if (atoms) {
        aware_atom = atoms->XdndAware;
    } else {
        aware_atom = XInternAtom(dpy, "XdndAware", True);
        if (aware_atom == None) {
            return -1;
        }
    }

    data = NULL;
    if (x11dnd_get_window_property(dpy, win, aware_atom, XA_ATOM, 32,
        &nitems, &data) != 0) {
        return -1;
    }
    if (nitems < 1 || data == NULL) {
        if (data) {
            free(data);
        }
        return -1;
    }

    version = (int)((long *)data)[0];
    free(data);
    return version;
}

Bool
x11dnd_version_at_least(int negotiated, int required)
{
    return (negotiated >= required) ? True : False;
}

int
x11dnd_register_target(Display *dpy, Window win,
    X11DndClass *callbacks, void *user_data)
{
    X11DndTargetEntry *entry;

    if (dpy == NULL || win == None) {
        return 1;
    }

    if (find_target(dpy, win) != NULL) {
        return 0;
    }

    if (g_n_targets >= X11DND_MAX_TARGETS) {
        return 1;
    }

    if (!x11dnd_set_aware(dpy, win, X11DND_VERSION_5)) {
        return 1;
    }

    entry = &g_targets[g_n_targets];
    entry->dpy = dpy;
    entry->win = win;
    entry->callbacks = callbacks;
    entry->user_data = user_data;
    entry->active_session = NULL;
    g_n_targets++;
    return 0;
}

void
x11dnd_unregister_target(Display *dpy, Window win)
{
    const X11DndAtoms *atoms;
    Atom aware_atom;
    int i, j;

    if (dpy == NULL || win == None) {
        return;
    }

    for (i = 0; i < g_n_targets; i++) {
        if (g_targets[i].dpy == dpy && g_targets[i].win == win) {
            destroy_session(&g_targets[i]);
            for (j = i; j < g_n_targets - 1; j++) {
                g_targets[j] = g_targets[j + 1];
            }
            g_n_targets--;
            memset(&g_targets[g_n_targets], 0, sizeof(g_targets[0]));
            break;
        }
    }

    atoms = x11dnd_get_atoms();
    if (atoms) {
        aware_atom = atoms->XdndAware;
    } else {
        aware_atom = XInternAtom(dpy, "XdndAware", False);
    }
    if (aware_atom != None) {
        XDeleteProperty(dpy, win, aware_atom);
        XFlush(dpy);
    }
}

int
x11dnd_target_handle_enter(X11DndTargetSession *sess,
    XClientMessageEvent *ev)
{
    const X11DndAtoms *atoms;
    Window source;
    int source_version;
    int more_types;
    int i, count;
    Atom inline_types[3];

    if (sess == NULL || ev == NULL) {
        return 0;
    }

    atoms = x11dnd_get_atoms();
    if (atoms == NULL) {
        return 0;
    }

    source = (Window)XCM32(ev->data.l[0]);
    source_version = (int)((ev->data.l[1] >> 24) & 0xFF);
    more_types = (int)(ev->data.l[1] & 0x1);

    if (source_version < X11DND_VERSION_MIN) {
        return 0;
    }

    sess->source_win = source;
    sess->negotiated_version = source_version;
    if (sess->negotiated_version > X11DND_VERSION_5) {
        sess->negotiated_version = X11DND_VERSION_5;
    }

    free_session_types(sess);

    if (more_types) {
        unsigned long nitems;
        unsigned char *data;

        data = NULL;
        if (x11dnd_get_window_property(sess->dpy, source,
            atoms->XdndTypeList, XA_ATOM, 32, &nitems, &data) == 0
            && data != NULL && nitems > 0) {
            sess->types = malloc(nitems * sizeof(Atom));
            if (sess->types) {
                for (i = 0; i < (int)nitems; i++) {
                    sess->types[i] = ((Atom *)data)[i];
                }
                sess->n_types = (int)nitems;
            }
            free(data);
        }
    } else {
        inline_types[0] = (Atom)XCM32(ev->data.l[2]);
        inline_types[1] = (Atom)XCM32(ev->data.l[3]);
        inline_types[2] = (Atom)XCM32(ev->data.l[4]);

        count = 0;
        for (i = 0; i < 3; i++) {
            if (inline_types[i] != None) {
                count++;
            }
        }

        if (count > 0) {
            sess->types = malloc(count * sizeof(Atom));
            if (sess->types) {
                int idx = 0;
                for (i = 0; i < 3; i++) {
                    if (inline_types[i] != None) {
                        sess->types[idx++] = inline_types[i];
                    }
                }
                sess->n_types = count;
            }
        }
    }

    sess->state = X11DND_TARGET_ENTERED;

    if (sess->callbacks && sess->callbacks->on_enter) {
        sess->callbacks->on_enter(sess, source, sess->negotiated_version,
            sess->types, sess->n_types);
    }

    return 1;
}

int
x11dnd_target_handle_position(X11DndTargetSession *sess,
    XClientMessageEvent *ev)
{
    int x, y;
    Time timestamp;
    Atom action;
    Bool accept = False;
    Atom reply_action = None;
    int rect_x = 0, rect_y = 0, rect_w = 0, rect_h = 0;

    if (sess == NULL || ev == NULL) {
        return 0;
    }
    if (sess->state == X11DND_TARGET_IDLE) {
        return 0;
    }

    x = (int)((ev->data.l[2] >> 16) & 0xFFFF);
    y = (int)(ev->data.l[2] & 0xFFFF);
    timestamp = (Time)XCM32(ev->data.l[3]);
    action = (Atom)XCM32(ev->data.l[4]);

    sess->last_x = x;
    sess->last_y = y;
    sess->last_time = timestamp;
    sess->last_action = action;

    sess->state = X11DND_TARGET_POSITION_RECEIVED;

    if (sess->callbacks && sess->callbacks->position_received) {
        sess->callbacks->position_received(sess, x, y, timestamp, action,
            &accept, &reply_action, &rect_x, &rect_y, &rect_w, &rect_h);
    } else {
        accept = False;
        reply_action = None;
    }

    x11dnd_send_status(sess->dpy, sess->source_win, sess->target_win,
        accept, rect_x, rect_y, rect_w, rect_h, reply_action);

    return 1;
}

int
x11dnd_target_handle_drop(X11DndTargetSession *sess,
    XClientMessageEvent *ev)
{
    const X11DndAtoms *atoms;
    Time timestamp;
    Atom requested_type;

    if (sess == NULL || ev == NULL) {
        return 0;
    }

	if (sess->state != X11DND_TARGET_POSITION_RECEIVED) {
		x11dnd_send_finished(sess->dpy, sess->source_win,
			sess->target_win, False, None);
		free_session_types(sess);
		sess->state = X11DND_TARGET_IDLE;
		return 1;
	}

    atoms = x11dnd_get_atoms();
    if (atoms == NULL) {
        x11dnd_send_finished(sess->dpy, sess->source_win,
            sess->target_win, False, None);
        free_session_types(sess);
        sess->state = X11DND_TARGET_IDLE;
        return 1;
    }

    timestamp = (Time)XCM32(ev->data.l[2]);
    sess->drop_time = timestamp;

    requested_type = select_best_type(sess, atoms);
    if (requested_type == None) {
        x11dnd_send_finished(sess->dpy, sess->source_win,
            sess->target_win, False, None);
        free_session_types(sess);
        sess->state = X11DND_TARGET_IDLE;
        return 1;
    }

	sess->requested_type = requested_type;
	sess->state = X11DND_TARGET_DROP_PENDING;

	return 1;
}

int
x11dnd_target_handle_leave(X11DndTargetSession *sess,
    XClientMessageEvent *ev)
{
    if (sess == NULL || ev == NULL) {
        return 0;
    }

    /* If a drop is pending, the source sends XdndLeave after XdndDrop
     * as part of protocol teardown. We must not reset the session —
     * the selection callback has not delivered the data yet. The
     * session will be cleaned up by xt_selection_callback or
     * x11dnd_target_reset_session after the data arrives. */
    if (sess->state == X11DND_TARGET_DROP_PENDING) {
        return 1;
    }

    if (sess->callbacks && sess->callbacks->on_leave) {
        sess->callbacks->on_leave(sess);
    }

    free_session_types(sess);
    sess->source_win = None;
    sess->negotiated_version = 0;
    sess->state = X11DND_TARGET_IDLE;
    sess->last_x = 0;
    sess->last_y = 0;
    sess->last_time = 0;
    sess->last_action = None;
    sess->drop_time = 0;
    sess->requested_type = None;

    return 1;
}

int
x11dnd_target_process_event(XEvent *ev)
{
    const X11DndAtoms *atoms;
    X11DndTargetEntry *entry;
    X11DndTargetSession *sess;
    XClientMessageEvent *cm;

    if (ev == NULL) {
        return 0;
    }
    if (ev->type != ClientMessage) {
        return 0;
    }

    atoms = x11dnd_get_atoms();
    if (atoms == NULL) {
        return 0;
    }

    cm = &ev->xclient;

    if (cm->message_type != atoms->XdndEnter &&
        cm->message_type != atoms->XdndPosition &&
        cm->message_type != atoms->XdndDrop &&
        cm->message_type != atoms->XdndLeave) {
        return 0;
    }

    entry = find_target_by_event(cm);
    if (entry == NULL) {
        return 0;
    }

    sess = entry->active_session;
    if (sess == NULL && cm->message_type != atoms->XdndEnter) {
        return 0;
    }

    if (cm->message_type == atoms->XdndEnter) {
        if (sess == NULL) {
            sess = create_session(entry);
            if (sess == NULL) {
                return 0;
            }
        } else {
            reset_session(sess);
        }
        return x11dnd_target_handle_enter(sess, cm);
    }

    if (cm->message_type == atoms->XdndPosition) {
        return x11dnd_target_handle_position(sess, cm);
    }

    if (cm->message_type == atoms->XdndDrop) {
        return x11dnd_target_handle_drop(sess, cm);
    }

    if (cm->message_type == atoms->XdndLeave) {
        return x11dnd_target_handle_leave(sess, cm);
    }

    return 0;
}

void *
x11dnd_target_get_user_data(X11DndTargetSession *sess)
{
    if (sess == NULL) {
        return NULL;
    }
    return sess->user_data;
}

Window
x11dnd_target_get_window(X11DndTargetSession *sess)
{
    if (sess == NULL) {
        return None;
    }
    return sess->target_win;
}

Window
x11dnd_target_get_source_window(X11DndTargetSession *sess)
{
    if (sess == NULL) {
        return None;
    }
    return sess->source_win;
}

Display *
x11dnd_target_get_display(X11DndTargetSession *sess)
{
    if (sess == NULL) {
        return NULL;
    }
    return sess->dpy;
}

int
x11dnd_target_get_negotiated_version(X11DndTargetSession *sess)
{
    if (sess == NULL) {
        return 0;
    }
    return sess->negotiated_version;
}

Atom
x11dnd_target_get_action(X11DndTargetSession *sess)
{
    if (sess == NULL)
        return None;
    return sess->last_action;
}

static Atom
select_best_type(X11DndTargetSession *sess, const X11DndAtoms *atoms)
{
    Atom preferred_order[5];
    int n_preferred = 5;
    int i, j;

    if (sess == NULL || atoms == NULL || sess->types == NULL) {
        return None;
    }

    preferred_order[0] = atoms->text_uri_list;
    preferred_order[1] = atoms->UTF8_STRING;
    preferred_order[2] = atoms->STRING;
    preferred_order[3] = atoms->text_plain;
    preferred_order[4] = atoms->FILE_NAME;

    for (i = 0; i < n_preferred; i++) {
        for (j = 0; j < sess->n_types; j++) {
            if (sess->types[j] == preferred_order[i]) {
                return preferred_order[i];
            }
        }
    }

    return sess->types[0];
}

Bool
x11dnd_target_request_selection(X11DndTargetSession *sess, Atom property)
{
    const X11DndAtoms *atoms;
    Atom requested_type;

    if (sess == NULL || sess->dpy == NULL || sess->target_win == None) {
        return False;
    }
    if (sess->state != X11DND_TARGET_DROP_PENDING) {
        return False;
    }

    atoms = x11dnd_get_atoms();
    if (atoms == NULL) {
        return False;
    }

    requested_type = select_best_type(sess, atoms);
    if (requested_type == None) {
        return False;
    }

    sess->requested_type = requested_type;
    sess->state = X11DND_TARGET_CONVERTING;

    XConvertSelection(sess->dpy, atoms->XdndSelection, requested_type,
        property, sess->target_win, sess->drop_time);
    XFlush(sess->dpy);

    return True;
}

int
x11dnd_target_handle_selection_notify(XEvent *ev)
{
    XSelectionEvent *sel;
    X11DndTargetEntry *entry;
    X11DndTargetSession *sess;
    const X11DndAtoms *atoms;
    unsigned char *data;
    unsigned long nitems;
    int format;
    Atom actual_type;
    unsigned long bytes_after;
    int ret;

    if (ev == NULL || ev->type != SelectionNotify) {
        return 0;
    }

    sel = &ev->xselection;

    atoms = x11dnd_get_atoms();
    if (atoms == NULL) {
        return 0;
    }

    if (sel->selection != atoms->XdndSelection) {
        return 0;
    }

    /* Find the target entry for this window. */
    {
        int i;
        entry = NULL;
        for (i = 0; i < g_n_targets; i++) {
            if (g_targets[i].win == sel->requestor) {
                entry = &g_targets[i];
                break;
            }
        }
    }
    if (entry == NULL || entry->active_session == NULL) {
        return 0;
    }

    sess = entry->active_session;
    if (sess->state != X11DND_TARGET_CONVERTING) {
        return 0;
    }

	if (sel->property == None) {
		x11dnd_send_finished(sess->dpy, sess->source_win,
			sess->target_win, False, None);
		free_session_types(sess);
		sess->state = X11DND_TARGET_IDLE;
		return 1;
	}

    /* Read the property to check for INCR type. */
    data = NULL;
    ret = XGetWindowProperty(sess->dpy, sess->target_win, sel->property,
        0, 0x7FFFFFFF, False, AnyPropertyType,
        &actual_type, &format, &nitems, &bytes_after, &data);

	if (ret != Success || data == NULL) {
		x11dnd_send_finished(sess->dpy, sess->source_win,
			sess->target_win, False, None);
		free_session_types(sess);
		sess->state = X11DND_TARGET_IDLE;
		return 1;
	}

    if (actual_type == atoms->INCR) {
        /* INCR transfer: the value is a size hint. */
        unsigned long estimated_size = 0;
        if (nitems >= 1 && format == 32) {
            estimated_size = (unsigned long)((long *)data)[0];
        }
        XFree(data);

        /* Start INCR target session. */
        if (g_incr_target != NULL) {
            x11dnd_incr_target_cleanup(g_incr_target);
            g_incr_target = NULL;
        }

        g_incr_target = x11dnd_incr_target_start(sess->dpy,
            sess->target_win, sel->property, sel->selection,
            sess->requested_type, format, estimated_size);

        if (g_incr_target == NULL) {
            sess->state = X11DND_TARGET_DROP_PENDING;
            return 1;
        }

        /* Delete the INCR property to start the incremental transfer. */
        XDeleteProperty(sess->dpy, sess->target_win, sel->property);
        XFlush(sess->dpy);

        return 1;
    }

    XDeleteProperty(sess->dpy, sess->target_win, sel->property);
    XFlush(sess->dpy);

    {
        size_t unit;
        unsigned long data_len;
        unsigned char *data_copy;

        if (format == 32) {
            unit = sizeof(long);
        } else {
            unit = (size_t)format / 8;
        }
        data_len = nitems * unit;
        data_copy = malloc(data_len);
        if (data_copy != NULL) {
            memcpy(data_copy, data, data_len);
        }
        XFree(data);

        if (data_copy != NULL && sess->callbacks != NULL &&
            sess->callbacks->drop_received != NULL) {
            sess->callbacks->drop_received(sess, sel->target,
                data_copy, data_len, format);
        }
        free(data_copy);
    }

    free_session_types(sess);
    sess->source_win = None;
    sess->negotiated_version = 0;
    sess->state = X11DND_TARGET_IDLE;
    sess->drop_time = 0;
    sess->requested_type = None;
    return 1;
}

X11DndIncrTargetSession *
x11dnd_target_get_incr_session(void)
{
    return g_incr_target;
}

void
x11dnd_target_set_incr_session(X11DndIncrTargetSession *sess)
{
    g_incr_target = sess;
}

X11DndTargetSession *
x11dnd_find_target_session(Display *dpy, Window win)
{
    X11DndTargetEntry *entry = find_target(dpy, win);
    if (entry == NULL || entry->active_session == NULL) {
        return NULL;
    }
    return entry->active_session;
}

void
x11dnd_target_reset_session(X11DndTargetSession *sess)
{
    if (sess == NULL) {
        return;
    }
    free_session_types(sess);
    sess->source_win = None;
    sess->negotiated_version = 0;
    sess->state = X11DND_TARGET_IDLE;
    sess->drop_time = 0;
    sess->requested_type = None;
}