# AGENTS.md — XFile 1.2.0

XFile is a Motif (Open Motif / libXm) file manager for X11, written in C99.

## Build System

GNU Autotools (autoconf/automake). The project uses `configure.ac` + `Makefile.am` with a three-directory structure (`libx11dnd/`, `src/`, top-level).

### Build dependencies

**Required:**
```
libX11, libXt, libXm (Motif ≥2.3), libXpm, libXinerama, libXext, libm
autoconf ≥2.69, automake ≥1.14
pkg-config
```

**Optional:**
```
gio-2.0 (GLib/GIO) — enables XDG open support (USE_GIO)
```

### Build commands

```sh
./autogen.sh              # bootstrap (first time, or after configure.ac changes)
./configure               # detect dependencies, generate Makefiles
make                      # build
make install              # install (PREFIX=/usr/local by default)
make distclean            # remove all build artifacts + generated configure
```

### Configure options

| Option                         | Default                 | Description                                     |
|--------------------------------|-------------------------|-------------------------------------------------|
| `--prefix=DIR`                 | `/usr/local`            | Installation prefix                              |
| `--enable-xdg-open`            | auto-detect             | Enable XDG open via GLib/GIO                    |
| `--disable-xdg-open`           | —                       | Disable GIO dependency                          |
| `--disable-xft-fonts`           | Xft fonts               | Use core X fonts instead of Xft                 |
| `--disable-xinerama`           | auto-detect             | Build without Xinerama multi-monitor support    |
| `--enable-debug`                | no                      | Enable debug tracing macros                      |
| `--with-app-defaults-dir=DIR`  | `/etc/X11/app-defaults` | X resource app-defaults directory                |

### Structure

- `configure.ac` — Autoconf input (dependency detection, configure options)
- `Makefile.am` — Top-level automake (SUBDIRS = libx11dnd src)
- `libx11dnd/Makefile.am` — Convenience static library (not installed)
- `src/Makefile.am` — Main binary, generated sources, data installation
- `m4/` — Autoconf macro directory
- `build-aux/` — Install helpers (install-sh, depcomp, etc.)

### libx11dnd build

- `libx11dnd/` is built as a convenience static archive (`noinst_LIBRARIES`) before `src/`.
- It produces `libx11dnd/libx11dnd.a`, linked directly into the xfile binary.
- Not installed standalone — only used internally by xfile.

## Architecture

Single-directory C project — all source lives in `src/`, except for the separate `libx11dnd/` library.

### Module map

| File(s)       | Responsibility                              |
|---------------|---------------------------------------------|
| `main.c/h`    | Entry point, app globals, signal handlers   |
| `menu.c/h`    | Menu bar creation                           |
| `listw.c/h`   | Custom XmContainer-based file list widget    |
| `pathw.c/h`   | Path navigation bar widget                 |
| `filemgr.c/h` | File manager orchestration (read dir, etc.) |
| `cbproc.c/h`  | Menu/callback procedures                   |
| `fsproc.c/h`  | File operations (copy, move, delete, etc.) |
| `fstab.c/h`   | Filesystem table (mount points)            |
| `mount.c/h`   | Mount/unmount operations                   |
| `typedb.c/h`  | File type database engine                  |
| `graphics.c/h`| Icon rendering, pixmaps                    |
| `exec.c/h`    | Command execution (fork/exec)              |
| `comdlgs.c/h` | Common dialogs                             |
| `guiutil.c/h` | GUI utility functions                      |
| `attrib.c`    | File attribute/properties dialog            |
| `select.c/h`  | Selection management                       |
| `path.c/h`    | Path string utilities                      |
| `fsutil.c/h`  | Filesystem utility functions               |
| `mbstr.c/h`   | Multibyte string handling                  |
| `stack.c/h`   | Generic stack data structure               |
| `progw.c/h`   | Progress dialog widget                     |
| `usrtool.c/h` | User tools menu                            |
| `info.c/h`    | About/info dialog                          |
| `debug.c/h`   | Conditional debug macros (`dbg_trace`, etc.)|
| `defaults.c`  | **Generated** — compiled-in fallback X resources |
| `dnd.c/h`     | DnD adapter: XDnD via libx11dnd, Motif drop target (legacy) |

