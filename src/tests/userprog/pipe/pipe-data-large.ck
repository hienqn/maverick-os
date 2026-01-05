# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-data-large) begin
(pipe-data-large) pipe()
(pipe-data-large) child wrote 8192 bytes
(pipe-data-large) end
pipe-data-large: exit(8192)
(pipe-data-large) parent read 8192 bytes
(pipe-data-large) data integrity verified
(pipe-data-large) end
pipe-data-large: exit(0)
EOF
pass;
