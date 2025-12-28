# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(sym-dangling) begin
(sym-dangling) symlink "nonexistent" -> "dangling"
(sym-dangling) readlink "dangling" length correct
(sym-dangling) readlink "dangling" returns "nonexistent"
(sym-dangling) open "dangling" (must fail)
(sym-dangling) create "nonexistent"
(sym-dangling) open "nonexistent"
(sym-dangling) write "nonexistent"
(sym-dangling) close "nonexistent"
(sym-dangling) open "dangling" (now works)
(sym-dangling) read "dangling"
(sym-dangling) verify contents through symlink
(sym-dangling) close "dangling"
(sym-dangling) remove "nonexistent"
(sym-dangling) open "dangling" (dangling again)
(sym-dangling) end
EOF
pass;
