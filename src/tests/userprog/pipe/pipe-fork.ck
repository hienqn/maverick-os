# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-fork) begin
(pipe-fork) pipe()
(pipe-fork) child received: Hello from parent
(pipe-fork) end
pipe-fork: exit(18)
(pipe-fork) child returned 18
(pipe-fork) end
pipe-fork: exit(0)
EOF
pass;
