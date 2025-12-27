1. What virtual address did the program try to access from userspace that caused it to crash? Why is the program not allowed to access this memory address at this point? (Be specific, mentioning specific macros from the Pintos codebase.)

0xc0000008

No, this is kernel address space. 

#define LOADER_PHYS_BASE 0xc0000000 /* 3 GB. */

2. What is the virtual address of the instruction that resulted in the crash?

eip=0x8048915

3. To investigate, disassemble the do-nothing binary using i386-objdump (you used this tool in Homework 0). What is the name of the function the program was in when it crashed? Copy the disassembled code for that function onto Gradescope, and identify the instruction at which the program crashed.

_start 
 8048915:	8b 45 0c             	mov    0xc(%ebp),%eax

4. Find the C code for the function you identified above (Hint: it was executed in userspace, so it’s either in do-nothing.c or one of the files in proj-pregame/src/lib or proj-pregame/src/lib/user), and copy it onto Gradescope.

void _start(int argc, char* argv[]);

void _start(int argc, char* argv[]) { exit(main(argc, argv)); }

5. For each instruction in the disassembled function in #3, try to justify to yourself in a few words why it’s necessary and/or what it’s trying to do. Hint: read about 80x86 calling convention. Why did the instruction you identified in #3 try to access memory at the virtual address you identified in #1? Please provide a high-level explanation, rather than simply mentioning register values.

For _start to call main, it needs to abide by the calling convention as a caller and a calle.

push   %ebp - Saves the old frame pointer on the stack
mov    %esp,%ebp - Set up a new frame pointer
sub    $0x18,%esp - Allocate Allocates 24 bytes (0x18) on the stack (for alignment and local storage)
mov    0xc(%ebp),%eax - Load argv (2nd parameter, at ebp+12)
mov    %eax,0x4(%esp) - Store argv at esp+4 (2nd argument)
mov    0x8(%ebp),%eax - Load argc (1st parameter, at ebp+8)
mov    %eax,(%esp) - Store argc at esp+0 (1st argument)

The stack isn't set up properly so when we try to access 0xc(%ebp), it's accessing kernel space.

6. Step into the process_execute function. What is the name and address of the thread running this function? What other threads are present in Pintos at this time? Copy their struct threads. (Hint: for the last part, dumplist &all_list thread allelem may be useful.)

```c
{tid = 1, status = THREAD_RUNNING, name = d"main", '\000' <repeats 11 times>,
  stack = 0xc000edbc "\335\322\002\300\n", priority = 31, allelem = {
    prev = 0xc003b19c <all_list>, next = 0xc0104020}, elem = {
    prev = 0xc003b18c <fifo_ready_list>, next = 0xc003b194 <fifo_ready_list+8>},
  pcb = 0xc010500c, magic = 3446325067}
```

```c
pintos-debug: dumplist #0: 0xc000e000 {tid = 1, status = THREAD_RUNNING,
  name = "main", '\000' <repeats 11 times>, stack = 0xc000edbc "\335\322\002\300\n",
  priority = 31, allelem = {prev = 0xc003b19c <all_list>, next = 0xc0104020}, elem = {
    prev = 0xc003b18c <fifo_ready_list>, next = 0xc003b194 <fifo_ready_list+8>},
  pcb = 0xc010500c, magic = 3446325067}
pintos-debug: dumplist #1: 0xc0104000 {tid = 2, status = THREAD_BLOCKED,
  name = "idle", '\000' <repeats 11 times>, stack = 0xc0104f14 "", priority = 0,
  allelem = {prev = 0xc000e020, next = 0xc003b1a4 <all_list+8>}, elem = {
    prev = 0xc003b18c <fifo_ready_list>, next = 0xc003b194 <fifo_ready_list+8>},
  pcb = 0x0, magic = 3446325067}
```

7. What is the backtrace for the current thread? Copy the backtrace from GDB as your answer.

#0  process_execute (file_name=0xc0007d50 "do-nothing") at ../../userprog/process.c:57
#1  0xc0020a62 in run_task (argv=0xc003b08c <argv+12>) at ../../threads/init.c:315
#2  0xc0020ba4 in run_actions (argv=0xc003b08c <argv+12>) at ../../threads/init.c:388
#3  0xc0020421 in main () at ../../threads/init.c:136

8. Step through the start_process function until you have stepped over the call to load. Note that load sets the eip and esp fields in the if_ structure. Print out the value of the if_ structure, displaying the values in hex (hint: print/x if_).

```c
{edi = 0x0, esi = 0x0, ebp = 0x0, esp_dummy = 0x0, ebx = 0x0, edx = 0x0, ecx = 0x0, eax = 0x0, gs = 0x23, fs = 0x23,
  es = 0x23, ds = 0x23, vec_no = 0x0, error_code = 0x0, frame_pointer = 0x0, eip = 0x804890f, cs = 0x1b, eflags = 0x202,
  esp = 0xc0000000, ss = 0x23}
```

This is a dump of the `struct intr_frame` (interrupt frame) in Pintos. Specifically, in the context of `start_process`, it represents the **initial CPU state** that the kernel has prepared for a new user program just before it starts running.

Here is the breakdown of what these fields mean:

### 1. The Context
This structure is used to simulate a "return from interrupt" to switch the CPU from kernel mode to user mode. The kernel sets up this "fake" interrupt frame and then executes an `iret` (interrupt return) instruction, which pops these values into the actual CPU registers, effectively jumping into the user program.

