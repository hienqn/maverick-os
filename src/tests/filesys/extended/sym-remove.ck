# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(sym-remove) begin
(sym-remove) create "target-file"
(sym-remove) open "target-file"
(sym-remove) write "target-file"
(sym-remove) close "target-file"
(sym-remove) symlink "target-file" -> "symlink"
(sym-remove) remove "symlink"
(sym-remove) open "symlink" (must fail)
(sym-remove) open "target-file" (still exists)
(sym-remove) read "target-file"
(sym-remove) verify contents intact
(sym-remove) close "target-file"
(sym-remove) end
EOF
pass;
