/**
 * @file threads/init.c
 * @brief Pintos kernel initialization and boot sequence.
 *
 * This file implements the main kernel initialization sequence for Pintos.
 * It handles:
 * - BSS segment initialization
 * - Command line parsing
 * - Memory system setup (paging, physical page allocator, malloc)
 * - Device initialization (timer, keyboard, serial, network)
 * - Thread system initialization
 * - File system initialization (if FILESYS is defined)
 * - User program support initialization (if USERPROG is defined)
 * - Execution of kernel command line actions
 *
 * The initialization follows a strict order to ensure dependencies are
 * satisfied at each step. The main() function orchestrates the entire
 * boot sequence.
 *
 * @see threads/init.h for the public interface.
 */

#include "threads/init.h"
#include <console.h>
#include <debug.h>
#include <inttypes.h>
#include <limits.h>
#include <random.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <test-lib.h>
#include "devices/kbd.h"
#include "devices/input.h"
#include "devices/serial.h"
#include "devices/shutdown.h"
#include "devices/timer.h"
#include "devices/vga.h"
#include "devices/rtc.h"
#include "devices/e1000.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/thread.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/exception.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#include "tests/userprog/kernel/tests.h"
#endif
#ifdef VM
#include "vm/vm.h"
#endif
#ifdef THREADS
#include "tests/threads/tests.h"
#endif
#ifdef FILESYS
#include "devices/block.h"
#include "devices/ide.h"
#include "tests/filesys/kernel/tests.h"
#include "filesys/filesys.h"
#include "filesys/fsutil.h"
#endif

/* Page directory with kernel mappings only. */
uint32_t* init_page_dir;

#ifdef FILESYS
/**
 * @brief Whether to format the file system during initialization.
 *
 * Set to true if the "-f" command line option is specified.
 * When true, filesys_init() will format the file system device
 * before mounting it.
 */
static bool format_filesys;

/**
 * @brief Block device names from command line options.
 *
 * These override the default block device selection if specified
 * via command line options:
 * - filesys_bdev_name: Set by "-filesys=BDEV"
 * - scratch_bdev_name: Set by "-scratch=BDEV"
 * - swap_bdev_name: Set by "-swap=BDEV" (only if VM is defined)
 */
static const char* filesys_bdev_name;
static const char* scratch_bdev_name;
#ifdef VM
static const char* swap_bdev_name;
#endif
#endif /* FILESYS */

/**
 * @brief Maximum number of pages to allocate to the user pool.
 *
 * Set by the "-ul=COUNT" command line option. Defaults to SIZE_MAX
 * (unlimited). This limits how much physical memory can be used
 * for user processes, reserving the rest for kernel use.
 */
static size_t user_page_limit = SIZE_MAX;

static void bss_init(void);
static void paging_init(void);

static char** read_command_line(void);
static char** parse_options(char** argv);
static void run_actions(char** argv);
static void usage(void);

#ifdef FILESYS
static void locate_block_devices(void);
static void locate_block_device(enum block_type, const char* name);
#endif

/**
 * @brief Pintos kernel main entry point.
 *
 * This function orchestrates the complete kernel boot sequence:
 *
 * 1. **BSS Initialization**: Zero out uninitialized global variables
 * 2. **Command Line Parsing**: Read and parse kernel command line arguments
 * 3. **Thread System**: Initialize thread subsystem and console locking
 * 4. **Memory System**: Initialize physical page allocator, malloc, and paging
 * 5. **Segmentation**: Initialize GDT and TSS (if USERPROG is defined)
 * 6. **Interrupts**: Initialize interrupt handlers, timer, keyboard, input
 * 7. **User Program Support**: Initialize exception handlers and syscalls (if USERPROG)
 * 8. **Thread Scheduler**: Start thread scheduling and enable interrupts
 * 9. **Network**: Initialize network device (e1000)
 * 10. **User Program Init**: Set up main thread's PCB (if USERPROG)
 * 11. **File System**: Initialize IDE, locate block devices, mount filesys (if FILESYS)
 * 12. **Actions**: Execute command line actions (run programs, tests, etc.)
 * 13. **Shutdown**: Clean shutdown or reboot
 *
 * @note The initialization order is critical - later steps depend on earlier ones.
 *       For example, paging must be initialized before malloc can work, and
 *       interrupts must be initialized before the scheduler can run.
 *
 * @return Never returns normally - calls shutdown() or thread_exit() at the end.
 */
