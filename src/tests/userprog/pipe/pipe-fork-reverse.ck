# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-fork-reverse) begin
(pipe-fork-reverse) pipe()
(pipe-fork-reverse) end
pipe-fork-reverse: exit(0)
(pipe-fork-reverse) parent received: Hello from child
(pipe-fork-reverse) end
pipe-fork-reverse: exit(0)
EOF
pass;
