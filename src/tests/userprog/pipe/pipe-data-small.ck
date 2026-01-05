# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-data-small) begin
(pipe-data-small) pipe()
(pipe-data-small) wrote 20 bytes
(pipe-data-small) read 20 bytes
(pipe-data-small) data integrity verified
(pipe-data-small) end
pipe-data-small: exit(0)
EOF
pass;
