# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(wal-remove) begin
(wal-remove) === Phase 1: Create files ===
(wal-remove) create "file_a"
(wal-remove) open "file_a"
(wal-remove) write to "file_a"
(wal-remove) Created file_a with 'A' pattern
(wal-remove) create "file_b"
(wal-remove) open "file_b"
(wal-remove) write to "file_b"
(wal-remove) Created file_b with 'B' pattern
(wal-remove) create "file_c"
(wal-remove) open "file_c"
(wal-remove) write to "file_c"
(wal-remove) Created file_c with 'C' pattern
(wal-remove) === Phase 2: Remove file_b ===
(wal-remove) remove "file_b"
(wal-remove) Removed file_b
(wal-remove) Verified file_b cannot be opened
(wal-remove) === Phase 3: Verify remaining files ===
(wal-remove) open "file_a" after removal of file_b
(wal-remove) read from "file_a"
(wal-remove) verify "file_a" contains 'A'
(wal-remove) file_a verified: still contains 'A'
(wal-remove) open "file_c" after removal of file_b
(wal-remove) read from "file_c"
(wal-remove) verify "file_c" contains 'C'
(wal-remove) file_c verified: still contains 'C'
(wal-remove) === Phase 4: Reuse freed space ===
(wal-remove) recreate "file_b"
(wal-remove) open new "file_b"
(wal-remove) write to new "file_b"
(wal-remove) Created new file_b with 'X' pattern
(wal-remove) reopen new "file_b"
(wal-remove) read from new "file_b"
(wal-remove) verify new "file_b" contains 'X'
(wal-remove) New file_b verified: contains 'X'
(wal-remove) === Phase 5: Remove remaining files ===
(wal-remove) remove "file_a"
(wal-remove) remove new "file_b"
(wal-remove) remove "file_c"
(wal-remove) All files removed
(wal-remove) Verified all files removed successfully
(wal-remove) File removal with WAL test: PASSED
(wal-remove) end
EOF
pass;
