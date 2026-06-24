dnl ──────────────────────────────────────────────────────────
dnl  XFile — configure.ac
dnl  Copyright (C) 2023-2026 alx@fastestcode.org
dnl  X/MIT license — see COPYING for details.
dnl ──────────────────────────────────────────────────────────

AC_PREREQ([2.69])
AC_INIT([xfile], [1.2.0], [alx@fastestcode.org])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])

AM_INIT_AUTOMAKE([1.14 foreign subdir-objects -Wall -Werror])

dnl ── Compiler & standard ──────────────────────────────────
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MKDIR_P
AM_PROG_AR
AC_PROG_RANLIB

dnl ── Optional: debug build ───────────────────────────────
AC_ARG_ENABLE([debug],
  [AS_HELP_STRING([--enable-debug],
    [Enable debug tracing macros @<:@default=no@:>@])],
  [enable_debug=$enableval],
  [enable_debug=no])

AS_IF([test "x$enable_debug" = "xyes"], [
  AC_DEFINE([DEBUG], [1], [Define to enable debug tracing])
])

dnl ── Required: X11 ────────────────────────────────────────
PKG_CHECK_MODULES([X11], [x11],,
  [AC_PATH_XTRA
   AS_IF([test "x$no_x" = "xyes"],
     [AC_MSG_ERROR([X11 development libraries not found])])
   X11_CFLAGS="$X_CFLAGS"
   X11_LIBS="$X_PRE_LIBS $X_LIBS -lX11 $X_EXTRA_LIBS"
   AC_SUBST([X11_CFLAGS])
   AC_SUBST([X11_LIBS])
  ])

dnl ── Required: Xt ─────────────────────────────────────────
PKG_CHECK_MODULES([XT], [xt],,
  [AC_CHECK_LIB([Xt], [XtAppInitialize],,
     [AC_MSG_ERROR([libXt not found])],
     [$X11_LIBS])
   XT_CFLAGS=""
   XT_LIBS="-lXt"
   AC_SUBST([XT_CFLAGS])
   AC_SUBST([XT_LIBS])
  ])

dnl ── Required: Motif (libXm >= 2.3) ─────────────────────
dnl  Try pkg-config first (some Motif installs ship motif.pc),
dnl  then fall back to AC_CHECK_LIB + header check.
PKG_CHECK_MODULES([MOTIF], [motif],,
  [AC_CHECK_HEADER([Xm/Xm.h],,
     [AC_MSG_ERROR([Motif/Xm header not found])])
   AC_CHECK_LIB([Xm], [XmCreatePushButton],,
     [AC_MSG_ERROR([Motif libXm >= 2.3 not found])],
     [$X11_LIBS $XT_LIBS])
   MOTIF_CFLAGS=""
   MOTIF_LIBS="-lXm"
   AC_SUBST([MOTIF_CFLAGS])
   AC_SUBST([MOTIF_LIBS])
  ])

dnl ── Required: Xpm ────────────────────────────────────────
PKG_CHECK_MODULES([XPM], [xpm],,
  [AC_CHECK_LIB([Xpm], [XpmCreatePixmapFromData],,
     [AC_MSG_ERROR([libXpm not found])],
     [$X11_LIBS])
   XPM_CFLAGS=""
   XPM_LIBS="-lXpm"
   AC_SUBST([XPM_CFLAGS])
   AC_SUBST([XPM_LIBS])
  ])

dnl ── Required: Xinerama ──────────────────────────────────
AC_ARG_ENABLE([xinerama],
  [AS_HELP_STRING([--disable-xinerama],
    [Disable Xinerama multi-monitor support @<:@default=check@:>@])],
  [enable_xinerama=$enableval],
  [enable_xinerama=check])

AS_IF([test "x$enable_xinerama" != "xno"], [
  PKG_CHECK_MODULES([XINERAMA], [xinerama], [have_xinerama=yes],
    [AC_CHECK_LIB([Xinerama], [XineramaQueryScreens],
       [have_xinerama=yes
        XINERAMA_CFLAGS=""
        XINERAMA_LIBS="-lXinerama"
        AC_SUBST([XINERAMA_CFLAGS])
        AC_SUBST([XINERAMA_LIBS])],
       [have_xinerama=no],
       [$X11_LIBS $XEXT_LIBS])])
  AS_IF([test "x$have_xinerama" = "xyes"],
    [AC_DEFINE([HAVE_XINERAMA], [1], [Define if Xinerama is available])],
    [AS_IF([test "x$enable_xinerama" = "xyes"],
       [AC_MSG_ERROR([--enable-xinerama requested but libXinerama not found])],
       [AC_MSG_WARN([Xinerama not found; building without multi-monitor support])
        AC_DEFINE([NO_XINERAMA], [1], [Define to disable Xinerama support])])
    ])
], [have_xinerama=no
     AC_DEFINE([NO_XINERAMA], [1], [Define to disable Xinerama support])])

