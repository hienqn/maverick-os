# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(wal-symlink) begin
(wal-symlink) === Phase 1: Create target file ===
(wal-symlink) create "target.txt"
(wal-symlink) open "target.txt"
(wal-symlink) write to "target.txt"
(wal-symlink) Created target.txt with 'T' pattern
(wal-symlink) === Phase 2: Create and use symlink ===
(wal-symlink) create symlink "link.txt" -> "target.txt"
(wal-symlink) Created symlink link.txt -> target.txt
(wal-symlink) open "link.txt" (symlink)
(wal-symlink) read through symlink
(wal-symlink) verify data through symlink is 'T'
(wal-symlink) Read through symlink: correct data 'T'
(wal-symlink) === Phase 3: Write through symlink ===
(wal-symlink) open "link.txt" for writing
(wal-symlink) write through symlink
(wal-symlink) Wrote 'W' pattern through symlink
(wal-symlink) open "target.txt" directly
(wal-symlink) read from "target.txt"
(wal-symlink) verify target has 'W' (written through link)
(wal-symlink) Target file now contains 'W': write through symlink worked
(wal-symlink) === Phase 4: Remove symlink, keep target ===
(wal-symlink) remove symlink "link.txt"
(wal-symlink) Removed symlink
(wal-symlink) Verified symlink is gone
(wal-symlink) open "target.txt" after symlink removal
(wal-symlink) read from "target.txt"
(wal-symlink) verify target still has 'W'
(wal-symlink) Target file still exists with 'W' data
(wal-symlink) === Phase 5: Directory symlink ===
(wal-symlink) create directory "mydir"
(wal-symlink) create "mydir/file.txt"
(wal-symlink) open "mydir/file.txt"
(wal-symlink) write to "mydir/file.txt"
(wal-symlink) Created mydir/file.txt with 'D' pattern
(wal-symlink) create symlink "dirlink" -> "mydir"
(wal-symlink) Created symlink dirlink -> mydir
(wal-symlink) open "dirlink/file.txt" (through dir symlink)
(wal-symlink) read through directory symlink
(wal-symlink) verify data through dir symlink is 'D'
(wal-symlink) Read through directory symlink: correct data 'D'
(wal-symlink) === Cleanup ===
(wal-symlink) remove dir symlink
(wal-symlink) remove "mydir/file.txt"
(wal-symlink) remove directory "mydir"
(wal-symlink) remove "target.txt"
(wal-symlink) Cleanup complete
(wal-symlink) Symlink with WAL test: PASSED
(wal-symlink) end
EOF
pass;
