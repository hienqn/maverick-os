# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(sym-cycle) begin
(sym-cycle) symlink "link2" -> "link1"
(sym-cycle) symlink "link1" -> "link2"
(sym-cycle) open "link1" (must fail - cycle)
(sym-cycle) open "link2" (must fail - cycle)
(sym-cycle) symlink "self" -> "self"
(sym-cycle) open "self" (must fail - self-reference)
(sym-cycle) readlink "link1" length correct
(sym-cycle) readlink "link1" returns "link2"
(sym-cycle) end
EOF
pass;
