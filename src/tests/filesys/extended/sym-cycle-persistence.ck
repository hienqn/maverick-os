# -*- perl -*-
use strict;
use warnings;
use tests::tests;
# Cycle test only creates symlinks, no regular files.
check_archive ({});
pass;
