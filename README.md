# phist (phi's simple terminal)

phist is an X11 terminal emulator forked from [st](https://st.suckless.org/).

## Features

- [Compressed, reflowing scrollback](doc/scrollback.md) with page keys and
  history preserved across clears and resets.
- [Incremental scrollback search](doc/scrollback.md#searching) with Ctrl+/,
  wrapped text, Unicode and highlighted matches.
- [Expanding text selections](doc/selection.md#expanding-a-selection) that
  grow from a part to the whole of IPv6 and MAC addresses, prefix lengths,
  `user@host` and unit-file values, [selecting with Vim's
  keys](doc/selection.md#keyboard-selection) and [incremental transfers for
  large copies](doc/selection.md#sending-large-selections).
- [Clickable URLs and OSC 8 hyperlinks](doc/urls.md), with copy feedback and
  a target preview box, including wrapped text and history.
- [Paste confirmation with selectable, copyable previews](doc/pasting.md),
  filtering and bounded transfers.
- [OSC 7 directory reports](doc/working-directory.md) exposed through an X11
  window property for `xprop` and window-manager scripts.
- [Disabled application mouse reporting](doc/selection.md#local-mouse-actions),
  keeping mouse actions local, and disabled [focus
  reporting](doc/terminal-behavior.md#focus-reporting).
- [Colored single, double, curly, dotted and dashed
  underlines](doc/appearance.md#underlines).
- [Color emoji](doc/appearance.md#emoji-presentation) and [canonical
  combining-character composition](doc/appearance.md#combining-characters).
- [Visible concealed text and minimum
  contrast](doc/appearance.md#concealed-and-low-contrast-text), with [opt-in
  palette changes](doc/terminal-behavior.md#palette-changes).
- [Configurable visual bell animations](doc/appearance.md#visual-bell) with
  [rate limiting](doc/appearance.md#bell-rate-limit).
- [Configurable blinking cursor
  styles](doc/appearance.md#cursor-and-blinking).
- [Initial working directory](doc/building.md#initial-working-directory) with
  `-d`.
- [Custom font, palette, shell and zoom
  defaults](doc/building.md#default-settings), with local build configuration.
- [Consistent Delete keys and nonblocking input/reply queues](doc/input.md),
  preserving order and terminal responsiveness.
- [Text-area size queries](doc/input.md#window-size-query) (`CSI 18 t`),
  answered in character cells.
- [Filtered, bounded titles](doc/window-titles.md) with combining-mark limits
  and updates batched with drawing.
- [Output controls and parser limits](doc/terminal-behavior.md), with bounded
  diagnostics and silent Vim compatibility probes.
- [Headless tests and fuzzing](doc/testing.md), with [parser and wide-cell
  bounds fixes](doc/terminal-behavior.md#parser-bounds).
- [Batched reverse-video redraws](doc/appearance.md#reverse-video-updates).
- Protection against hostile output: [printing only to an `-o`
  file](doc/terminal-behavior.md#media-copy), ignored [keyboard
  locking](doc/terminal-behavior.md#keyboard-locking) and [local
  echo](doc/terminal-behavior.md#local-echo) requests, and [bracketed
  pastes](doc/input.md#bracketed-paste) that cannot be ended early.

## Build

Requires a C compiler, make, pkg-config, development libraries for Xlib, Xft,
XRender, Fontconfig and FreeType, and `tic` from ncurses.

```sh
make
make terminfo
TERMINFO="$PWD/.terminfo" ./phist
```

The build version comes from `git describe --tags --always --dirty`. Without
Git metadata it defaults to `unknown`; use `make VERSION=...` to override it.

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
