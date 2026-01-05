# -*- perl -*-
use strict;
use warnings;
use tests::tests;

# The child writes after a delay, so the order should be:
# parent about to read -> child wrote -> parent read
my (@output) = read_text_file ("$test.output");
common_checks ("run", @output);

my $parent_about_to_read = 0;
my $child_wrote = 0;
my $parent_read = 0;

for my $line (@output) {
  if ($line =~ /parent about to read/) {
    $parent_about_to_read = 1;
    fail "child wrote before parent started reading\n" if $child_wrote;
  }
  if ($line =~ /child wrote data/) {
    $child_wrote = 1;
    fail "parent about to read not seen\n" unless $parent_about_to_read;
  }
  if ($line =~ /parent read: X/) {
    $parent_read = 1;
    fail "child did not write before parent read\n" unless $child_wrote;
  }
}

fail "parent did not read\n" unless $parent_read;
pass;