int main(void) {
  char** argv;

  /* Clear BSS. */
  bss_init();

  /* Break command line into arguments and parse options. */
  argv = read_command_line();
  argv = parse_options(argv);

  /* Initialize ourselves as a thread so we can use locks,
     then enable console locking. */
  thread_init();
  console_init();

  /* Greet user. */
  printf("Pintos booting with %'" PRIu32 " kB RAM...\n", init_ram_pages * PGSIZE / 1024);

  /* Initialize memory system. */
  palloc_init(user_page_limit);
  malloc_init();
  paging_init();

  /* Segmentation. */
#ifdef USERPROG
  tss_init();
  gdt_init();
#endif

  /* Initialize interrupt handlers. */
  intr_init();
  timer_init();
  kbd_init();
  input_init();
#ifdef USERPROG
  exception_init();
  syscall_init();
#endif

  /* Start thread scheduler and enable interrupts. */
  thread_start();
  serial_init_queue();
  timer_calibrate();

  /* Initialize network device. */
  e1000_init();

#ifdef USERPROG
  /* Give main thread a minimal PCB so it can launch the first process */
  userprog_init();
#endif

#ifdef FILESYS
  /* Initialize file system. */
  ide_init();
  locate_block_devices();
  filesys_init(format_filesys);
#endif

#ifdef VM
  /* Initialize virtual memory subsystem.
     Must be after locate_block_devices() so swap partition is available. */
  vm_init();
#endif

  printf("Boot complete.\n");

  /* Run actions specified on kernel command line. */
  run_actions(argv);

  /* Finish up. */
  shutdown();
  thread_exit();
}

/**
 * @brief Clear the BSS (Block Started by Symbol) segment.
 *
 * The BSS segment contains uninitialized global and static variables
 * that should be initialized to zero. The kernel loader doesn't zero
 * this segment, so we must do it explicitly during boot.
 *
 * The linker script (kernel.lds) defines symbols _start_bss and _end_bss
 * that mark the boundaries of the BSS segment. We zero all memory
 * between these two addresses.
 *
 * @note This must be called before any global/static variables are used,
 *       as they may contain garbage values otherwise.
 *
 * @see kernel.lds for linker script that defines _start_bss and _end_bss.
 */
static void bss_init(void) {
  extern char _start_bss, _end_bss;
  memset(&_start_bss, 0, &_end_bss - &_start_bss);
}

/**
 * @brief Initialize kernel paging and create initial page directory.
 *
 * This function sets up the kernel's virtual memory system by:
 * 1. Allocating a new page directory (init_page_dir)
 * 2. Creating page tables for all physical memory pages
 * 3. Mapping each physical page to its corresponding kernel virtual address
 * 4. Marking kernel text pages as read-only (executable but not writable)
 * 5. Activating the page directory by loading it into CR3
 *
 * The mapping created is a direct identity mapping: physical address X
 * maps to kernel virtual address ptov(X). This allows the kernel to
 * access all physical memory through virtual addresses.
 *
 * Kernel text pages (between _start and _end_kernel_text) are marked
 * as read-only to prevent accidental modification of kernel code.
 *
 * @note This must be called after palloc_init() so that page allocation
 *       functions are available.
 *
 * @note After this function returns, the CPU is using the new page
 *       directory. The init_page_dir global variable points to it.
 *
 * @see threads/pte.h for pde_create() and pte_create_kernel() functions.
 * @see threads/loader.h for ptov() macro that converts physical to virtual.
 */
