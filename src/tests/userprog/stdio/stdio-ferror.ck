# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-ferror) begin
(stdio-ferror) create testfile.txt
(stdio-ferror) fopen succeeds
(stdio-ferror) ferror is false initially
(stdio-ferror) ferror is false after read
(stdio-ferror) fopen for write succeeds
(stdio-ferror) ferror is false for write stream
(stdio-ferror) ferror is false after write
(stdio-ferror) ferror tests passed
(stdio-ferror) end
stdio-ferror: exit(0)
EOF
pass;
