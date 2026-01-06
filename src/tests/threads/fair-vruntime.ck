# -*- perl -*-
use strict;
use warnings;
use tests::tests;

# This test verifies that CFS/EEVDF partial tick accounting works.
# The CPU-bound thread should have higher vruntime than the I/O-bound thread.
# We check for the PASS message which indicates the test logic passed.

our ($test);
my (@output) = read_text_file ("$test.output");

# Use standard checks first
common_checks ("kernel run", @output);

# Get the core output (between "Executing" and "Execution complete")
@output = get_core_output ("kernel run", @output);

# Now look for our test-specific output
my $found_pass = 0;
my $found_fail = 0;

for my $line (@output) {
  if ($line =~ /PASS:.*vruntime is much higher/) {
    $found_pass = 1;
  }
  if ($line =~ /MARGINAL:.*vruntime is higher/) {
    # Marginal is acceptable
    $found_pass = 1;
  }
  if ($line =~ /FAIL:.*vruntime/) {
    $found_fail = 1;
  }
}

if ($found_fail) {
  fail "CFS/EEVDF partial accounting test failed - I/O thread has >= vruntime than CPU thread\n";
} elsif ($found_pass) {
  pass;
} else {
  fail "Could not determine test result from output\n";
}
