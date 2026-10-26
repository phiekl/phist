# Keyboard input and terminal replies

How user input and automatic protocol replies reach the child process.

## Input and replies

Keyboard input, mapped keys, configured send actions and paste use
`ttyinput()`. Only nonempty user input returns a scrolled view to the live
screen; a selection remains attached to its text. Protocol replies use a
separate path, so cursor-position queries leave the history view in place.
OSC color replies are not locally echoed.

Run `LC_ALL=C.UTF-8 sh tests/replies.sh` for reply and empty/nonempty-input
regressions. History selection behavior is also covered by
`tests/history.sh`.

## Delete key

Delete and keypad Delete send the sequence advertised by the default
terminfo entries in either normal or application keypad mode:

| Key | Bytes |
| --- | --- |
| Delete | `ESC [ 3 ~` |
| Ctrl+Delete | `ESC [ 3 ; 5 ~` |
| Shift+Delete | `ESC [ 3 ; 2 ~` |

Previously, non-application mode sent `ESC [ P`, which shell prompts
expecting the advertised sequence could ignore. Existing `config.h` files
retain their old mappings; update them or regenerate from `config.def.h` and
rebuild.

The `phist-bs` and `phist-bs-256color` entries instead advertise DEL
(`\177`) for Delete and Backspace as `^H`. They match only a `config.h`
whose key mappings were changed to send those bytes; with the default
mappings, use `phist-256color`.

## Bracketed paste

When bracketed paste is enabled, pasted ESC bytes are removed before text is
wrapped with `ESC [ 200 ~` and `ESC [ 201 ~`. An embedded end marker
therefore cannot end the bracket early and turn the rest of a paste into
typed commands. Plain paste is sent unchanged.

For a check that does not execute the pasted text, copy a crafted string,
then paste it into `cat -v` with Ctrl+Shift+V:

```sh
printf 'harmless\033[201~id\n' | xclip -selection clipboard
printf '\033[?2004h'; cat -v
```

The interior text appears as `harmless[201~id`, with only phist's own ending
`^[[201~` closing the paste. Press Ctrl+C and run `printf '\033[?2004l'`
afterwards. Run `LC_ALL=C.UTF-8 sh tests/mouse.sh` for paste-path
regressions.
