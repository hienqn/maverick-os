# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-linebuf) begin
(stdio-linebuf) create testfile.txt
(stdio-linebuf) fopen succeeds
(stdio-linebuf) line buffer flushes on newline
(stdio-linebuf) first line preserved
(stdio-linebuf) second line correct
(stdio-linebuf) linebuf tests passed
(stdio-linebuf) end
EOF
