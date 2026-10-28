# Documentation

These guides describe the local changes to phist. Start with building and
configuration, then follow the topic that matches what you want to use or
change. Settings live in `config.def.h` and take effect through `config.h`.

- [Building and configuration](building.md): Build, regenerate settings and launch phist.
- [Scrollback and resizing](scrollback.md): Read and search earlier output, and rewrap it when the window changes size.
- [Keyboard input and terminal replies](input.md): How user input and automatic protocol replies reach the child process.
- [Appearance and text rendering](appearance.md): Configure the cursor and the way terminal text is drawn.
- [Clickable URLs](urls.md): Recognize, underline and copy URLs from the screen or history.
- [Text selection and clipboard](selection.md): Select words, paths and logical lines, and copy text to other programs.
- [Testing and fuzzing](testing.md): Run regression suites and stress the terminal core with reproducible input.
- [Terminal behavior and limits](terminal-behavior.md): How phist handles control sequences and bounds work triggered by application output.
- [Window titles](window-titles.md): Keep application titles suitable for task bars and window lists.
- [Working-directory reports](working-directory.md): Read shell-reported directory URIs through X11 properties.
- [Pasting text](pasting.md): Review multiline text, tabs and control characters before sending them to a program.
- [Paste confirmation spec](paste-spec.md): Exact transformations, dialog behavior and transfer rules.
