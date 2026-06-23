/*
 * x11dnd_incr.c - INCR (incremental) selection transfer implementation.
 *
 * Implements the ICCCM Section 2.5 INCR protocol for both source and
 * target sides. Large selections are transferred in chunks to avoid
 * exceeding the X server's maximum property size.
 *
 * Source side: responds to SelectionRequest with INCR type + size hint,
 * then sends chunks on PropertyNotify(Deleted) from requestor.
 *
 * Target side: detects INCR type in SelectionNotify, collects chunks
 * via PropertyNotify(NewValue), assembles complete data.
 */
#include "x11dnd_incr.h"
#include "x11dnd_atoms.h"
#include "x11dnd_util.h"
#include "x11dnd_target.h"

#include <X11/Xatom.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Source-side INCR transfer
 * ------------------------------------------------------------------ */

/* Maximum bytes per increment chunk (X server property size limit). */
#define INCR_CHUNK_SIZE 65536

/* Linked list of active source-side INCR sessions. */
static X11DndIncrSourceSession *incr_source_list = NULL;

Bool
x11dnd_incr_should_use_incr(unsigned long data_length)
{
	return (data_length > (unsigned long)X11DND_INCR_THRESHOLD) ? True : False;
}

X11DndIncrSourceSession *
x11dnd_incr_source_start(Display *dpy, Window requestor, Atom property,
	Atom target, Atom selection, Time time, int format,
	unsigned char *data, unsigned long length)
{
	X11DndIncrSourceSession *sess;
	const X11DndAtoms *atoms;
	long incr_size;

	if (dpy == NULL || requestor == None || property == None ||
		data == NULL || length == 0) {
		return NULL;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return NULL;
	}

	sess = calloc(1, sizeof(*sess));
	if (sess == NULL) {
		return NULL;
	}

	sess->dpy = dpy;
	sess->requestor = requestor;
	sess->property = property;
	sess->target = target;
	sess->selection = selection;
	sess->time = time;
	sess->format = format;
	sess->data = data;
	sess->length = length;
	sess->offset = 0;
	sess->started = False;
	sess->complete = False;

	/* Write INCR type property with size hint (lower bound estimate). */
	incr_size = (long)length;
	XChangeProperty(dpy, requestor, property, atoms->INCR, 32,
		PropModeReplace, (unsigned char *)&incr_size, 1);

	/* Send SelectionNotify with type INCR. */
	{
		XSelectionEvent notify;
		memset(&notify, 0, sizeof(notify));
		notify.type = SelectionNotify;
		notify.requestor = requestor;
		notify.selection = selection;
		notify.target = atoms->INCR;
		notify.property = property;
		notify.time = time;
		XSendEvent(dpy, requestor, False, NoEventMask,
			(XEvent *)&notify);
	}
	XFlush(dpy);

	/* Add to linked list. */
	sess->next = incr_source_list;
	incr_source_list = sess;

	return sess;
}

