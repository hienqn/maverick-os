# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(sym-path) begin
(sym-path) mkdir "a"
(sym-path) mkdir "a/b"
(sym-path) mkdir "a/b/c"
(sym-path) create "a/b/c/file"
(sym-path) open "a/b/c/file"
(sym-path) write "a/b/c/file"
(sym-path) close "a/b/c/file"
(sym-path) symlink "a/b" -> "shortcut"
(sym-path) open "shortcut/c/file"
(sym-path) read "shortcut/c/file"
(sym-path) verify contents via symlink path
(sym-path) close "shortcut/c/file"
(sym-path) symlink "c/file" -> "a/b/link-to-file"
(sym-path) open "a/b/link-to-file"
(sym-path) read "a/b/link-to-file"
(sym-path) close "a/b/link-to-file"
(sym-path) end
EOF
pass;