### libx11dnd module map

| File(s)                                          | Responsibility                                      |
|--------------------------------------------------|----------------------------------------------------|
| `libx11dnd/src/x11dnd_atoms.c/h`                 | XDnD atom management                               |
| `libx11dnd/src/x11dnd_source.c/h`                | Drag source state machine                          |
| `libx11dnd/src/x11dnd_target.c/h`                | Drop target state machine                          |
| `libx11dnd/src/x11dnd_action.c/h`                | Action negotiation                                 |
| `libx11dnd/src/x11dnd_reply.c`                   | `XdndStatus`/`XdndFinished` reply builder           |
| `libx11dnd/src/x11dnd_incr.c/h`                  | INCR transfer support                              |
| `libx11dnd/src/x11dnd_dsave.c/h`                 | `XdndDirectSave` protocol                          |
| `libx11dnd/src/x11dnd_proxy.c/h`                 | `XdndProxy` support                                |
| `libx11dnd/src/x11dnd_util.c/h`                  | X11 window utilities                               |
| `libx11dnd/src/x11dnd_xt.c`                      | Xt integration wrapper                             |
| `libx11dnd/src/x11dnd_stub.c`                    | Library init/destroy stubs                         |

### Generated files (never edit directly)

| File                | Generated from                | Generator            |
|---------------------|-------------------------------|----------------------|
| `icons.h`           | `icons/*.xpm` + `mkicons.sh` | Shell script         |
| `defaults.c`        | `xfile.ad`                    | `awk -f adtoc.awk`  |
| `xfile.ad`          | `res/prefs.ad`, `res/fonts-xft.ad`, `res/accels.ad`, `res/misc.ad` | `cat` |
| `xfile.1`           | `man/*.man`                   | `cat`                |

### Build artifacts (compiled, not generated by scripts)

| File                | Origin                          |
|---------------------|---------------------------------|
| `libx11dnd/libx11dnd.a` | All `libx11dnd/src/*.c` files |

### X resources pipeline

`res/*.ad` → `cat` → `xfile.ad` → `awk -f adtoc.awk` → `defaults.c` (compiled into binary as fallback resources).

Users can override via `~/.Xresources` or `$HOME/.xfile/` config directory.

## File Type Database

- System DB: `PREFIX/share/xfile/types/system.db` (installed from `default.db`)
- User DB: `$HOME/.xfile/types/*.db`
- Format: plain-text RC syntax with blocks defining `icon`, `match_name`, `match_content`, `action`.
- Built-in variables: `%n` (filename), `%p` (path), `%m` (mime), `%u` (user param).
- Environment variables (`terminal`, `textEditor`, etc.) are resolved from X resources `XFile.variable.*`.

## Coding Conventions

- C99, Motif/Xm widget toolkit, Xt intrinsics.
- Include guards: `#ifndef NAME_H / #define NAME_H`.
- Debug macros compile to no-ops unless `DEBUG` is defined.
- Global app state in two structs: `app_resources` (X resource fields) and `app_inst_data` (runtime state) — both in `main.h`.
- Signal handling uses POSIX `sigaction` via `rsignal()` wrapper.
- All `.o` files are in `src/` alongside their `.c` and `.h` — no subdirectory build.

### libx11dnd conventions

- libx11dnd is C99, pure Xlib core, no Xt or Motif dependencies in core files.
- Only `x11dnd_xt.c` may include Xt headers.
- Atom names use NO underscore prefix (`XdndEnter` not `_XdndEnter`).
- Callback-based API (`X11DndClass` pattern) for toolkit independence.

## Installation quirks

- `make install` requires root for system directories.
- Installs a symlink: `PREFIX/bin/xfile-open` → `PREFIX/bin/xfile`.
- Icons go to `PREFIX/share/xfile/icons/`, type DB to `PREFIX/share/xfile/types/`.
- Desktop pixmap: `xbm/cabinet.xpm` → `PREFIX/share/pixmaps/xfile_48x48.xpm`.
- The app-defaults file is installed as `APPLRESDIR/XFile` (configurable via `--with-app-defaults-dir`).