# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(fork-cow) begin
(fork-cow) Parent initialized buffer with 'A's
(fork-cow) Child verified initial buffer is 'A's
(fork-cow) Child wrote 'B's to buffer
(fork-cow) Child verified buffer is 'B's
(fork-cow) end
fork-cow: exit(0)
(fork-cow) Parent wrote 'C's to buffer
(fork-cow) Parent verified buffer is still 'C's
(fork-cow) end
fork-cow: exit(0)
EOF
pass;
