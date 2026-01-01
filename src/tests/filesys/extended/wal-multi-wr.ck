# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(wal-multi-wr) begin
(wal-multi-wr) create "multiwrite"
(wal-multi-wr) open "multiwrite"
(wal-multi-wr) write chunk 0 (pattern 'A')
(wal-multi-wr) write chunk 1 (pattern 'B')
(wal-multi-wr) write chunk 2 (pattern 'C')
(wal-multi-wr) write chunk 3 (pattern 'D')
(wal-multi-wr) All 4 writes completed
(wal-multi-wr) reopen "multiwrite"
(wal-multi-wr) read all data
(wal-multi-wr) verify chunk 0 has pattern 'A'
(wal-multi-wr) verify chunk 1 has pattern 'B'
(wal-multi-wr) verify chunk 2 has pattern 'C'
(wal-multi-wr) verify chunk 3 has pattern 'D'
(wal-multi-wr) All chunks verified correctly
(wal-multi-wr) end
EOF
pass;
