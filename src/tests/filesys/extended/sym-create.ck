# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(sym-create) begin
(sym-create) create "target-file"
(sym-create) symlink "target-file" -> "symlink"
(sym-create) readlink "symlink" length correct
(sym-create) readlink "symlink" returns "target-file"
(sym-create) end
EOF
pass;
