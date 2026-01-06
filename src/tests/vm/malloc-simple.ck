# -*- perl -*-
use strict;
use warnings;
use tests::tests;

our ($test);
my (@output) = read_text_file ("$test.output");
common_checks ("run", @output);

# Check for key messages indicating malloc is working
my $ok = 1;
$ok &&= grep (/\(malloc-simple\) begin/, @output);
$ok &&= grep (/\(malloc-simple\) small allocation write succeeded/, @output);
$ok &&= grep (/\(malloc-simple\) large allocation write succeeded/, @output);
$ok &&= grep (/\(malloc-simple\) calloc returned zeroed memory/, @output);
$ok &&= grep (/\(malloc-simple\) realloc preserved original data/, @output);
$ok &&= grep (/\(malloc-simple\) allocated 20 blocks of 64 bytes/, @output);
$ok &&= grep (/\(malloc-simple\) freed all 20 blocks/, @output);

# Edge case checks
$ok &&= grep (/\(malloc-simple\) malloc\(0\) handled correctly/, @output);
$ok &&= grep (/\(malloc-simple\) free\(NULL\) succeeded/, @output);
$ok &&= grep (/\(malloc-simple\) realloc\(NULL, n\) works like malloc/, @output);
$ok &&= grep (/\(malloc-simple\) realloc shrink preserved data/, @output);
$ok &&= grep (/\(malloc-simple\) realloc\(ptr, 0\) handled correctly/, @output);
$ok &&= grep (/\(malloc-simple\) all allocations properly aligned/, @output);
$ok &&= grep (/\(malloc-simple\) page-size allocation succeeded/, @output);
$ok &&= grep (/\(malloc-simple\) memory reuse test completed/, @output);

$ok &&= grep (/\(malloc-simple\) end/, @output);

if ($ok) {
    pass;
} else {
    fail "Missing expected output messages\n";
}
