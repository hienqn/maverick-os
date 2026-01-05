# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-double-close) begin
(pipe-double-close) pipe()
(pipe-double-close) first close of read end
(pipe-double-close) second close of read end (should be no-op or error)
(pipe-double-close) closed write end
(pipe-double-close) end
pipe-double-close: exit(0)
EOF
pass;
