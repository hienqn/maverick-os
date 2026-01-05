# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-fk-rev) begin
(pipe-fk-rev) pipe()
(pipe-fk-rev) end
pipe-fk-rev: exit(0)
(pipe-fk-rev) parent received: Hello from child
(pipe-fk-rev) end
pipe-fk-rev: exit(0)
EOF
pass;
