# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-simple) begin
(pipe-simple) pipe() returns 0 on success
(pipe-simple) read fd is valid (>= 2)
(pipe-simple) write fd is valid (>= 2)
(pipe-simple) read and write fds are different
(pipe-simple) end
pipe-simple: exit(0)
EOF
pass;
