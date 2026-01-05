# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-fork-close) begin
(pipe-fork-close) pipe()
(pipe-fork-close) child read returned 0 (EOF expected: 0)
(pipe-fork-close) end
pipe-fork-close: exit(0)
(pipe-fork-close) child got EOF correctly
(pipe-fork-close) end
pipe-fork-close: exit(0)
EOF
pass;
