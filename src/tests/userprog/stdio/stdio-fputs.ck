# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-fputs) begin
(stdio-fputs) create testfile.txt
(stdio-fputs) fopen succeeds
(stdio-fputs) fputs returns non-negative
(stdio-fputs) fputs second string succeeds
(stdio-fputs) file contains 'Hello World'
(stdio-fputs) first line correct
(stdio-fputs) second line correct
(stdio-fputs) fputs tests passed
(stdio-fputs) end
stdio-fputs: exit(0)
EOF
pass;
