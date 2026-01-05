#include <syscall.h>

int main(int, char*[]);
void _start(int argc, char* argv[]);

/* stdio initialization (defined in stdio_file.c) */
extern void __stdio_init(void);
extern void __stdio_exit(void);

void _start(int argc, char* argv[]) {
  __stdio_init(); /* Initialize stdin, stdout, stderr */
  int ret = main(argc, argv);
  __stdio_exit(); /* Flush output streams */
  exit(ret);
}
