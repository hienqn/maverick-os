# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-ungetc) begin
(stdio-ungetc) create testfile.txt
(stdio-ungetc) fopen succeeds
(stdio-ungetc) first fgetc returns 'A'
(stdio-ungetc) ungetc returns pushed char
(stdio-ungetc) fgetc after ungetc returns 'A'
(stdio-ungetc) ungetc 'X' succeeds
(stdio-ungetc) fgetc returns pushed 'X'
(stdio-ungetc) EOF is set
(stdio-ungetc) ungetc after EOF succeeds
(stdio-ungetc) ungetc clears EOF flag
(stdio-ungetc) fgetc returns pushed 'Z'
(stdio-ungetc) ungetc tests passed
(stdio-ungetc) end
EOF
