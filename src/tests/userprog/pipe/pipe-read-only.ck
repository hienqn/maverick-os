# -*- perl -*-
use strict;
use warnings;
use tests::tests;
our ($test);

my (@output) = read_text_file ("$test.output");
common_checks ("run", @output);

my $write_to_read = 0;
my $read_from_write = 0;

for my $line (@output) {
  $write_to_read = 1 if $line =~ /write to read fd returned/;
  $read_from_write = 1 if $line =~ /read from write fd returned/;
}

fail "write to read fd not tested\n" unless $write_to_read;
fail "read from write fd not tested\n" unless $read_from_write;
pass;