static void paging_init(void) {
  uint32_t *pd, *pt;
  size_t page;
  extern char _start, _end_kernel_text;

  pd = init_page_dir = palloc_get_page(PAL_ASSERT | PAL_ZERO);
  pt = NULL;
  for (page = 0; page < init_ram_pages; page++) {
    uintptr_t paddr = page * PGSIZE;
    char* vaddr = ptov(paddr);
    size_t pde_idx = pd_no(vaddr);
    size_t pte_idx = pt_no(vaddr);
    bool in_kernel_text = &_start <= vaddr && vaddr < &_end_kernel_text;

    if (pd[pde_idx] == 0) {
      pt = palloc_get_page(PAL_ASSERT | PAL_ZERO);
      pd[pde_idx] = pde_create(pt);
    }

    pt[pte_idx] = pte_create_kernel(vaddr, !in_kernel_text);
  }

  /* Store the physical address of the page directory into CR3
     aka PDBR (page directory base register).  This activates our
     new page tables immediately.  See [IA32-v2a] "MOV--Move
     to/from Control Registers" and [IA32-v3a] 3.7.5 "Base Address
     of the Page Directory". */
  asm volatile("movl %0, %%cr3" : : "r"(vtop(init_page_dir)));
}

/**
 * @brief Parse the kernel command line into an argument vector.
 *
 * The kernel command line is stored by the loader at a fixed location
 * in memory (LOADER_ARGS). This function:
 * 1. Reads the argument count from LOADER_ARG_CNT
 * 2. Parses the null-terminated argument strings from LOADER_ARGS
 * 3. Builds an argv-style array (null-terminated array of pointers)
 * 4. Prints the command line for debugging
 *
 * Arguments are separated by null bytes in the loader's storage area.
 * The function validates that arguments don't overflow the available space.
 *
 * @return Pointer to a static argv array. The array is null-terminated
 *         and remains valid for the lifetime of the kernel.
 *
 * @note The returned array is static, so subsequent calls will overwrite
 *       the previous contents. This is fine since it's only called once
 *       during boot.
 *
 * @see threads/loader.h for LOADER_ARGS, LOADER_ARG_CNT, and LOADER_ARGS_LEN.
 */
static char** read_command_line(void) {
  static char* argv[LOADER_ARGS_LEN / 2 + 1];
  char *p, *end;
  int argc;
  int i;

  argc = *(uint32_t*)ptov(LOADER_ARG_CNT);
  p = ptov(LOADER_ARGS);
  end = p + LOADER_ARGS_LEN;
  for (i = 0; i < argc; i++) {
    if (p >= end)
      PANIC("command line arguments overflow");

    argv[i] = p;
    p += strnlen(p, end - p) + 1;
  }
  argv[argc] = NULL;

  /* Print kernel command line. */
  printf("Kernel command line:");
  for (i = 0; i < argc; i++)
    if (strchr(argv[i], ' ') == NULL)
      printf(" %s", argv[i]);
    else
      printf(" '%s'", argv[i]);
  printf("\n");

  return argv;
}

/**
 * @brief Parse command line options and configure kernel behavior.
 *
 * This function processes kernel command line options and configures
 * various kernel subsystems accordingly. Supported options include:
 *
 * **General Options:**
 * - `-h`: Print help message and power off
 * - `-q`: Power off VM after actions or on panic
 * - `-r`: Reboot after actions
 * - `-rs=SEED`: Set random number generator seed
 *
 * **Scheduler Options:**
 * - `-sched=fifo`: Use first-in-first-out scheduler
 * - `-sched=prio`: Use strict-priority round-robin scheduler (default)
 * - `-sched=fair`: Use fair scheduler
 * - `-sched=mlfqs`: Use multi-level feedback queue scheduler
 *
 * **Fair Scheduler Options** (only used with `-sched=fair`):
 * - `-fair=stride`: Use stride scheduling (default)
 * - `-fair=lottery`: Use lottery scheduling
 * - `-fair=cfs`: Use Completely Fair Scheduler (CFS)
 * - `-fair=eevdf`: Use Earliest Eligible Virtual Deadline First (EEVDF)
 *
 * **File System Options** (if FILESYS is defined):
 * - `-f`: Format the file system during initialization
 * - `-filesys=BDEV`: Use BDEV as the file system block device
 * - `-scratch=BDEV`: Use BDEV as the scratch block device
 * - `-swap=BDEV`: Use BDEV as the swap block device (if VM is defined)
 *
 * **User Program Options** (if USERPROG is defined):
 * - `-ul=COUNT`: Limit user memory to COUNT pages
 *
 * @param argv Array of command line arguments (from read_command_line())
 * @return Pointer to the first non-option argument (the first action to execute)
 *
 * @note Options must precede actions on the command line.
 * @note Scheduler flags are mutually exclusive - setting multiple will panic.
 * @note Fair scheduler flags are mutually exclusive - setting multiple will panic.
 * @note If no scheduler is specified, defaults to SCHED_PRIO.
 * @note If no fair scheduler is specified (when using -sched=fair), defaults to stride.
 */
