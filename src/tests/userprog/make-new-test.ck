# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
make-new-test: exit(161)
EOF
pass;
