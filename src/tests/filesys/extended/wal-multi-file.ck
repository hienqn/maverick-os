# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(wal-multi-file) begin
(wal-multi-file) create "file_a"
(wal-multi-file) open "file_a"
(wal-multi-file) write 'X' pattern to "file_a"
(wal-multi-file) create "file_b"
(wal-multi-file) open "file_b"
(wal-multi-file) write 'Y' pattern to "file_b"
(wal-multi-file) create "file_c"
(wal-multi-file) open "file_c"
(wal-multi-file) write 'Z' pattern to "file_c"
(wal-multi-file) Created and wrote to 3 files
(wal-multi-file) reopen "file_a"
(wal-multi-file) read from "file_a"
(wal-multi-file) verify "file_a" contains only 'X'
(wal-multi-file) reopen "file_b"
(wal-multi-file) read from "file_b"
(wal-multi-file) verify "file_b" contains only 'Y'
(wal-multi-file) reopen "file_c"
(wal-multi-file) read from "file_c"
(wal-multi-file) verify "file_c" contains only 'Z'
(wal-multi-file) All files verified - data isolation confirmed
(wal-multi-file) end
EOF
pass;