static char** parse_options(char** argv) {
  bool scheduler_flags[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  bool fair_sched_flags[4] = {0, 0, 0, 0};

  for (; *argv != NULL && **argv == '-'; argv++) {
    char* save_ptr;
    char* name = strtok_r(*argv, "=", &save_ptr);
    char* value = strtok_r(NULL, "", &save_ptr);

    if (!strcmp(name, "-h"))
      usage();
    else if (!strcmp(name, "-q"))
      shutdown_configure(SHUTDOWN_POWER_OFF);
    else if (!strcmp(name, "-r"))
      shutdown_configure(SHUTDOWN_REBOOT);
#ifdef FILESYS
    else if (!strcmp(name, "-f"))
      format_filesys = true;
    else if (!strcmp(name, "-filesys"))
      filesys_bdev_name = value;
    else if (!strcmp(name, "-scratch"))
      scratch_bdev_name = value;
#ifdef VM
    else if (!strcmp(name, "-swap"))
      swap_bdev_name = value;
#endif
#endif
    else if (!strcmp(name, "-rs"))
      random_init(atoi(value));
    else if (!strcmp(name, "-sched")) {
      if (!strcmp(value, "fifo"))
        scheduler_flags[SCHED_FIFO] = 1;
      else if (!strcmp(value, "prio"))
        scheduler_flags[SCHED_PRIO] = 1;
      else if (!strcmp(value, "fair"))
        scheduler_flags[SCHED_FAIR] = 1;
      else if (!strcmp(value, "mlfqs"))
        scheduler_flags[SCHED_MLFQS] = 1;
      else
        PANIC("unknown scheduler option `%s' (use -h for help)", value);
    } else if (!strcmp(name, "-fair")) {
      if (!strcmp(value, "stride"))
        fair_sched_flags[FAIR_SCHED_STRIDE] = 1;
      else if (!strcmp(value, "lottery"))
        fair_sched_flags[FAIR_SCHED_LOTTERY] = 1;
      else if (!strcmp(value, "cfs"))
        fair_sched_flags[FAIR_SCHED_CFS] = 1;
      else if (!strcmp(value, "eevdf"))
        fair_sched_flags[FAIR_SCHED_EEVDF] = 1;
      else
        PANIC("unknown fair scheduler option `%s' (use -h for help)", value);
    }
#ifdef USERPROG
    else if (!strcmp(name, "-ul"))
      user_page_limit = atoi(value);
#endif
    else
      PANIC("unknown option `%s' (use -h for help)", name);
  }

  /* Configure the kernel scheduler to use the algorithm
   * corresponding to the requested command-line options.
   * All three flags are mutually-exclusive, and as such
   * setting multiple will panic.
   * If none are set, the scheduler is set to use the
   * default value, SCHED_PRIO. */
  size_t sched_flags_set = 0;
  for (int i = 0; i < 8; i++) {
    sched_flags_set += scheduler_flags[i];
  }
  if (sched_flags_set == 0)
    active_sched_policy = SCHED_DEFAULT;
  else if (sched_flags_set > 1)
    PANIC("too many scheduler flags set: set at most one of \"-sched-fifo\", \"-sched-prio\", "
          "\"-sched-fair\", \"-sched-mlfqs\"");
  else if (scheduler_flags[SCHED_FIFO])
    active_sched_policy = SCHED_FIFO;
  else if (scheduler_flags[SCHED_PRIO])
    active_sched_policy = SCHED_PRIO;
  else if (scheduler_flags[SCHED_FAIR])
    active_sched_policy = SCHED_FAIR;
  else if (scheduler_flags[SCHED_MLFQS])
    active_sched_policy = SCHED_MLFQS;
  else
    PANIC("kernel bug in init.c: unreachable case");

  /* Configure which fair scheduler implementation to use when
   * active_sched_policy == SCHED_FAIR.
   * These flags are mutually-exclusive.
   * If none are set, defaults to FAIR_SCHED_STRIDE. */
  size_t fair_flags_set = 0;
  for (int i = 0; i < 4; i++) {
    fair_flags_set += fair_sched_flags[i];
  }
  if (fair_flags_set == 0)
    active_fair_sched_type = FAIR_SCHED_DEFAULT;
  else if (fair_flags_set > 1)
    PANIC("too many fair scheduler flags set: set at most one of \"-fair=stride\", "
          "\"-fair=lottery\", \"-fair=cfs\", \"-fair=eevdf\"");
  else if (fair_sched_flags[FAIR_SCHED_STRIDE])
    active_fair_sched_type = FAIR_SCHED_STRIDE;
  else if (fair_sched_flags[FAIR_SCHED_LOTTERY])
    active_fair_sched_type = FAIR_SCHED_LOTTERY;
  else if (fair_sched_flags[FAIR_SCHED_CFS])
    active_fair_sched_type = FAIR_SCHED_CFS;
  else if (fair_sched_flags[FAIR_SCHED_EEVDF])
    active_fair_sched_type = FAIR_SCHED_EEVDF;
  else
    PANIC("kernel bug in init.c: unreachable case in fair scheduler selection");

  /* Initialize the random number generator based on the system
     time.  This has no effect if an "-rs" option was specified.

     When running under Bochs, this is not enough by itself to
     get a good seed value, because the pintos script sets the
     initial time to a predictable value, not to the local time,
     for reproducibility.  To fix this, give the "-r" option to
     the pintos script to request real-time execution. */
  random_init(rtc_get_time());

  return argv;
}

/**
 * @brief Execute a user program task.
 *
 * Runs the user program specified in argv[1] and waits for it to complete.
 * This is the handler for the "run" action on the kernel command line.
 *
 * @param argv Command line arguments. argv[1] must contain the program name
 *             and optional arguments (space-separated).
 *
 * @note Only available if USERPROG is defined.
 * @note The program name may include arguments, e.g., "ls -l /"
 */
static void run_task(char** argv) {
  const char* task = argv[1];

  printf("Executing '%s':\n", task);
#ifdef USERPROG
  process_wait(process_execute(task));
#endif
  printf("Execution of '%s' complete.\n", task);
}

/**
 * @brief Execute a userprog kernel test.
 *
 * Runs the userprog kernel test specified in argv[1]. This is the handler
 * for the "rukt" (Run Userprog Kernel Test) action.
 *
 * @param argv Command line arguments. argv[1] must contain the test name.
 *
 * @note Only available if USERPROG is defined.
 */
static void run_userprog_kernel_task(char** argv) {
  const char* task = argv[1];

  printf("Executing '%s':\n", task);
#ifdef USERPROG
  run_userprog_test(task);
#endif
  printf("Execution of '%s' complete.\n", task);
}

#ifdef THREADS
/**
 * @brief Execute a threads kernel test.
 *
 * Runs the threads kernel test specified in argv[1]. This is the handler
 * for the "rtkt" (Run Threads Kernel Test) action.
 *
 * @param argv Command line arguments. argv[1] must contain the test name.
 *
 * @note Only available if THREADS is defined.
 */
static void run_threads_kernel_task(char** argv) {
  const char* task = argv[1];

  printf("Executing '%s':\n", task);
  run_threads_test(task);
  printf("Execution of '%s' complete.\n", task);
}
#endif

#ifdef FILESYS
/**
 * @brief Execute a filesys kernel test.
 *
 * Runs the filesys kernel test specified in argv[1]. This is the handler
 * for the "rfkt" (Run Filesys Kernel Test) action.
 *
 * @param argv Command line arguments. argv[1] must contain the test name.
 *
 * @note Only available if FILESYS is defined.
 */
static void run_filesys_kernel_task(char** argv) {
  const char* task = argv[1];

  printf("Executing '%s':\n", task);
  run_filesys_kernel_test(task);
  printf("Execution of '%s' complete.\n", task);
}
#endif

/**
 * @brief Execute all actions specified on the kernel command line.
 *
 * This function processes the action list from the command line and
 * executes each action in order. Supported actions include:
 *
 * **User Program Actions** (if USERPROG is defined):
 * - `run 'PROG [ARG...]'`: Execute user program PROG with optional arguments
 * - `rukt TEST`: Run userprog kernel test TEST
 *
 * **Thread Actions** (if THREADS is defined):
 * - `rtkt TEST`: Run threads kernel test TEST
 *
 * **File System Actions** (if FILESYS is defined):
 * - `ls`: List files in the root directory
 * - `cat FILE`: Print FILE to the console
 * - `rm FILE`: Delete FILE
 * - `extract`: Untar from scratch device into file system
 * - `append FILE`: Append FILE to tar file on scratch device
 * - `rfkt TEST`: Run filesys kernel test TEST
 *
 * @param argv Array of action names and their arguments, terminated by NULL.
 *             Each action consumes a certain number of arguments (including
 *             the action name itself).
 *
 * @note Actions are executed in the order they appear on the command line.
 * @note Unknown actions will cause a panic.
 * @note Missing required arguments for an action will cause a panic.
 */
static void run_actions(char** argv) {
  /* An action. */
  struct action {
    char* name;                    /* Action name. */
    int argc;                      /* # of args, including action name. */
    void (*function)(char** argv); /* Function to execute action. */
  };

  /* Table of supported actions. */
  static const struct action actions[] = {
#ifdef USERPROG
      {"run", 2, run_task},
      {"rukt", 2, run_userprog_kernel_task},
#endif
#ifdef THREADS
      {"rtkt", 2, run_threads_kernel_task},
#endif
#ifdef FILESYS
      {"ls", 1, fsutil_ls},
      {"cat", 2, fsutil_cat},
      {"rm", 2, fsutil_rm},
      {"extract", 1, fsutil_extract},
      {"append", 2, fsutil_append},
      {"rfkt", 2, run_filesys_kernel_task}, /* Run Filesys Kernel Test */
#endif
      {NULL, 0, NULL},
  };

  while (*argv != NULL) {
    const struct action* a;
    int i;

    /* Find action name. */
    for (a = actions;; a++)
      if (a->name == NULL)
        PANIC("unknown action `%s' (use -h for help)", *argv);
      else if (!strcmp(*argv, a->name))
        break;

    /* Check for required arguments. */
    for (i = 1; i < a->argc; i++)
      if (argv[i] == NULL)
        PANIC("action `%s' requires %d argument(s)", *argv, a->argc - 1);

    /* Invoke action and advance. */
    a->function(argv);
    argv += a->argc;
  }
}

/**
 * @brief Print kernel command line help message and power off.
 *
 * Displays a comprehensive help message listing all available command
 * line options and actions. After printing, powers off the machine.
 *
 * This function is called when the "-h" option is specified on the
 * kernel command line.
 *
 * @note Never returns - calls shutdown_power_off() at the end.
 */
static void usage(void) {
  printf("\nCommand line syntax: [OPTION...] [ACTION...]\n"
         "Options must precede actions.\n"
         "Actions are executed in the order specified.\n"
         "\nAvailable actions:\n"
#ifdef USERPROG
         "  run 'PROG [ARG...]' Run PROG and wait for it to complete.\n"
         "  rukt TEST          Run userprog kernel test TEST.\n"
#endif
#ifdef THREADS
         "  rtkt TEST          Run threads kernel test TEST.\n"
#endif
#ifdef FILESYS
         "  ls                 List files in the root directory.\n"
         "  cat FILE           Print FILE to the console.\n"
         "  rm FILE            Delete FILE.\n"
         "Use these actions indirectly via `pintos' -g and -p options:\n"
         "  extract            Untar from scratch device into file system.\n"
         "  append FILE        Append FILE to tar file on scratch device.\n"
#endif
         "\nOptions:\n"
         "  -h                 Print this help message and power off.\n"
         "  -q                 Power off VM after actions or on panic.\n"
         "  -r                 Reboot after actions.\n"
#ifdef FILESYS
         "  -f                 Format file system device during startup.\n"
         "  -filesys=BDEV      Use BDEV for file system instead of default.\n"
         "  -scratch=BDEV      Use BDEV for scratch instead of default.\n"
#ifdef VM
         "  -swap=BDEV         Use BDEV for swap instead of default.\n"
#endif // VM
#endif // FILESYS
         "  -rs=SEED           Set random number seed to SEED.\n"
         "  -sched=fifo        Use first-in-first-out scheduler.\n"
         "  -sched=prio        Use strict-priority round-robin scheduler.\n"
         "  -sched=fair        Use fair scheduler. Mutually exclusive with other -sched options.\n"
         "  -sched=mlfqs       Use multi-level feedback queue scheduler.\n"
         "\n"
         "Fair scheduler options (only used when -sched=fair):\n"
         "  -fair=stride       Use stride scheduling (default).\n"
         "  -fair=lottery      Use lottery scheduling.\n"
         "  -fair=cfs          Use Completely Fair Scheduler (CFS).\n"
         "  -fair=eevdf        Use Earliest Eligible Virtual Deadline First (EEVDF).\n"
#ifdef USERPROG
         "  -ul=COUNT          Limit user memory to COUNT pages.\n"
#endif // USERPROG
  );
  shutdown_power_off();
}

#ifdef FILESYS
/**
 * @brief Locate and assign block devices for all Pintos roles.
 *
 * This function identifies which physical block devices should be used
 * for each Pintos role (filesys, scratch, swap) and assigns them.
 * It uses the names specified via command line options if provided,
 * otherwise it selects the first device of the appropriate type.
 *
 * Roles assigned:
 * - BLOCK_FILESYS: The file system storage device
 * - BLOCK_SCRATCH: The scratch device (used for tar operations)
 * - BLOCK_SWAP: The swap device (only if VM is defined)
 *
 * @note This must be called after ide_init() so that block devices
 *       are available for probing.
 *
 * @see locate_block_device() for the actual device selection logic.
 */
static void locate_block_devices(void) {
  locate_block_device(BLOCK_FILESYS, filesys_bdev_name);
  locate_block_device(BLOCK_SCRATCH, scratch_bdev_name);
#ifdef VM
  locate_block_device(BLOCK_SWAP, swap_bdev_name);
#endif
}

/**
 * @brief Locate and assign a block device for a specific role.
 *
 * Determines which block device should be used for the given role:
 * - If `name` is non-NULL, searches for a device with that exact name
 * - If `name` is NULL, selects the first device of the specified type
 *   found during device probing
 *
 * Once a device is found, it is assigned to the role via block_set_role().
 * If a name is specified but no matching device is found, the kernel panics.
 *
 * @param role The block device role (BLOCK_FILESYS, BLOCK_SCRATCH, or BLOCK_SWAP)
 * @param name Optional device name from command line option, or NULL to use default
 *
 * @note This function prints which device was selected for debugging.
 * @note If a name is specified but the device doesn't exist, the kernel panics.
 */
static void locate_block_device(enum block_type role, const char* name) {
  struct block* block = NULL;

  if (name != NULL) {
    block = block_get_by_name(name);
    if (block == NULL)
      PANIC("No such block device \"%s\"", name);
  } else {
    for (block = block_first(); block != NULL; block = block_next(block))
      if (block_type(block) == role)
        break;
  }

  if (block != NULL) {
    printf("%s: using %s\n", block_type_name(role), block_name(block));
    block_set_role(role, block);
  }
}
#endif // FILESYS
