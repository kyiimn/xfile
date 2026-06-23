/*
 * x11dnd_incr.h - Internal header for INCR (incremental) selection transfer.
 *
 * This is an INTERNAL header for the libx11dnd library, NOT the public API.
 * It implements the ICCCM Section 2.5 INCR protocol for both source and
 * target sides of a selection transfer.
 *
 * Source side: when a SelectionRequest asks for data larger than a threshold,
 * respond with property type INCR and a size hint, then send chunks as the
 * requestor deletes the property.
 *
 * Target side: detect INCR type in SelectionNotify response, select
 * PropertyChangeMask on the window, collect chunks via PropertyNotify
 * events, and assemble the complete data.
 *
 * INCR protocol summary (ICCCM Section 2.5):
 *   1. Source responds to SelectionRequest with property of type INCR and
 *      size hint (lower bound on total data size).
 *   2. Target receives SelectionNotify with INCR type → knows data is
 *      incremental.
 *   3. Target selects PropertyChangeMask on its window.
 *   4. Source deletes the property, then writes first chunk.
 *   5. Target receives PropertyNotify (NewValue) → reads chunk.
 *   6. Target deletes property → Source receives PropertyNotify (Deleted).
 *   7. Steps 4-6 repeat until source writes a 0-length chunk (or deletes
 *      the property with 0 items).
 *   8. Transfer complete.
 */
#ifndef X11DND_INCR_H
#define X11DND_INCR_H

#include <X11/Xlib.h>
#include "x11dnd.h"

/* Default threshold: data larger than this triggers INCR mode. */
#define X11DND_INCR_THRESHOLD 262144 /* 256 KB */

/* Timeout per increment in milliseconds */
#define X11DND_INCR_TIMEOUT_MS 5000

/*
 * INCR transfer state for the source side.
 *
 * Tracks an in-progress incremental transfer from the source (drag owner)
 * to the requestor (target window). The source writes one chunk per
 * PropertyNotify (Deleted) event from the requestor.
 */
typedef struct X11DndIncrSourceSession {
	Display *dpy;
	Window requestor;      /* Window that requested the selection */
	Atom property;         /* Property on requestor window to use */
	Atom target;           /* Requested target type (e.g. text/uri-list) */
	Atom selection;        /* Selection atom (XdndSelection) */
	Time time;             /* Timestamp from SelectionRequest */
	int format;            /* Property format: 8, 16, or 32 */
	unsigned char *data;  /* Complete data to send */
	unsigned long length;  /* Total length of data */
	unsigned long offset;  /* Current offset into data */
	Bool started;          /* True after first PropertyNotify(Deleted) */
	Bool complete;          /* True after 0-length chunk sent */
	struct X11DndIncrSourceSession *next; /* Linked list */
} X11DndIncrSourceSession;

/*
 * INCR transfer state for the target side.
 *
 * Tracks an in-progress incremental transfer from the source to this
 * target. The target reads one chunk per PropertyNotify (NewValue) event.
 */
typedef struct X11DndIncrTargetSession {
	Display *dpy;
	Window window;         /* Our window (requestor) */
	Atom property;        /* Property on our window */
	Atom selection;       /* Selection atom (XdndSelection) */
	Atom target_type;     /* Original requested type (e.g. text/uri-list) */
	int format;           /* Property format: 8, 16, or 32 */
	unsigned long estimated_size; /* Size hint from INCR property value */
	unsigned char *data;   /* Assembled data buffer */
	unsigned long length;  /* Current assembled length */
	unsigned long capacity; /* Buffer capacity */
	Bool complete;         /* True after 0-length chunk received */
	Bool timed_out;        /* True if transfer timed out */
	Time start_time;       /* Time of first PropertyNotify(NewValue) */
	struct X11DndIncrTargetSession *next; /* Linked list */
} X11DndIncrTargetSession;

/* --- Source-side INCR functions --- */

/*
 * Check if data size exceeds the INCR threshold. Returns True if
 * the transfer should use INCR mode, False for immediate transfer.
 */
Bool x11dnd_incr_should_use_incr(unsigned long data_length);

/*
 * Start an INCR transfer on the source side. Sends the initial
 * SelectionNotify with type INCR and a size hint, sets up the
 * session for chunk-by-chunk delivery. Returns the session pointer,
 * or NULL on failure.
 */
X11DndIncrSourceSession *x11dnd_incr_source_start(
	Display *dpy, Window requestor, Atom property, Atom target,
	Atom selection, Time time, int format,
	unsigned char *data, unsigned long length);

/*
 * Handle a PropertyNotify (Deleted) event for the source side.
 * Writes the next chunk of data to the requestor's property.
 * Returns 1 if the event was consumed, 0 otherwise.
 * When all data has been sent (including the final 0-length chunk),
 * the session is automatically freed and *sess is set to NULL.
 */
int x11dnd_incr_source_handle_property_notify(
	X11DndIncrSourceSession **sess, XPropertyEvent *pev);

/*
 * Clean up source-side INCR session. Called when the source loses
 * selection ownership or the drag is cancelled.
 */
void x11dnd_incr_source_cleanup(X11DndIncrSourceSession *sess);

/* --- Target-side INCR functions --- */

/*
 * Start an INCR transfer on the target side. Called when SelectionNotify
 * reports INCR type. Selects PropertyChangeMask on the window and
 * allocates the session. Returns the session pointer, or NULL on failure.
 */
X11DndIncrTargetSession *x11dnd_incr_target_start(
	Display *dpy, Window window, Atom property, Atom selection,
	Atom target_type, int format, unsigned long estimated_size);

/*
 * Handle a PropertyNotify (NewValue) event for the target side.
 * Reads the chunk from the property, appends to the buffer, and
 * deletes the property to signal the source to send the next chunk.
 * Returns 1 if the event was consumed, 0 otherwise.
 * When the transfer is complete (0-length chunk) or timed out,
 * *sess is set to NULL and the data is delivered via the callback.
 */
int x11dnd_incr_target_handle_property_notify(
	X11DndIncrTargetSession **sess, XPropertyEvent *pev,
	X11DndTargetSession *target_sess);

/*
 * Clean up target-side INCR session. Frees all allocated memory.
 */
void x11dnd_incr_target_cleanup(X11DndIncrTargetSession *sess);

#endif /* X11DND_INCR_H */