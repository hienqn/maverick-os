# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(sym-dir) begin
(sym-dir) mkdir "mydir"
(sym-dir) create "mydir/file"
(sym-dir) symlink "mydir" -> "dirlink"
(sym-dir) open "dirlink/file"
(sym-dir) close "dirlink/file"
(sym-dir) chdir "dirlink"
(sym-dir) open "file"
(sym-dir) close "file"
(sym-dir) end
EOF
pass;
