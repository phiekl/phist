# Keyboard input and terminal replies

How user input and automatic protocol replies reach the child process.

## Input and replies

Keyboard input, mapped keys, configured send actions and paste use
`ttyinput()`. Only nonempty user input returns a scrolled view to the live
screen; a selection remains attached to its text. Protocol replies use a
separate path, so cursor-position queries and automatic focus reports leave
the history view in place. OSC color replies are not locally echoed.

Run `LC_ALL=C.UTF-8 sh tests/replies.sh` for reply, focus-report and
empty/nonempty-input regressions. History selection behavior is also covered
by `tests/history.sh`.
