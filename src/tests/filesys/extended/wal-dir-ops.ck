# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(wal-dir-ops) begin
(wal-dir-ops) mkdir "dir1"
(wal-dir-ops) mkdir "dir1/subdir"
(wal-dir-ops) Directory structure created
(wal-dir-ops) create "dir1/file1.txt"
(wal-dir-ops) open "dir1/file1.txt"
(wal-dir-ops) write to "dir1/file1.txt"
(wal-dir-ops) create "dir1/subdir/file2.txt"
(wal-dir-ops) open "dir1/subdir/file2.txt"
(wal-dir-ops) write to "dir1/subdir/file2.txt"
(wal-dir-ops) Files created in directory structure
(wal-dir-ops) reopen "dir1/file1.txt"
(wal-dir-ops) read "dir1/file1.txt"
(wal-dir-ops) verify "dir1/file1.txt" content
(wal-dir-ops) reopen "dir1/subdir/file2.txt"
(wal-dir-ops) read "dir1/subdir/file2.txt"
(wal-dir-ops) verify "dir1/subdir/file2.txt" content
(wal-dir-ops) Directory operations verified successfully
(wal-dir-ops) end
EOF
pass;
