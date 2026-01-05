# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-stdout) begin
(stdio-stdout) stdout is not NULL
Hello from fprintf
(stdio-stdout) fprintf returns correct count
fputs output
(stdio-stdout) fputs to stdout succeeds
X
(stdio-stdout) stdout tests passed
(stdio-stdout) end
EOF
