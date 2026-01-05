# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-fprintf) begin
(stdio-fprintf) create testfile.txt
(stdio-fprintf) fopen succeeds
(stdio-fprintf) fprintf returns char count
(stdio-fprintf) integer format correct
(stdio-fprintf) string format correct
(stdio-fprintf) hex format correct
(stdio-fprintf) char format correct
(stdio-fprintf) width/padding correct
(stdio-fprintf) fprintf tests passed
(stdio-fprintf) end
EOF
