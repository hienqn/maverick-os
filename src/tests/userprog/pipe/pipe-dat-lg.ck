# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-dat-lg) begin
(pipe-dat-lg) pipe()
(pipe-dat-lg) child wrote 8192 bytes
(pipe-dat-lg) end
pipe-dat-lg: exit(8192)
(pipe-dat-lg) parent read 8192 bytes
(pipe-dat-lg) data integrity verified
(pipe-dat-lg) end
pipe-dat-lg: exit(0)
EOF
pass;
