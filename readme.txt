	Basic implementation of UNIX shell. This dummy implementation lacks any
grammar of shell language, variables processing and etc. It supports:
1) Job control.
2) Some builtins.
3) Pipelines(but only with foreground tasks).
4) File lookup in '$PATH' variable.
5) List previous commands.

In progress:
1) Autocompletion.
2) ANSI control codes support.
3) Cursor handling.

Neares future TODO:
Technicals:
1) Test system.
2) Colored output.
3) Fix dumb pipe creation system.
Language:
1) Grammar parsing...

!To perform tests: set owner of run_pts root and set setuid bit!
