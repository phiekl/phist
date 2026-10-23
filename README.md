# phist (phi's simple terminal)

phist is an X11 terminal emulator forked from [st](https://st.suckless.org/).

## Features

- [Custom font, palette, shell and zoom
  defaults](doc/building.md#default-settings), with local build configuration.
- [Scrollback with resize reflow, page keys and preserved
  clears](doc/scrollback.md).
- [Automatic replies that preserve the history
  view](doc/input.md#input-and-replies).
- [Configurable blinking cursor
  styles](doc/appearance.md#cursor-and-blinking).
- [Colored single, double, curly, dotted and dashed
  underlines](doc/appearance.md#underlines).
- [Clickable URLs with copy feedback](doc/urls.md), including wrapped text and
  history.
- [Expanding multi-click selections](doc/selection.md) for words, paths and
  logical lines.
- [Local mouse actions](doc/selection.md#local-mouse-actions) without
  application mouse reporting.
- [Initial working directory](doc/building.md#initial-working-directory) with
  `-d`.
- [Optional color emoji](doc/appearance.md#emoji-presentation) with
  presentation-selector support.
- [Configurable visual bell animations](doc/appearance.md#visual-bell).

## Build

Requires a C compiler, make, pkg-config, development libraries for Xlib, Xft,
XRender, Fontconfig and FreeType, and `tic` from ncurses.

```sh
make
make terminfo
TERMINFO="$PWD/.terminfo" ./phist
```

The executable is `phist` and the default `TERM` is `phist-256color`. The
terminfo definitions in `st.info` are st's entries renamed to `phist` names,
including the suffixed variants. Install these entries on remote hosts too
when applications there need them.

## Install

```sh
make install
```

This installs `phist` and `phist(1)` under `/usr/local` by default, and
copies the compiled `.terminfo` entries into `/etc/terminfo`, regardless of
`PREFIX`. `make terminfo` compiles `st.info` into the working directory's
`.terminfo` directory; `make install` uses the same target and reuses its
output until `st.info` changes. `make clean` removes the local entries.

Adjust `PREFIX` in `config.mk` or on the make command line as needed.
`DESTDIR` applies to the binary, manual and terminfo entries for staged
installs. `make uninstall` removes the binary and manual, but not the
installed terminfo entries.

## Configure

Edit `config.h` and rebuild. The first build copies it from `config.def.h`. To
adopt updated defaults, save any local changes, remove `config.h`, then run
`make clean` and `make`.

See [the documentation](doc/index.md) for usage, settings and tests.

## License

MIT/X Consortium; see [LICENSE](LICENSE).
