/* echo.c

   Prints its command-line arguments to the console, separated by spaces.
   Tests argument passing from shell to user programs. */

#include <stdio.h>
#include <syscall.h>

int main(int argc, char** argv) {
  int i;

  for (i = 0; i < argc; i++)
    printf("%s ", argv[i]);
  printf("\n");

  return EXIT_SUCCESS;
}
