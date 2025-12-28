# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(sym-write) begin
(sym-write) create "target-file"
(sym-write) symlink "target-file" -> "symlink"
(sym-write) open "symlink"
(sym-write) write "symlink"
(sym-write) close "symlink"
(sym-write) open "target-file"
(sym-write) read "target-file"
(sym-write) verify contents in "target-file"
(sym-write) close "target-file"
(sym-write) end
EOF
pass;