int
x11dnd_incr_source_handle_property_notify(X11DndIncrSourceSession **sess_ptr,
	XPropertyEvent *pev)
{
	X11DndIncrSourceSession *sess;
	X11DndIncrSourceSession **prev;
	unsigned long chunk_size;
	unsigned char *chunk_data;

	if (sess_ptr == NULL || *sess_ptr == NULL || pev == NULL) {
		return 0;
	}

	sess = *sess_ptr;

	/* Only handle PropertyNotify(Deleted) on the requestor's property. */
	if (pev->type != PropertyNotify ||
		pev->state != PropertyDelete ||
		pev->window != sess->requestor ||
		pev->atom != sess->property) {
		return 0;
	}

	if (sess->complete) {
		return 0;
	}

	sess->started = True;

	if (sess->offset < sess->length) {
		/* Send next chunk. */
		chunk_size = sess->length - sess->offset;
		if (chunk_size > INCR_CHUNK_SIZE) {
			chunk_size = INCR_CHUNK_SIZE;
		}
		chunk_data = sess->data + sess->offset;

		XChangeProperty(sess->dpy, sess->requestor, sess->property,
			sess->target, sess->format, PropModeReplace,
			chunk_data, (int)chunk_size);
		XFlush(sess->dpy);

		sess->offset += chunk_size;
	} else {
		/* Send 0-length chunk to signal completion. */
		XChangeProperty(sess->dpy, sess->requestor, sess->property,
			sess->target, sess->format, PropModeReplace,
			NULL, 0);
		XFlush(sess->dpy);

		sess->complete = True;
	}

	/* If complete, remove from list and free session. */
	if (sess->complete) {
		/* Remove from linked list. */
		prev = &incr_source_list;
		while (*prev != NULL) {
			if (*prev == sess) {
				*prev = sess->next;
				break;
			}
			prev = &(*prev)->next;
		}

		/* Note: sess->data is NOT freed here — the caller owns it
		 * (it was passed via get_drag_data callback which uses XFree). */
		free(sess);
		*sess_ptr = NULL;
	}

	return 1;
}

void
x11dnd_incr_source_cleanup(X11DndIncrSourceSession *sess)
{
	X11DndIncrSourceSession **prev;

	if (sess == NULL) {
		return;
	}

	/* Remove from linked list. */
	prev = &incr_source_list;
	while (*prev != NULL) {
		if (*prev == sess) {
			*prev = sess->next;
			break;
		}
		prev = &(*prev)->next;
	}

	free(sess);
}

/* ------------------------------------------------------------------ *
 * Target-side INCR transfer
 * ------------------------------------------------------------------ */

/* Linked list of active target-side INCR sessions. */
static X11DndIncrTargetSession *incr_target_list = NULL;

X11DndIncrTargetSession *
x11dnd_incr_target_start(Display *dpy, Window window, Atom property,
	Atom selection, Atom target_type, int format,
	unsigned long estimated_size)
{
	X11DndIncrTargetSession *sess;
	XWindowAttributes attrs;

	if (dpy == NULL || window == None || property == None) {
		return NULL;
	}

	sess = calloc(1, sizeof(*sess));
	if (sess == NULL) {
		return NULL;
	}

	sess->dpy = dpy;
	sess->window = window;
	sess->property = property;
	sess->selection = selection;
	sess->target_type = target_type;
	sess->format = format;
	sess->estimated_size = estimated_size;
	sess->data = NULL;
	sess->length = 0;
	sess->capacity = 0;
	sess->complete = False;
	sess->timed_out = False;
	sess->start_time = 0;

	/* Pre-allocate buffer based on estimated size. */
	if (estimated_size > 0) {
		sess->capacity = estimated_size + 4096;
		sess->data = malloc(sess->capacity);
		if (sess->data == NULL) {
			sess->capacity = 0;
		}
	}
	if (sess->data == NULL) {
		/* Fallback: start with a reasonable initial size. */
		sess->capacity = 65536;
		sess->data = malloc(sess->capacity);
		if (sess->data == NULL) {
			free(sess);
			return NULL;
		}
	}

	/* Select PropertyChangeMask so we get PropertyNotify events. */
	if (XGetWindowAttributes(dpy, window, &attrs) != 0) {
		XSelectInput(dpy, window,
			attrs.your_event_mask | PropertyChangeMask);
	}
	XFlush(dpy);

	/* Add to linked list. */
	sess->next = incr_target_list;
	incr_target_list = sess;

	return sess;
}

