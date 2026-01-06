# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-fgets) begin
(stdio-fgets) create testfile.txt
(stdio-fgets) fopen succeeds
(stdio-fgets) fgets returns buffer pointer
(stdio-fgets) first line is 'Line1\n'
(stdio-fgets) fgets second line succeeds
(stdio-fgets) second line is 'Line2\n'
(stdio-fgets) fgets third line succeeds
(stdio-fgets) third line is 'Line3'
(stdio-fgets) fgets at EOF returns NULL
(stdio-fgets) fgets with small buffer succeeds
(stdio-fgets) small buffer gets 'Lin'
(stdio-fgets) fgets tests passed
(stdio-fgets) end
stdio-fgets: exit(0)
EOF
pass;
