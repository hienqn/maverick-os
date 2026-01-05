# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(stdio-fopen) begin
(stdio-fopen) create testfile.txt
(stdio-fopen) fopen("testfile.txt", "w") succeeds
(stdio-fopen) fclose succeeds
(stdio-fopen) fopen("testfile.txt", "r") succeeds
(stdio-fopen) fclose succeeds
(stdio-fopen) fopen nonexistent file returns NULL
(stdio-fopen) fopen for fileno test
(stdio-fopen) fileno returns valid fd
(stdio-fopen) fopen tests passed
(stdio-fopen) end
EOF
