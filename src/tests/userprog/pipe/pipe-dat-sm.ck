# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-dat-sm) begin
(pipe-dat-sm) pipe()
(pipe-dat-sm) wrote 20 bytes
(pipe-dat-sm) read 20 bytes
(pipe-dat-sm) data integrity verified
(pipe-dat-sm) end
pipe-dat-sm: exit(0)
EOF
pass;
