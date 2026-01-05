# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-eof-pt) begin
(pipe-eof-pt) pipe()
(pipe-eof-pt) read 1: 3 bytes
(pipe-eof-pt) read 2: 3 bytes
(pipe-eof-pt) read 3: 2 bytes (remaining)
(pipe-eof-pt) read 4: 0 (EOF)
(pipe-eof-pt) end
pipe-eof-pt: exit(0)
EOF
pass;
