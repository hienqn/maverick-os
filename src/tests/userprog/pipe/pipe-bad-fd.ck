# -*- perl -*-
use strict;
use warnings;
use tests::tests;
our ($test);

my (@output) = read_text_file ("$test.output");
common_checks ("run", @output);

my $read_result = 0;
my $write_result = 0;

for my $line (@output) {
  $read_result = 1 if $line =~ /read on closed fd returned/;
  $write_result = 1 if $line =~ /write on closed fd returned/;
}

fail "read on closed fd not tested\n" unless $read_result;
fail "write on closed fd not tested\n" unless $write_result;
pass;
