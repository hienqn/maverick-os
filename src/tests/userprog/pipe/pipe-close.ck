# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-close) begin
(pipe-close) pipe()
(pipe-close) closed read end
(pipe-close) closed write end
(pipe-close) end
pipe-close: exit(0)
EOF
pass;
