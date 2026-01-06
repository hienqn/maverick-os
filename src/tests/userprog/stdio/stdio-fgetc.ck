# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-fgetc) begin
(stdio-fgetc) create testfile.txt
(stdio-fgetc) fopen succeeds
(stdio-fgetc) fgetc returns 'A'
(stdio-fgetc) fgetc returns 'B'
(stdio-fgetc) fgetc returns 'C'
(stdio-fgetc) fgetc returns EOF at end
(stdio-fgetc) getc returns 'A'
(stdio-fgetc) fgetc tests passed
(stdio-fgetc) end
stdio-fgetc: exit(0)
EOF
pass;
