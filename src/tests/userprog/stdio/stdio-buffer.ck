# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-buffer) begin
(stdio-buffer) create testfile.txt
(stdio-buffer) fopen succeeds
(stdio-buffer) data written after close
(stdio-buffer) data visible after fflush
(stdio-buffer) buffer tests passed
(stdio-buffer) end
stdio-buffer: exit(0)
EOF
pass;
