# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-ftell) begin
(stdio-ftell) create testfile.txt
(stdio-ftell) fopen succeeds
(stdio-ftell) ftell at start is 0
(stdio-ftell) ftell after 1 read is 1
(stdio-ftell) ftell after 4 reads is 4
(stdio-ftell) ftell at end is 10
(stdio-ftell) ftell at write start is 0
(stdio-ftell) ftell after 3 writes is 3
(stdio-ftell) ftell tests passed
(stdio-ftell) end
stdio-ftell: exit(0)
EOF
pass;
