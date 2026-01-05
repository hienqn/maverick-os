# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-fork-chain) begin
(pipe-fork-chain) pipe1()
(pipe-fork-chain) pipe2()
(pipe-fork-chain) end
pipe-fork-chain: exit(0)
(pipe-fork-chain) end
pipe-fork-chain: exit(0)
(pipe-fork-chain) pipeline result: data
(pipe-fork-chain) pipeline transformation correct
(pipe-fork-chain) end
pipe-fork-chain: exit(0)
EOF
pass;
