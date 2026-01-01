# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(wal-basic) begin
(wal-basic) create "testfile"
(wal-basic) File created successfully
(wal-basic) open "testfile"
(wal-basic) File opened, fd = 2
(wal-basic) write 512 bytes to "testfile"
(wal-basic) Write completed
(wal-basic) read 512 bytes from "testfile"
(wal-basic) verify data integrity
(wal-basic) Data verification passed
(wal-basic) File closed
(wal-basic) reopen "testfile"
(wal-basic) read after reopen
(wal-basic) verify data persists after reopen
(wal-basic) Data persistence verified
(wal-basic) end
EOF
pass;
