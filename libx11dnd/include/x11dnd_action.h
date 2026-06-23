/*
 * x11dnd_action.h - Internal header for XDnD action negotiation.
 *
 * This is an INTERNAL header for the libx11dnd library, NOT the public API.
 * It declares helpers for mapping between the X11DndAction enum and the
 * interned XdndAction* atoms, negotiating the action to perform between
 * source and target, and reading/writing the XdndActionList and
 * XdndActionDescription window properties.
 */
#ifndef X11DND_ACTION_H
#define X11DND_ACTION_H

#include <X11/Xlib.h>
#include "x11dnd.h"

/*
 * Map an X11DndAction enum value to the corresponding interned XdndAction*
 * atom. Returns None if the atoms have not been initialized or the enum
 * value is out of range.
 */
Atom x11dnd_action_to_atom(Display *dpy, X11DndAction action);

/*
 * Map an interned XdndAction* atom back to the X11DndAction enum.
 * Returns X11DND_ACTION_COPY if the atom does not match any known action
 * (COPY is the safe default per the XDnD specification).
 */
X11DndAction x11dnd_atom_to_action(Display *dpy, Atom atom);

/*
 * Negotiate an offered action against a list of supported actions.
 *
 * If 'offered' is XdndActionAsk, returns XdndActionAsk unchanged so the
 * caller can invoke the application's action_ask callback.
 *
 * Otherwise searches 'supported' for 'offered'. If found, returns 'offered'.
 * If not found, returns XdndActionCopy (the default fallback).
 *
 * 'supported' may be NULL or 'n_supported' may be 0 — in that case the
 * fallback XdndActionCopy is returned (unless offered is XdndActionAsk).
 */
Atom x11dnd_negotiate_action(Atom offered, Atom *supported, int n_supported);

/*
 * Return the canonical string name for an action atom
 * (e.g. "XdndActionCopy"). Returns "unknown" for unrecognized atoms.
 * The returned pointer is a static string and must not be freed.
 */
const char *x11dnd_action_to_string(Atom action);

/*
 * Parse a string action name (e.g. "XdndActionCopy") into an atom.
 * Uses XInternAtom with only_if_exists=True, so only already-interned
 * atoms are recognized. Returns None if the name is not a known atom.
 */
Atom x11dnd_string_to_action(const char *name, Display *dpy);

/*
 * Set the XdndActionList property on 'win' to the array of action atoms.
 * The property type is XA_ATOM, format 32, PropModeReplace.
 * Returns 0 on success, non-zero on failure.
 */
int x11dnd_set_action_list(Display *dpy, Window win, Atom *actions,
	int n_actions);

/*
 * Set the XdndActionDescription property on 'win'.
 * 'descriptions' is an array of NUL-terminated strings; they are packed
 * into a single contiguous buffer separated by NUL bytes (as required by
 * the XDnD specification for XdndActionDescription).
 * Property type is XA_STRING, format 8, PropModeReplace.
 * Returns 0 on success, non-zero on failure.
 */
int x11dnd_set_action_descriptions(Display *dpy, Window win,
	char **descriptions, int n_desc);

/*
 * Read the XdndActionList property from 'win'.
 * Returns a malloc'd array of Atom (caller must free) and sets *n_actions.
 * Returns NULL if the property is absent or the read fails.
 */
Atom *x11dnd_get_action_list(Display *dpy, Window win, int *n_actions);

/*
 * Read the XdndActionDescription property from 'win'.
 * The property is a sequence of NUL-separated strings. This function splits
 * them into a malloc'd array of malloc'd strings (caller must free each
 * string and the array itself).
 * Sets *n_desc to the number of descriptions. Returns NULL on failure.
 */
char **x11dnd_get_action_descriptions(Display *dpy, Window win, int *n_desc);

/*
 * Return a human-readable description string for a standard action atom.
 * Returns "Unknown" for unrecognized atoms. The returned pointer is a
 * static string and must not be freed.
 */
const char *x11dnd_default_action_description(Atom action);

#endif /* X11DND_ACTION_H */