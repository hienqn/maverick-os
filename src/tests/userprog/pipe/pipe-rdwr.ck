# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-rdwr) begin
(pipe-rdwr) pipe()
(pipe-rdwr) write 13 bytes
(pipe-rdwr) read 13 bytes
(pipe-rdwr) data matches
(pipe-rdwr) end
pipe-rdwr: exit(0)
EOF
pass;
