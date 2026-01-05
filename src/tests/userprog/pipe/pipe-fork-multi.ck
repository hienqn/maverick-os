# -*- perl -*-
use strict;
use warnings;
use tests::tests;

# The order of children reading may vary, so we use a more flexible check.
# Each child should read one of 'A', 'B', or 'C'.
my (@output) = read_text_file ("$test.output");
common_checks ("run", @output);

my $found_begin = 0;
my $found_end = 0;
my $found_all_finished = 0;
my @children_read;

for my $line (@output) {
  $found_begin = 1 if $line =~ /\(pipe-fork-multi\) begin/;
  $found_end++ if $line =~ /\(pipe-fork-multi\) end/;
  $found_all_finished = 1 if $line =~ /\(pipe-fork-multi\) all children finished/;
  if ($line =~ /\(pipe-fork-multi\) child \d+ read: ([ABC])/) {
    push @children_read, $1;
  }
}

fail "Test did not begin properly\n" unless $found_begin;
fail "Test did not end properly\n" unless $found_end >= 1;
fail "Not all children finished\n" unless $found_all_finished;
fail "Expected 3 children to read, got " . scalar(@children_read) . "\n"
  unless scalar(@children_read) == 3;

# Check that A, B, C were all read (order may vary)
my %seen;
$seen{$_}++ for @children_read;
fail "Children did not read A, B, C\n"
  unless $seen{'A'} && $seen{'B'} && $seen{'C'};

pass;
