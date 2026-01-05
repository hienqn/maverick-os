# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-dbl-cls) begin
(pipe-dbl-cls) pipe()
(pipe-dbl-cls) first close of read end
(pipe-dbl-cls) second close of read end (should be no-op or error)
(pipe-dbl-cls) closed write end
(pipe-dbl-cls) end
pipe-dbl-cls: exit(0)
EOF
pass;
