# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(priority-sema-effective) begin
(priority-sema-effective) Thread A acquired lock (base priority 30, should have eff_priority 50 after donation)
(priority-sema-effective) Calling sema_up - should wake Thread A (eff_priority 50) not Thread B (eff_priority 40)
(priority-sema-effective) Thread A woke up from semaphore (this is CORRECT - eff_priority 50 > 40)
(priority-sema-effective) Thread C acquired lock (priority 50)
(priority-sema-effective) Thread B woke up from semaphore (this is WRONG - base priority 40 < eff_priority 50)
(priority-sema-effective) Test completed.
(priority-sema-effective) end
EOF
pass;

