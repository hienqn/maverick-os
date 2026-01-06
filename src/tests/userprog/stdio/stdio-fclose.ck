# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-fclose) begin
(stdio-fclose) create testfile.txt
(stdio-fclose) fopen succeeds
(stdio-fclose) fclose returns 0 on success
(stdio-fclose) fopen for flush test
(stdio-fclose) fputc writes 'A'
(stdio-fclose) fclose flushes and closes
(stdio-fclose) reopen file
(stdio-fclose) data was flushed before close
(stdio-fclose) fclose tests passed
(stdio-fclose) end
stdio-fclose: exit(0)
EOF
pass;
