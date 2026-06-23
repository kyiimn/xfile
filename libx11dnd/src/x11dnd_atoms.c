/*
 * x11dnd_atoms.c - XDnD atom registry implementation.
 *
 * CRITICAL: All Xdnd* atoms use the canonical names WITHOUT a leading
 * underscore. The previous xdnd.c code interned "_XdndEnter" etc., which
 * are different atoms from "XdndEnter" and are invisible to GTK/Qt/
 * Chromium. Do not regress this.
 */
#include "x11dnd_atoms.h"

#include <stddef.h>

static X11DndAtoms atoms;
static int atoms_initialized = 0;

void
x11dnd_init_atoms(Display *dpy)
{
	if (dpy == NULL) {
		return;
	}

	atoms.XdndAware             = XInternAtom(dpy, "XdndAware", False);
	atoms.XdndEnter             = XInternAtom(dpy, "XdndEnter", False);
	atoms.XdndPosition          = XInternAtom(dpy, "XdndPosition", False);
	atoms.XdndStatus            = XInternAtom(dpy, "XdndStatus", False);
	atoms.XdndLeave             = XInternAtom(dpy, "XdndLeave", False);
	atoms.XdndDrop              = XInternAtom(dpy, "XdndDrop", False);
	atoms.XdndFinished          = XInternAtom(dpy, "XdndFinished", False);
	atoms.XdndSelection         = XInternAtom(dpy, "XdndSelection", False);
	atoms.XdndTypeList          = XInternAtom(dpy, "XdndTypeList", False);
	atoms.XdndActionList        = XInternAtom(dpy, "XdndActionList", False);
	atoms.XdndActionDescription = XInternAtom(dpy, "XdndActionDescription", False);
	atoms.XdndProxy             = XInternAtom(dpy, "XdndProxy", False);

	atoms.XdndActionCopy        = XInternAtom(dpy, "XdndActionCopy", False);
	atoms.XdndActionMove        = XInternAtom(dpy, "XdndActionMove", False);
	atoms.XdndActionLink        = XInternAtom(dpy, "XdndActionLink", False);
	atoms.XdndActionAsk         = XInternAtom(dpy, "XdndActionAsk", False);
	atoms.XdndActionPrivate     = XInternAtom(dpy, "XdndActionPrivate", False);

	atoms.XdndDirectSave        = XInternAtom(dpy, "XdndDirectSave", False);
	atoms.XdndDirectSave0       = XInternAtom(dpy, "XdndDirectSave0", False);

	atoms.INCR                  = XInternAtom(dpy, "INCR", False);
	atoms.TARGETS               = XInternAtom(dpy, "TARGETS", False);
	atoms.TIMESTAMP             = XInternAtom(dpy, "TIMESTAMP", False);

	atoms.UTF8_STRING            = XInternAtom(dpy, "UTF8_STRING", False);
	atoms.STRING                 = XInternAtom(dpy, "STRING", False);
	atoms.FILE_NAME              = XInternAtom(dpy, "FILE_NAME", False);
	atoms.text_uri_list          = XInternAtom(dpy, "text/uri-list", False);
	atoms.text_plain             = XInternAtom(dpy, "text/plain", False);
	atoms.application_x_file_list = XInternAtom(dpy, "application/x-file-list", False);

	atoms_initialized = 1;
}

const X11DndAtoms *
x11dnd_get_atoms(void)
{
	if (!atoms_initialized) {
		return NULL;
	}
	return &atoms;
}

const char *
x11dnd_atom_name(Atom a)
{
	if (!atoms_initialized) {
		return "unknown";
	}

	if (a == atoms.XdndAware)             return "XdndAware";
	if (a == atoms.XdndEnter)             return "XdndEnter";
	if (a == atoms.XdndPosition)          return "XdndPosition";
	if (a == atoms.XdndStatus)            return "XdndStatus";
	if (a == atoms.XdndLeave)             return "XdndLeave";
	if (a == atoms.XdndDrop)               return "XdndDrop";
	if (a == atoms.XdndFinished)          return "XdndFinished";
	if (a == atoms.XdndSelection)         return "XdndSelection";
	if (a == atoms.XdndTypeList)          return "XdndTypeList";
	if (a == atoms.XdndActionList)        return "XdndActionList";
	if (a == atoms.XdndActionDescription) return "XdndActionDescription";
	if (a == atoms.XdndProxy)             return "XdndProxy";

	if (a == atoms.XdndActionCopy)        return "XdndActionCopy";
	if (a == atoms.XdndActionMove)        return "XdndActionMove";
	if (a == atoms.XdndActionLink)        return "XdndActionLink";
	if (a == atoms.XdndActionAsk)         return "XdndActionAsk";
	if (a == atoms.XdndActionPrivate)     return "XdndActionPrivate";

	if (a == atoms.XdndDirectSave)        return "XdndDirectSave";
	if (a == atoms.XdndDirectSave0)       return "XdndDirectSave0";

	if (a == atoms.INCR)                  return "INCR";
	if (a == atoms.TARGETS)               return "TARGETS";
	if (a == atoms.TIMESTAMP)             return "TIMESTAMP";

	if (a == atoms.UTF8_STRING)           return "UTF8_STRING";
	if (a == atoms.STRING)                return "STRING";
	if (a == atoms.FILE_NAME)             return "FILE_NAME";
	if (a == atoms.text_uri_list)         return "text/uri-list";
	if (a == atoms.text_plain)            return "text/plain";
	if (a == atoms.application_x_file_list) return "application/x-file-list";

	return "unknown";
}