/*
 * x11dnd_atoms.h - Internal header for XDnD atom management.
 *
 * This is an INTERNAL header for the libx11dnd library, NOT the public API.
 * It declares the atom registry struct and helpers used by other modules
 * within libx11dnd.
 *
 * All XDnD v5 atoms are interned via XInternAtom with the canonical names
 * (NO leading underscore). The old xdnd.c code used "_XdndEnter" etc. which
 * are invisible to GTK/Qt/Chromium — this module fixes that.
 */
#ifndef X11DND_ATOMS_H
#define X11DND_ATOMS_H

#include <X11/Xlib.h>

/*
 * Container for all atoms used by the XDnD protocol (v5) plus a few
 * standard X selection/clipboard atoms needed for data transfer.
 *
 * Field names mirror the atom string names where practical. Fields for
 * atoms whose names contain characters illegal in C identifiers use
 * descriptive suffixes (see comment after each).
 */
typedef struct {
	/* XDnD v5 protocol atoms (no underscore prefix) */
	Atom XdndAware;              /* "XdndAware"            */
	Atom XdndEnter;              /* "XdndEnter"            */
	Atom XdndPosition;           /* "XdndPosition"         */
	Atom XdndStatus;             /* "XdndStatus"           */
	Atom XdndLeave;              /* "XdndLeave"            */
	Atom XdndDrop;               /* "XdndDrop"             */
	Atom XdndFinished;           /* "XdndFinished"         */
	Atom XdndSelection;          /* "XdndSelection"        */
	Atom XdndTypeList;           /* "XdndTypeList"         */
	Atom XdndActionList;         /* "XdndActionList"       */
	Atom XdndActionDescription;  /* "XdndActionDescription"*/
	Atom XdndProxy;              /* "XdndProxy"            */

	/* XDnD action atoms */
	Atom XdndActionCopy;         /* "XdndActionCopy"       */
	Atom XdndActionMove;         /* "XdndActionMove"       */
	Atom XdndActionLink;         /* "XdndActionLink"       */
	Atom XdndActionAsk;          /* "XdndActionAsk"        */
	Atom XdndActionPrivate;      /* "XdndActionPrivate"    */

	/* XDS (Direct Save) atoms */
	Atom XdndDirectSave;         /* "XdndDirectSave"       */
	Atom XdndDirectSave0;        /* "XdndDirectSave0"      */

	/* Standard X selection-transfer atoms */
	Atom INCR;                   /* "INCR"                 */
	Atom TARGETS;                /* "TARGETS"              */
	Atom TIMESTAMP;              /* "TIMESTAMP"            */

	/* Common text/data type atoms */
	Atom UTF8_STRING;            /* "UTF8_STRING"          */
	Atom STRING;                 /* "STRING"               */
	Atom FILE_NAME;              /* "FILE_NAME"            */
	Atom text_uri_list;          /* "text/uri-list"        */
	Atom text_plain;             /* "text/plain"           */
	Atom application_x_file_list;/* "application/x-file-list" */
} X11DndAtoms;

/*
 * Intern all atoms for the given display. Safe to call once per Display;
 * atoms are cached in a static struct keyed by the most-recent display.
 * Passing NULL is a no-op.
 */
void x11dnd_init_atoms(Display *dpy);

/*
 * Return a pointer to the cached atom table. Returns NULL if
 * x11dnd_init_atoms has not been called yet.
 */
const X11DndAtoms *x11dnd_get_atoms(void);

/*
 * Return the canonical string name for a known atom, or "unknown" if the
 * atom is not part of the XDnD atom table.
 */
const char *x11dnd_atom_name(Atom a);

#endif /* X11DND_ATOMS_H */