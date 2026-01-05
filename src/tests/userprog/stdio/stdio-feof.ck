# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-feof) begin
(stdio-feof) create testfile.txt
(stdio-feof) fopen succeeds
(stdio-feof) feof is false at start
(stdio-feof) feof is false after first read
(stdio-feof) feof is false after second read
(stdio-feof) fgetc returns EOF
(stdio-feof) feof is true after EOF
(stdio-feof) feof is false after clearerr
(stdio-feof) feof tests passed
(stdio-feof) end
EOF
