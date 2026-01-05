# -*- perl -*-
use strict;
use warnings;
use tests::tests;

my (@output) = read_text_file ("$test.output");
common_checks ("run", @output);

my $child_wrote = 0;
my $parent_read = 0;
my $bytes_match = 0;

for my $line (@output) {
  if ($line =~ /child wrote (\d+) bytes/) {
    $child_wrote = $1;
  }
  if ($line =~ /parent read (\d+) bytes/) {
    $parent_read = $1;
  }
  if ($line =~ /bytes match/) {
    $bytes_match = 1;
  }
}

fail "child did not write\n" unless $child_wrote > 0;
fail "parent did not read\n" unless $parent_read > 0;
fail "bytes written and read don't match\n" unless $bytes_match;
pass;
