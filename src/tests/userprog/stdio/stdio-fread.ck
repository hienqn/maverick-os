# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-fread) begin
(stdio-fread) create testfile.txt
(stdio-fread) fopen succeeds
(stdio-fread) fread returns correct count
(stdio-fread) fread data matches
(stdio-fread) fread at EOF returns 0
(stdio-fread) feof returns true after EOF
(stdio-fread) fread with size=4 returns 3 items
(stdio-fread) fread tests passed
(stdio-fread) end
stdio-fread: exit(0)
EOF
pass;
