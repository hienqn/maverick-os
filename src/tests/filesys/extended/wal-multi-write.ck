# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(wal-multi-write) begin
(wal-multi-write) create "multiwrite"
(wal-multi-write) open "multiwrite"
(wal-multi-write) write chunk 0 (pattern 'A')
(wal-multi-write) write chunk 1 (pattern 'B')
(wal-multi-write) write chunk 2 (pattern 'C')
(wal-multi-write) write chunk 3 (pattern 'D')
(wal-multi-write) All 4 writes completed
(wal-multi-write) reopen "multiwrite"
(wal-multi-write) read all data
(wal-multi-write) verify chunk 0 has pattern 'A'
(wal-multi-write) verify chunk 1 has pattern 'B'
(wal-multi-write) verify chunk 2 has pattern 'C'
(wal-multi-write) verify chunk 3 has pattern 'D'
(wal-multi-write) All chunks verified correctly
(wal-multi-write) end
EOF
pass;