dnl ── Required: Xext (for XShape in libx11dnd drag icons) ──
PKG_CHECK_MODULES([XEXT], [xext],,
  [AC_CHECK_LIB([Xext], [XShapeCombineMask],,
     [AC_MSG_ERROR([libXext not found])],
     [$X11_LIBS])
   XEXT_CFLAGS=""
   XEXT_LIBS="-lXext"
   AC_SUBST([XEXT_CFLAGS])
   AC_SUBST([XEXT_LIBS])
  ])

dnl ── Required: libm ──────────────────────────────────────
AC_CHECK_LIB([m], [cos],, [AC_MSG_ERROR([libm not found])])

dnl ── Optional: GLib/GIO for XDG open ─────────────────────
AC_ARG_ENABLE([xdg-open],
  [AS_HELP_STRING([--enable-xdg-open],
    [Enable XDG open support via GLib/GIO @<:@default=check@:>@])],
  [enable_xdg_open=$enableval],
  [enable_xdg_open=check])

have_gio=no
AS_IF([test "x$enable_xdg_open" != "xno"], [
  PKG_CHECK_MODULES([GIO], [gio-2.0],
    [have_gio=yes
     AC_DEFINE([USE_GIO], [1], [Define to enable GLib/GIO XDG open support])],
    [AS_IF([test "x$enable_xdg_open" = "xyes"],
       [AC_MSG_ERROR([--enable-xdg-open requested but gio-2.0 not found])],
       [AC_MSG_WARN([gio-2.0 not found; building without XDG open support])])
    ])
], [enable_xdg_open=no])

AM_CONDITIONAL([HAVE_GIO], [test "x$have_gio" = "xyes"])

dnl ── Font variant selection ──────────────────────────────
AC_ARG_ENABLE([xft-fonts],
  [AS_HELP_STRING([--disable-xft-fonts],
    [Use core X fonts instead of Xft font resources @<:@default=xft@:>@])],
  [enable_xft_fonts=$enableval],
  [enable_xft_fonts=yes])

AS_IF([test "x$enable_xft_fonts" = "xyes"],
  [FONT_AD_FILE=fonts-xft.ad],
  [FONT_AD_FILE=fonts.ad])
AC_SUBST([FONT_AD_FILE])

dnl ── App-defaults directory ──────────────────────────────
AC_ARG_WITH([app-defaults-dir],
  [AS_HELP_STRING([--with-app-defaults-dir=DIR],
    [X resource app-defaults directory @<:@default=/etc/X11/app-defaults@:>@])],
  [APPLRESDIR=$withval],
  [APPLRESDIR=/etc/X11/app-defaults])
AC_SUBST([APPLRESDIR])

dnl ── Shared data directory ────────────────────────────────
SHAREDIR='${prefix}/share/xfile'
AC_SUBST([SHAREDIR])

dnl ── Aggregate X libs/CFLAGS ─────────────────────────────
dnl  Combined for cleanliness in src/Makefile.am.
XFILE_X_CFLAGS="$X11_CFLAGS $XT_CFLAGS $MOTIF_CFLAGS $XPM_CFLAGS $XINERAMA_CFLAGS $XEXT_CFLAGS"
XFILE_X_LIBS="$X11_LIBS $XT_LIBS $MOTIF_LIBS $XPM_LIBS $XINERAMA_LIBS $XEXT_LIBS -lm"
AC_SUBST([XFILE_X_CFLAGS])
AC_SUBST([XFILE_X_LIBS])

dnl ── awk (for adtoc.awk) ────────────────────────────────
AC_PROG_AWK

dnl ── Output ──────────────────────────────────────────────
AC_CONFIG_FILES([
  Makefile
  libx11dnd/Makefile
  src/Makefile
])
AC_OUTPUT

dnl ── Configuration summary ────────────────────────────────
AC_MSG_NOTICE([])
AC_MSG_NOTICE([XFile $PACKAGE_VERSION configuration summary:])
AC_MSG_NOTICE([  Prefix:            $prefix])
AC_MSG_NOTICE([  XDG open / GIO:   ${have_gio:-no}])
AC_MSG_NOTICE([  Font resources:    $FONT_AD_FILE])
AC_MSG_NOTICE([  Xinerama:          ${have_xinerama:-no}])
AC_MSG_NOTICE([  Debug:             $enable_debug])
AC_MSG_NOTICE([  App-defaults:      $APPLRESDIR])
AC_MSG_NOTICE([  Shared data:       $SHAREDIR])
AC_MSG_NOTICE([])