# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-fwrite) begin
(stdio-fwrite) create testfile.txt
(stdio-fwrite) fopen for write
(stdio-fwrite) fwrite returns correct count
(stdio-fwrite) written data matches
(stdio-fwrite) fwrite 5 ints succeeds
(stdio-fwrite) fread 5 ints succeeds
(stdio-fwrite) int data matches
(stdio-fwrite) fwrite tests passed
(stdio-fwrite) end
stdio-fwrite: exit(0)
EOF
pass;
