# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-data-chunk) begin
(pipe-data-chunk) pipe()
(pipe-data-chunk) read 1: 6 bytes = AAAABB
(pipe-data-chunk) read 2: 6 bytes = BBCCCC
(pipe-data-chunk) read 3: 0 (EOF)
(pipe-data-chunk) end
pipe-data-chunk: exit(0)
EOF
pass;
