/*
 * x11dnd_action.c - XDnD action negotiation and property helpers.
 *
 * Implements mapping between the X11DndAction enum and interned
 * XdndAction* atoms, action negotiation (offered vs. supported),
 * and reading/writing the XdndActionList and XdndActionDescription
 * window properties defined by the XDnD v5 specification.
 */
#include "x11dnd_action.h"
#include "x11dnd_atoms.h"
#include "x11dnd_util.h"

#include <X11/Xatom.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

Atom
x11dnd_action_to_atom(Display *dpy, X11DndAction action)
{
	const X11DndAtoms *atoms;

	(void)dpy;
	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return None;
	}

	switch (action) {
	case X11DND_ACTION_COPY:        return atoms->XdndActionCopy;
	case X11DND_ACTION_MOVE:        return atoms->XdndActionMove;
	case X11DND_ACTION_LINK:        return atoms->XdndActionLink;
	case X11DND_ACTION_ASK:         return atoms->XdndActionAsk;
	case X11DND_ACTION_PRIVATE:     return atoms->XdndActionPrivate;
	case X11DND_ACTION_DIRECT_SAVE: return atoms->XdndDirectSave;
	default:                        return None;
	}
}

X11DndAction
x11dnd_atom_to_action(Display *dpy, Atom atom)
{
	const X11DndAtoms *atoms;

	(void)dpy;
	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return X11DND_ACTION_COPY;
	}

	if (atom == atoms->XdndActionCopy)    return X11DND_ACTION_COPY;
	if (atom == atoms->XdndActionMove)    return X11DND_ACTION_MOVE;
	if (atom == atoms->XdndActionLink)    return X11DND_ACTION_LINK;
	if (atom == atoms->XdndActionAsk)     return X11DND_ACTION_ASK;
	if (atom == atoms->XdndActionPrivate) return X11DND_ACTION_PRIVATE;
	if (atom == atoms->XdndDirectSave)    return X11DND_ACTION_DIRECT_SAVE;

	return X11DND_ACTION_COPY;
}

Atom
x11dnd_negotiate_action(Atom offered, Atom *supported, int n_supported)
{
	const X11DndAtoms *atoms;
	int i;

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return None;
	}

	if (offered == atoms->XdndActionAsk) {
		return atoms->XdndActionAsk;
	}

	if (supported != NULL) {
		for (i = 0; i < n_supported; i++) {
			if (supported[i] == offered) {
				return offered;
			}
		}
	}

	return atoms->XdndActionCopy;
}

const char *
x11dnd_action_to_string(Atom action)
{
	const X11DndAtoms *atoms;

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return "unknown";
	}

	if (action == atoms->XdndActionCopy)    return "XdndActionCopy";
	if (action == atoms->XdndActionMove)    return "XdndActionMove";
	if (action == atoms->XdndActionLink)    return "XdndActionLink";
	if (action == atoms->XdndActionAsk)     return "XdndActionAsk";
	if (action == atoms->XdndActionPrivate) return "XdndActionPrivate";
	if (action == atoms->XdndDirectSave)    return "XdndActionDirectSave";

	return "unknown";
}

Atom
x11dnd_string_to_action(const char *name, Display *dpy)
{
	if (name == NULL || dpy == NULL) {
		return None;
	}
	return XInternAtom(dpy, name, True);
}

int
x11dnd_set_action_list(Display *dpy, Window win, Atom *actions, int n_actions)
{
	const X11DndAtoms *atoms;

	atoms = x11dnd_get_atoms();
	if (atoms == NULL || dpy == NULL || win == None || actions == NULL) {
		return 1;
	}
	if (n_actions <= 0) {
		return 1;
	}

	XChangeProperty(dpy, win, atoms->XdndActionList, XA_ATOM, 32,
		PropModeReplace, (unsigned char *)actions, n_actions);
	return 0;
}

