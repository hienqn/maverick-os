# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-stdin) begin
(stdio-stdin) stdin is not NULL
(stdio-stdin) stdin fd is 0
(stdio-stdin) stdin tests passed
(stdio-stdin) end
EOF
