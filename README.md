# phist (phi's simple terminal)

phist is an X11 terminal emulator forked from [st](https://st.suckless.org/).

## Features

- [Local configuration and clean build outputs](doc/building.md).

## Build

Requires a C compiler, make, pkg-config, development libraries for Xlib, Xft,
Fontconfig and FreeType, and `tic` from ncurses.

```sh
make
tic -sx -o "$HOME/.terminfo" st.info
./phist
```

The executable is `phist` and the default `TERM` is `phist-256color`. The
terminfo definitions in `st.info` are st's entries renamed to `phist` names,
including the suffixed variants. Install these entries on remote hosts too
when applications there need them.

## Install

```sh
make install
```

This installs `phist` and `phist(1)` under `/usr/local` by default, and runs
`tic -sx st.info`. Adjust `PREFIX` in `config.mk` or on the make command line
as needed. `make uninstall` removes the binary and manual.

## Configure

Edit `config.h` and rebuild. The first build copies it from `config.def.h`. To
adopt updated defaults, save any local changes, remove `config.h`, then run
`make clean` and `make`.

See [the documentation](doc/index.md) for usage, settings and tests.

## License

MIT/X Consortium; see [LICENSE](LICENSE).
