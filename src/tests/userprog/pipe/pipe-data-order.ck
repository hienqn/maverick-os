# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-data-order) begin
(pipe-data-order) pipe()
(pipe-data-order) byte 0 is '0'
(pipe-data-order) byte 1 is '1'
(pipe-data-order) byte 2 is '2'
(pipe-data-order) byte 3 is '3'
(pipe-data-order) byte 4 is '4'
(pipe-data-order) byte 5 is '5'
(pipe-data-order) byte 6 is '6'
(pipe-data-order) byte 7 is '7'
(pipe-data-order) byte 8 is '8'
(pipe-data-order) byte 9 is '9'
(pipe-data-order) FIFO order verified
(pipe-data-order) end
pipe-data-order: exit(0)
EOF
pass;