### 2. Key Fields Breakdown

*   **`eip = 0x804890f` (Instruction Pointer)**
    *   This is the memory address where the user program will begin executing.
    *   It points to the entry point of the executable (usually the `_start` function).

*   **`esp = 0xc0000000` (Stack Pointer)**
    *   `0xc0000000` (3GB) is the top of the user address space in Pintos.
    *   The stack grows downwards from here. This tells the CPU where the user stack starts.

*   **`cs = 0x1b` (Code Segment)**
    *   `0x1b` in binary is `0001 1011`.
    *   The last 2 bits (`11` = 3) indicate **Ring 3 (User Mode)**.
    *   This ensures the program runs with user privileges, preventing it from crashing the kernel.

*   **`ss = 0x23` (Stack Segment)** and **`ds/es/fs/gs = 0x23`**
    *   `0x23` in binary is `0010 0011`.
    *   Similar to `cs`, the last 2 bits indicate **Ring 3 (User Mode)**.
    *   These tell the CPU to use the User Data Segment for variables and the stack.

*   **`eflags = 0x202`**
    *   This sets the initial status flags for the CPU.
    *   `0x200` (bit 9) is the **IF (Interrupt Flag)**, meaning interrupts are enabled in the user program.
    *   `0x002` is a reserved bit that is always 1.

*   **`eax` through `edi` = 0x0**
    *   These are general-purpose registers (`eax`, `ebx`, `ecx`, etc.).
    *   They are initialized to zero to ensure a clean slate for the new process (prevents leaking data from the previous kernel task).

### Summary
The kernel is saying: "When I execute `iret`, set the instruction pointer to `0x804890f`, set the stack to `0xc0000000`, and switch to **User Mode** (`cs=0x1b`, `ss=0x23`)."

9. The first instruction in the asm volatile statement sets the stack pointer to the bottom of the if_ structure. The second one jumps to intr_exit. The comments in the code explain what’s happening here. Step into the asm volatile statement, and then step through the instructions. As you step through the iret instruction, observe that the function “returns” into userspace. Why does the processor switch modes when executing this function? Feel free to explain this in terms of the values in memory and/or registers at the time iret is executed, and the functionality of the iret instruction.

The struct intr_frame layout mirrors the x86 stack layout for interrupts. Since the stack grows downwards (high address to low address), and pop operations increment the stack pointer (move upwards), the structure members are defined in the order they will be popped.
struct intr_frame Memory Layout (Low Address -> High Address)
This is the order in memory (and the order fields appear in the struct definition in threads/interrupt.h):
Order	Field	Size	Description	Popped By
1	edi	4	Destination Index	popal
2	esi	4	Source Index	popal
3	ebp	4	Base Pointer	popal
4	esp_dummy	4	Unused (ignored by popal)	popal
5	ebx	4	Base Register	popal
6	edx	4	Data Register	popal
7	ecx	4	Counter Register	popal
8	eax	4	Accumulator Register	popal
9	gs	2	Segment Register GS	pop instruction
10	fs	2	Segment Register FS	pop instruction
11	es	2	Segment Register ES	pop instruction
12	ds	2	Segment Register DS	pop instruction
13	vec_no	4	Interrupt Vector Number	(Handled by stubs)
14	error_code	4	Error Code (or dummy)	(Handled by stubs)
15	eip	4	Instruction Pointer	iret
16	cs	4	Code Segment	iret
17	eflags	4	CPU Flags	iret
18	esp	4	Stack Pointer (User)	iret
19	ss	4	Stack Segment (User)	iret
(Note: vec_no and error_code are typically pushed by the CPU or the interrupt stub wrapper, but intr_exit usually skips over them or pops them before the final iret depending on the specific path.)

When you do movl %0, %%esp, esp points to 1 (edi).
popal pops 1–8.
pop instructions pop 9–12 (gs, fs, es, ds).
The code usually skips/pops 13–14.
iret pops 15–19 atomically to switch to user mode.

10. Once you’ve executed iret, type info registers to print out the contents of registers. Include the output of this command on Gradescope. How do these values compare to those when you printed out if_?

eax            0x0                 0
ecx            0x0                 0
edx            0x0                 0
ebx            0x0                 0
esp            0xc0000000          0xc0000000
ebp            0x0                 0x0
esi            0x0                 0
edi            0x0                 0
eip            0x804890f           0x804890f
eflags         0x202               [ IF ]
cs             0x1b                27
ss             0x23                35
ds             0x23                35

11. Notice that if you try to get your current location with backtrace you’ll only get a hex address. This is because because the debugger only loads in the symbols from the kernel. Now that we are in userspace, we have to load in the symbols from the Pintos executable we are running, namely do-nothing. To do this, use loadusersymbols tests/userprog/do-nothing. Now, using backtrace, you’ll see that you’re currently in the _start function. Using the disassemble and stepi commands, step through userspace instruction by instruction until the page fault occurs. At this point, the processor has immediately entered kernel mode to handle the page fault, so backtrace will show the current stack in kernel mode, not the user stack at the time of the page fault. However, you can use btpagefault to find the user stack at the time of the page fault. Copy down the output of btpagefault.

(gdb) btpagefault
#0  0x00000023 in ?? ()
#1  0xc0022449 in intr_entry () at ../../threads/intr-stubs.S:37
#2  0xc010bfb0 in ?? ()
#3  0x08048915 in _start (argc=-268370093, argv=0xf000ff53)
    at ../../lib/user/entry.c:6