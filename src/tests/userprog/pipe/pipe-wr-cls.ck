# -*- perl -*-
use strict;
use warnings;
use tests::tests;
our ($test);

my (@output) = read_text_file ("$test.output");
common_checks ("run", @output);

my $read_end_closed = 0;
my $write_result = 0;

for my $line (@output) {
  $read_end_closed = 1 if $line =~ /read end closed/;
  $write_result = 1 if $line =~ /write to broken pipe returned/;
}

fail "read end not closed\n" unless $read_end_closed;
fail "write to broken pipe not tested\n" unless $write_result;
pass;
