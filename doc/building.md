# Building and configuration

## Build and configure

Build from the repository root with `make`. The Makefile copies
`config.def.h` to `config.h` only when `config.h` does not exist. Edit
`config.h` for a local configuration, or edit `config.def.h` to change the
versioned defaults.

After changing the defaults, regenerate your configuration and rebuild:

```sh
rm config.h
make clean
make
```

Save any local settings first: regenerating `config.h` replaces them with
the defaults. If keeping an older `config.h`, copy in new setting
definitions when a feature adds them; otherwise the next build may fail.

`config.h`, the `phist` executable and object files are generated and ignored
by Git. Versioned configuration belongs in `config.def.h`.

## Terminal type

The configured default is `TERM=phist-256color`. Run `make terminfo` to
compile `st.info` into `.terminfo` in the working directory. For a local
run, use `TERMINFO="$PWD/.terminfo" ./phist` so applications find these
entries even after changing directory.

`make install` depends on the same target and copies its output into
`/etc/terminfo`, honoring `DESTDIR` for staged installs. The local entries
are rebuilt only when missing or when `st.info` changes; `make clean`
removes them. The source filename stays `st.info`; its entries are st's,
renamed: `phist`, `phist-mono`, `phist-256color`, `phist-meta`,
`phist-meta-256color`, `phist-bs` and `phist-bs-256color`.

`phist` advertises 8 colors and `phist-256color` 256 colors, while
`phist-mono` is the monochrome variant. The meta and backspace variants
build on `phist`, and their `-256color` counterparts on `phist-256color`.

Install these entries on remote hosts where programs run under this
terminal type. An existing generated `config.h` keeps its own `termname`;
set it to `"phist-256color"` or regenerate it from `config.def.h` to adopt the
new default.

## Initial working directory

Use `phist -d path` to choose the directory inherited by the shell or command:

```sh
phist -d /tmp
phist -d ../project -e make
```

Relative paths are resolved from the directory where phist was launched.
Without `-d`, the inherited working directory is kept. Failure to enter the
requested directory exits with the path and system error.

Relative `-o` output-log and `-l` terminal-device paths still refer to the
launching directory: phist opens those before changing directory. In shell
mode, the child changes directory before execution; in line mode, phist changes
directory after opening the files and before invoking `stty`.

## Dependencies

Building requires a C compiler and development files for Xlib, Xft, XRender,
Fontconfig and FreeType. `pkg-config` supplies Fontconfig and FreeType
flags. XRender supplies the visual-bell overlay; desktop transparency and a
compositing window manager are not required.
