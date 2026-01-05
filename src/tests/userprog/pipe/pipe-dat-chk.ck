# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-dat-chk) begin
(pipe-dat-chk) pipe()
(pipe-dat-chk) read 1: 6 bytes = AAAABB
(pipe-dat-chk) read 2: 6 bytes = BBCCCC
(pipe-dat-chk) read 3: 0 (EOF)
(pipe-dat-chk) end
pipe-dat-chk: exit(0)
EOF
pass;
