# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-eof-partial) begin
(pipe-eof-partial) pipe()
(pipe-eof-partial) read 1: 3 bytes
(pipe-eof-partial) read 2: 3 bytes
(pipe-eof-partial) read 3: 2 bytes (remaining)
(pipe-eof-partial) read 4: 0 (EOF)
(pipe-eof-partial) end
pipe-eof-partial: exit(0)
EOF
pass;
