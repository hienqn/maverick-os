# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_archive ({'mydir' => {'file' => ["\0" x 512]}});
pass;
