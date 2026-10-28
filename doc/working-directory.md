# Working-directory reports

Shells can report their current directory with OSC 7. phist publishes the
latest valid report as `_PHIST_CWD_URI`, a `UTF8_STRING` property on its X11
window. Window-manager scripts and other X11 clients can read it without
phist needing a built-in window-launching action.

## Reading the directory

phist exports its window ID as `WINDOWID`. From a shell inside that window:

```sh
xprop -id "$WINDOWID" _PHIST_CWD_URI
xprop -spy -id "$WINDOWID" _PHIST_CWD_URI
```

The first command reads the property; the second watches for changes. A
report might appear as:

```text
_PHIST_CWD_URI(UTF8_STRING) = "file://myhost/home/user/my%20project"
```

The property is absent until the first valid report. It contains the entire
URI, including its hostname and percent encoding. A consumer that launches
`phist -d path` should check that the host denotes the local machine and
percent-decode the path. Remote directories, such as those reported through
SSH, are published too; they do not imply a corresponding local directory.
Treat the value as application-supplied data when passing it to commands.

## Reporting from a shell

The sequence is `ESC ] 7 ; file://hostname/absolute/path ST`. BEL can also
terminate it. This example publishes a fixed URI:

```sh
printf '\033]7;file://localhost/tmp\033\\'
```

A prompt hook should report after directory changes, percent-encoding the
hostname and path as needed. phist keeps the encoded URI unchanged: spaces
must be `%20`, and non-ASCII filename bytes must be percent-encoded too.
Semicolons in the path are preserved. Both empty hosts (`file:///tmp`) and
bracketed IPv6 hosts (`file://[::1]/tmp`) are accepted.

Reports are limited to 16,384 URI bytes. phist rejects malformed escapes,
literal whitespace and non-ASCII bytes, encoded ASCII control bytes, userinfo,
ports, queries and fragments. An invalid report leaves the previous value
unchanged. Validation checks URI syntax; phist performs no hostname lookup,
filesystem access or directory change in response to OSC 7.

An empty report (`printf '\033]7;\033\\'`) removes the property. A terminal
reset (`RIS`, `ESC c`) also clears it. Screen clearing, alternate-screen
switches and resizing preserve it. Repeated identical reports do not write
the property again; changes are batched once per draw cycle, including
while the window is hidden. Watchers receive the latest value, rather than
every intermediate report in a burst.

OSC 7 does not change the running shell's directory or populate a report
from the initial `-d` option. The shell or application must emit the report.

## Checks

```sh
LC_ALL=C.UTF-8 sh tests/cwd.sh
LC_ALL=C.UTF-8 sh tests/sequences.sh
```

The tests cover URI validation and limits, both terminators, split input,
semicolons, hostname/encoding preservation, X11 property type and updates,
batching while hidden, duplicate reports, clearing, reset and screen changes.
