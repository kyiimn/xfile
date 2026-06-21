/*
 * Copyright (C) 2025-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef XDND_H
#define XDND_H

#include <X11/Intrinsic.h>

void xdnd_init(Display *dpy);
void xdnd_destroy(void);
void xdnd_start_drag(Widget source, XEvent *event, XtPointer drag_context);
void xdnd_end_drag(void);
void xdnd_request_finish(void);
Boolean xdnd_has_active_target(void);
Boolean xdnd_convert_selection(Widget w, Atom *selection, Atom *target,
    Atom *type_ret, XtPointer *value_ret, unsigned long *length_ret,
    int *format_ret);

#endif /* XDND_H */
