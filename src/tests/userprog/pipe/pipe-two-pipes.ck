# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-two-pipes) begin
(pipe-two-pipes) first pipe()
(pipe-two-pipes) second pipe()
(pipe-two-pipes) all fds are unique
(pipe-two-pipes) pipe1 data correct
(pipe-two-pipes) pipe2 data correct
(pipe-two-pipes) end
pipe-two-pipes: exit(0)
EOF
pass;