int
x11dnd_incr_target_handle_property_notify(X11DndIncrTargetSession **sess_ptr,
	XPropertyEvent *pev, X11DndTargetSession *target_sess)
{
	X11DndIncrTargetSession *sess;
	X11DndIncrTargetSession **prev;
	unsigned char *chunk_data;
	unsigned long chunk_len;
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	Time now_ms;

	if (sess_ptr == NULL || *sess_ptr == NULL || pev == NULL) {
		return 0;
	}

	sess = *sess_ptr;

	/* Only handle PropertyNotify(NewValue) on our property. */
	if (pev->type != PropertyNotify ||
		pev->state != PropertyNewValue ||
		pev->window != sess->window ||
		pev->atom != sess->property) {
		return 0;
	}

	/* Track start time for timeout. */
	if (sess->start_time == 0) {
		sess->start_time = pev->time;
	}

	/* Timeout check: 5 seconds per increment. */
	now_ms = pev->time;
	if (sess->start_time != 0 && now_ms > 0 &&
		(now_ms - sess->start_time) > (X11DND_INCR_TIMEOUT_MS)) {
		sess->timed_out = True;
		sess->complete = True;
	} else {
		/* Read the chunk from the property. */
		chunk_data = NULL;
		if (XGetWindowProperty(sess->dpy, sess->window, sess->property,
			0, 0x7FFFFFFF, True, AnyPropertyType,
			&actual_type, &actual_format, &nitems, &bytes_after,
			&chunk_data) != Success || chunk_data == NULL) {
			/* Read failed — treat as 0-length (end of transfer). */
			sess->complete = True;
		} else if (nitems == 0) {
			/* 0-length chunk = end of INCR transfer. */
			sess->complete = True;
			XFree(chunk_data);
		} else {
			size_t unit;
			/* Append chunk to buffer. */
			if (actual_format == 32) {
				unit = sizeof(long);
			} else {
				unit = (size_t)actual_format / 8;
			}
			chunk_len = nitems * unit;

			if (sess->length + chunk_len > sess->capacity) {
				unsigned long new_cap = sess->capacity;
				unsigned char *new_buf;
				while (new_cap < sess->length + chunk_len) {
					new_cap *= 2;
				}
				new_buf = realloc(sess->data, new_cap);
				if (new_buf == NULL) {
					XFree(chunk_data);
					sess->timed_out = True;
					sess->complete = True;
					goto deliver;
				}
				sess->data = new_buf;
				sess->capacity = new_cap;
			}

			memcpy(sess->data + sess->length, chunk_data, chunk_len);
			sess->length += chunk_len;
			XFree(chunk_data);

			/* Delete the property to signal source for next chunk. */
			XDeleteProperty(sess->dpy, sess->window, sess->property);
			XFlush(sess->dpy);
		}
	}

deliver:
	if (sess->complete) {
		/* Remove from linked list. */
		prev = &incr_target_list;
		while (*prev != NULL) {
			if (*prev == sess) {
				*prev = sess->next;
				break;
			}
			prev = &(*prev)->next;
		}

		/* Deliver data via callback. */
		if (target_sess != NULL &&
			target_sess->callbacks != NULL &&
			target_sess->callbacks->drop_received != NULL) {
			target_sess->callbacks->drop_received(target_sess,
				sess->target_type,
				sess->data, sess->length,
				sess->format);
		}

		if (target_sess != NULL) {
			if (target_sess->types) {
				free(target_sess->types);
				target_sess->types = NULL;
			}
			target_sess->n_types = 0;
			target_sess->source_win = None;
			target_sess->negotiated_version = 0;
			target_sess->state = X11DND_TARGET_IDLE;
			target_sess->drop_time = 0;
			target_sess->requested_type = None;
		}

		free(sess->data);
		free(sess);
		*sess_ptr = NULL;
	}

	return 1;
}

void
x11dnd_incr_target_cleanup(X11DndIncrTargetSession *sess)
{
	X11DndIncrTargetSession **prev;

	if (sess == NULL) {
		return;
	}

	/* Remove from linked list. */
	prev = &incr_target_list;
	while (*prev != NULL) {
		if (*prev == sess) {
			*prev = sess->next;
			break;
		}
		prev = &(*prev)->next;
	}

	free(sess->data);
	free(sess);
}