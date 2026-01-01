# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(wal-overwrite) begin
(wal-overwrite) create "overwrite"
(wal-overwrite) open "overwrite"
(wal-overwrite) write initial 'A' pattern
(wal-overwrite) read after first write
(wal-overwrite) verify 'A' pattern
(wal-overwrite) Initial write verified
(wal-overwrite) overwrite with 'B' pattern
(wal-overwrite) read after overwrite
(wal-overwrite) verify 'B' pattern
(wal-overwrite) First overwrite verified
(wal-overwrite) overwrite with 'C' pattern
(wal-overwrite) reopen "overwrite"
(wal-overwrite) read after reopen
(wal-overwrite) Final content is 'C' - overwrite ordering correct
(wal-overwrite) end
EOF
pass;
