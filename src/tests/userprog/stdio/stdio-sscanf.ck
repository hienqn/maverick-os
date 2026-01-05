# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-sscanf) begin
(stdio-sscanf) sscanf %d returns 1
(stdio-sscanf) parsed 42
(stdio-sscanf) sscanf negative returns 1
(stdio-sscanf) parsed -123
(stdio-sscanf) sscanf %x returns 1
(stdio-sscanf) parsed 0xff as 255
(stdio-sscanf) sscanf %s returns 1
(stdio-sscanf) parsed 'hello'
(stdio-sscanf) sscanf three ints returns 3
(stdio-sscanf) parsed 10, 20, 30
(stdio-sscanf) sscanf with literal returns 1
(stdio-sscanf) parsed x=5
(stdio-sscanf) sscanf no match returns 0
(stdio-sscanf) sscanf tests passed
(stdio-sscanf) end
EOF
