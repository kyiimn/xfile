# AGENTS.md — XFile 1.2.0

XFile is a Motif (Open Motif / libXm) file manager for X11, written in C99.

## Build System

- **No configure script.** Platform selection is done via `make <platform>` where `<platform>` matches a file in `mf/Makefile.<platform>`.
- Available platforms: `Linux`, `FreeBSD`, `NetBSD`, `OpenBSD`, `SunOS`, or `generic`.
- `make Linux` symlinks `mf/Makefile.Linux` → `src/Makefile`, then builds.
- `make distclean` removes `src/Makefile` and build artifacts.
- The master `Makefile` delegates everything to `src/Makefile` (the symlinked platform file).

### Build dependencies

```
libX11, libXinerama, libXt, libXpm, libXm (Motif ≥2.3), libm
```

### Build commands

```sh
make Linux        # build for Linux (creates src/Makefile symlink)
make              # auto-detects platform via uname(1)
make install      # installs as root (PREFIX=/usr by default)
make distclean    # remove build artifacts + src/Makefile symlink
```

### Key Make variables (set in `mf/Makefile.<platform>` or override on command line)

| Variable    | Default                   | Purpose                               |
|-------------|---------------------------|---------------------------------------|
| `PREFIX`    | `/usr`                    | Installation prefix                   |
| `MANDIR`    | `$(PREFIX)/share/man`     | Man page directory                    |
| `SHAREDIR`  | `$(PREFIX)/share/xfile`   | Shared data (icons, type databases)   |
| `APPLRESDIR`| `/etc/X11/app-defaults`   | X resource defaults location          |
| `APPDEFS`   | `$(APPDEFS_XFT)`          | Set to `$(APPDEFS_CORE)` for core fonts |

## Architecture

Single-directory C project — all source lives in `src/`.

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

### Generated files (never edit directly)

| File          | Generated from                | Generator            |
|---------------|-------------------------------|----------------------|
| `icons.h`     | `icons/*.xpm` + `mkicons.sh` | Shell script         |
| `defaults.c`  | `xfile.ad`                    | `awk -f adtoc.awk`  |
| `xfile.ad`    | `res/prefs.ad`, `res/fonts-xft.ad`, `res/accels.ad`, `res/misc.ad` | `cat` |
| `xfile.1`     | `man/*.man`                   | `cat`                |
| `.depend`     | `*.c` files                  | `cc -MM`             |

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

## Installation quirks

- `make install` requires root (`install -o 0 -g 0`).
- Installs a symlink: `PREFIX/bin/xfile-open` → `PREFIX/bin/xfile`.
- Icons go to `PREFIX/share/xfile/icons/`, type DB to `PREFIX/share/xfile/types/`.
- Desktop pixmap: `xbm/cabinet.xpm` → `PREFIX/share/pixmaps/xfile_48x48.xpm`.
- The app-defaults file is installed as `/etc/X11/app-defaults/XFile` (configurable via `APPLRESDIR`).