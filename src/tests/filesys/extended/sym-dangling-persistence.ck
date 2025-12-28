# -*- perl -*-
use strict;
use warnings;
use tests::tests;
# After the test, the target file is removed so only the dangling symlink remains.
# The archive should be empty of regular files.
check_archive ({});
pass;
