# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-fseek) begin
(stdio-fseek) create testfile.txt
(stdio-fseek) fopen succeeds
(stdio-fseek) fseek SEEK_SET returns 0
(stdio-fseek) after seek to 5, read 'F'
(stdio-fseek) fseek to 0 succeeds
(stdio-fseek) after seek to 0, read 'A'
(stdio-fseek) fseek SEEK_CUR +3 succeeds
(stdio-fseek) after skip 3, read 'E'
(stdio-fseek) fseek SEEK_END -2 succeeds
(stdio-fseek) 2 from end is 'I'
(stdio-fseek) rewind goes to beginning
(stdio-fseek) fseek tests passed
(stdio-fseek) end
EOF
