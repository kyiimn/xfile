/*
 * x11dnd_stub.c — Library lifecycle functions.
 *
 * x11dnd_init() and x11dnd_destroy() are the public API entry points
 * declared in x11dnd.h. The core library modules (source, target, etc.)
 * are pure Xlib; x11dnd_xt.c provides the Xt convenience wrappers.
 *
 * x11dnd_init interns all XDnD atoms and resets per-display state.
 * x11dnd_destroy releases per-display state and cancels active sessions.
 */

#include "x11dnd.h"
#include "x11dnd_atoms.h"

/* Per-display state. Currently we only track the display pointer
 * for cleanup. The atoms are stored in x11dnd_atoms.c. */
static Display *g_dpy = NULL;

int
x11dnd_init(Display *dpy)
{
	if (dpy == NULL)
		return -1;

	/* Intern all XDnD protocol atoms. */
	x11dnd_init_atoms(dpy);

	g_dpy = dpy;
	return 0;
}

void
x11dnd_destroy(Display *dpy)
{
	/* Currently a no-op beyond clearing the global pointer.
	 * Active source/target sessions should be cancelled by
	 * the caller before destroying. */
	if (g_dpy == dpy)
		g_dpy = NULL;
}