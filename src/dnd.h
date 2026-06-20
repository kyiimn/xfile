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

/* Target atom arrays for drag source export and drop site import */
extern Atom dnd_export_targets[];
extern Atom dnd_import_targets[];

#define DND_NUM_EXPORT_TARGETS 4
#define DND_NUM_IMPORT_TARGETS 4

/* Drag source operation mask (XmDROP_COPY | XmDROP_MOVE) */
#define DND_DRAG_OPS (XmDROP_COPY | XmDROP_MOVE)

/* Drop site operation mask (XmDROP_COPY | XmDROP_MOVE) */
#define DND_DROP_OPS (XmDROP_COPY | XmDROP_MOVE)

/* Register the given list widget as a drop site */
void dnd_register_drop_site(Widget wlist);

/* Initialize DnD support for the given list widget */
void dnd_init(Widget wlist);

/* Clean up DnD resources */
void dnd_destroy(void);

void dnd_start_drag(Widget w, XEvent *event);

#endif /* DND_H */