int
x11dnd_set_action_descriptions(Display *dpy, Window win,
	char **descriptions, int n_desc)
{
	const X11DndAtoms *atoms;
	size_t total_len;
	int i;
	unsigned char *buf;
	size_t offset;

	atoms = x11dnd_get_atoms();
	if (atoms == NULL || dpy == NULL || win == None ||
		descriptions == NULL || n_desc <= 0) {
		return 1;
	}

	total_len = 0;
	for (i = 0; i < n_desc; i++) {
		total_len += strlen(descriptions[i]) + 1;
	}

	buf = (unsigned char *)malloc(total_len);
	if (buf == NULL) {
		return 1;
	}

	offset = 0;
	for (i = 0; i < n_desc; i++) {
		size_t len = strlen(descriptions[i]) + 1;
		memcpy(buf + offset, descriptions[i], len);
		offset += len;
	}

	XChangeProperty(dpy, win, atoms->XdndActionDescription, XA_STRING, 8,
		PropModeReplace, buf, (int)total_len);
	free(buf);
	return 0;
}

Atom *
x11dnd_get_action_list(Display *dpy, Window win, int *n_actions)
{
	const X11DndAtoms *atoms;
	unsigned char *data;
	unsigned long nitems;
	int rc;
	Atom *result;
	unsigned long i;

	if (n_actions) {
		*n_actions = 0;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL || dpy == NULL || win == None) {
		return NULL;
	}

	rc = x11dnd_get_window_property(dpy, win, atoms->XdndActionList,
		XA_ATOM, 32, &nitems, &data);
	if (rc != 0 || data == NULL || nitems == 0) {
		if (data) {
			free(data);
		}
		return NULL;
	}

	result = (Atom *)malloc(nitems * sizeof(Atom));
	if (result == NULL) {
		free(data);
		return NULL;
	}

	for (i = 0; i < nitems; i++) {
		result[i] = ((Atom *)data)[i];
	}

	free(data);
	if (n_actions) {
		*n_actions = (int)nitems;
	}
	return result;
}

char **
x11dnd_get_action_descriptions(Display *dpy, Window win, int *n_desc)
{
	const X11DndAtoms *atoms;
	unsigned char *data;
	unsigned long nitems;
	int rc;
	char **result;
	int count;
	unsigned long i;
	unsigned long start;

	if (n_desc) {
		*n_desc = 0;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL || dpy == NULL || win == None) {
		return NULL;
	}

	rc = x11dnd_get_window_property(dpy, win, atoms->XdndActionDescription,
		XA_STRING, 8, &nitems, &data);
	if (rc != 0 || data == NULL || nitems == 0) {
		if (data) {
			free(data);
		}
		return NULL;
	}

	count = 0;
	for (i = 0; i < nitems; i++) {
		if (data[i] == '\0') {
			count++;
		}
	}
	if (count == 0) {
		count = 1;
	}

	result = (char **)malloc(count * sizeof(char *));
	if (result == NULL) {
		free(data);
		return NULL;
	}

	count = 0;
	start = 0;
	for (i = 0; i < nitems; i++) {
		if (data[i] == '\0') {
			size_t len = i - start + 1;
			result[count] = (char *)malloc(len);
			if (result[count] == NULL) {
				int j;
				for (j = 0; j < count; j++) {
					free(result[j]);
				}
				free(result);
				free(data);
				return NULL;
			}
			memcpy(result[count], data + start, len);
			count++;
			start = i + 1;
		}
	}

	if (count == 0 && nitems > 0) {
		result[count] = (char *)malloc(nitems + 1);
		if (result[count] == NULL) {
			free(result);
			free(data);
			return NULL;
		}
		memcpy(result[count], data, nitems);
		result[count][nitems] = '\0';
		count++;
	}

	free(data);
	if (n_desc) {
		*n_desc = count;
	}
	return result;
}

const char *
x11dnd_default_action_description(Atom action)
{
	const X11DndAtoms *atoms;

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return "Unknown";
	}

	if (action == atoms->XdndActionCopy)    return "Copy";
	if (action == atoms->XdndActionMove)    return "Move";
	if (action == atoms->XdndActionLink)    return "Link";
	if (action == atoms->XdndActionAsk)     return "Ask";
	if (action == atoms->XdndActionPrivate) return "Private";
	if (action == atoms->XdndDirectSave)    return "Direct Save";

	return "Unknown";
}