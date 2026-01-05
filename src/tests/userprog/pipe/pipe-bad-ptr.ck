# -*- perl -*-
use strict;
use warnings;
use tests::tests;

my (@output) = read_text_file ("$test.output");
common_checks ("run", @output);

# The test should either:
# 1. Exit with -1 (process killed due to bad pointer)
# 2. Return an error from pipe()

my $process_killed = 0;
my $pipe_returned_error = 0;

for my $line (@output) {
  $process_killed = 1 if $line =~ /exit\(-1\)/;
  $pipe_returned_error = 1 if $line =~ /pipe with bad pointer returned -?\d+/;
}

fail "pipe with bad pointer should have failed or killed process\n"
  unless $process_killed || $pipe_returned_error;
pass;
