# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_archive ({
  'a' => {
    'b' => {
      'c' => {
        'file' => ["Deep nested data"]
      }
    }
  }
});
pass;
