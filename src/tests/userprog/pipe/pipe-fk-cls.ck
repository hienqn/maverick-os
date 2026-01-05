# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-fk-cls) begin
(pipe-fk-cls) pipe()
(pipe-fk-cls) child read returned 0 (EOF expected: 0)
(pipe-fk-cls) end
pipe-fk-cls: exit(0)
(pipe-fk-cls) child got EOF correctly
(pipe-fk-cls) end
pipe-fk-cls: exit(0)
EOF
pass;
