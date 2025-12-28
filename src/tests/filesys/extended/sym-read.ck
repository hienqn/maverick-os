# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(sym-read) begin
(sym-read) create "target-file"
(sym-read) open "target-file"
(sym-read) write "target-file"
(sym-read) close "target-file"
(sym-read) symlink "target-file" -> "symlink"
(sym-read) open "symlink"
(sym-read) read "symlink"
(sym-read) verify contents match
(sym-read) close "symlink"
(sym-read) end
EOF
pass;
