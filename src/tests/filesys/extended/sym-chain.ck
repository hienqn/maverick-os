# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(sym-chain) begin
(sym-chain) create "target-file"
(sym-chain) open "target-file"
(sym-chain) write "target-file"
(sym-chain) close "target-file"
(sym-chain) symlink "target-file" -> "link1"
(sym-chain) symlink "link1" -> "link2"
(sym-chain) symlink "link2" -> "link3"
(sym-chain) open "link3"
(sym-chain) read "link3"
(sym-chain) verify contents through chain
(sym-chain) close "link3"
(sym-chain) readlink "link3" length correct
(sym-chain) readlink "link3" returns "link2"
(sym-chain) end
EOF
pass;
