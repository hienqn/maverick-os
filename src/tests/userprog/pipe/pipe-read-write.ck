# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-read-write) begin
(pipe-read-write) pipe()
(pipe-read-write) write 13 bytes
(pipe-read-write) read 13 bytes
(pipe-read-write) data matches
(pipe-read-write) end
pipe-read-write: exit(0)
EOF
pass;
