#!/bin/sh
# autogen.sh — bootstrap autotools for XFile
# Run this from the top-level directory after cloning.
set -e

echo "Running autoreconf..."
autoreconf --install --force --verbose -I m4

echo ""
echo "Now run:  ./configure && make"
echo ""
echo "Configure options:"
echo "  --enable-xdg-open       Enable XDG open via GLib/GIO (auto-detected)"
echo "  --disable-xdg-open      Disable XDG open support"
echo "  --disable-xft-fonts     Use core X fonts instead of Xft"
echo "  --disable-xinerama      Build without Xinerama multi-monitor support"
echo "  --enable-debug          Enable debug tracing"
echo "  --with-app-defaults-dir=DIR  X resource defaults (default: /etc/X11/app-defaults)"