/*
 * Copyright (C) 2025-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef DND_H
#define DND_H

#include <X11/Intrinsic.h>
#include <Xm/DragDrop.h>

/* Atoms for drag/drop targets */
extern Atom XA_TEXT_URI_LIST;
extern Atom XA_FILE_NAME;
extern Atom XA_XFILE_FILE_LIST;
extern Atom XA_UTF8_STRING;
extern Atom XA_TARGETS;
extern Atom XA_TIMESTAMP;

/* Import targets: atoms the drop site accepts */
extern Atom dnd_import_targets[];

#define DND_NUM_IMPORT_TARGETS 4


/* Drag source operation mask (XmDROP_COPY | XmDROP_MOVE) */
#define DND_DRAG_OPS (XmDROP_COPY | XmDROP_MOVE)

/* Drop site operation mask (XmDROP_COPY | XmDROP_MOVE) */
#define DND_DROP_OPS (XmDROP_COPY | XmDROP_MOVE)

/* Register the given list widget as a Motif drop site */
void dnd_register_drop_site(Widget wlist);

/* Initialize DnD support for the given list widget */
void dnd_init(Widget wlist);

/* Clean up DnD resources */
void dnd_destroy(void);

/* Start a drag operation from the given widget */
void dnd_start_drag(Widget w, XEvent *event);

/* Returns True if an XDnD drag is currently in progress */
Boolean dnd_drag_active(void);

/* End the active drag: sends XdndDrop if over a target, cancels otherwise */
void dnd_end_drag(void);

/* X error handler for DnD operations */
int dnd_xerror_handler(Display *dpy, XErrorEvent *ev);

#endif /* DND_H */