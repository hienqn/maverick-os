# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-fputc) begin
(stdio-fputc) create testfile.txt
(stdio-fputc) fopen succeeds
(stdio-fputc) fputc returns written char
(stdio-fputc) fputc returns 'Y'
(stdio-fputc) fputc returns 'Z'
(stdio-fputc) first char is 'X'
(stdio-fputc) second char is 'Y'
(stdio-fputc) third char is 'Z'
(stdio-fputc) putc returns written char
(stdio-fputc) fputc tests passed
(stdio-fputc) end
EOF
