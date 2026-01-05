# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-fk-chn) begin
(pipe-fk-chn) pipe1()
(pipe-fk-chn) pipe2()
(pipe-fk-chn) end
pipe-fk-chn: exit(0)
(pipe-fk-chn) end
pipe-fk-chn: exit(0)
(pipe-fk-chn) pipeline result: data
(pipe-fk-chn) pipeline transformation correct
(pipe-fk-chn) end
pipe-fk-chn: exit(0)
EOF
pass;
