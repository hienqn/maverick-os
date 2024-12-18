CS 162 Group Repository
=======================

This repository contains code for CS 162 group projects.

1. What virtual address did the program try to access from userspace that caused it to crash? Why is the program not allowed to access this memory address at this point? (Be specific, mentioning specific macros from the Pintos codebase.

This virtual address: 0xc0000008

PHYS_BASE


It tries to access kernel address space which starts at 0xc0000008. This value must be less than LOADER_PHYS_BASE
LOADER_PHYS_BASE = 0xc0000000

2. What is the virtual address of the instruction that resulted in the crash?
eip=0x8048915

3. To investigate, disassemble the do-nothing binary using i386-objdump (you used this tool in Homework 0). What is the name of the function the program was in when it crashed? Copy the disassembled code for that function onto Gradescope, and identify the instruction at which the program crashed.

What is the name of the function the program was in when it crashed? _start

i386-objdump -d do-nothing | grep -A 10 -B 10 8048915

0804890f <_start>:
 804890f:       55                      push   %ebp
 8048910:       89 e5                   mov    %esp,%ebp
 8048912:       83 ec 18                sub    $0x18,%esp
 8048915:       8b 45 0c                mov    0xc(%ebp),%eax
 8048918:       89 44 24 04             mov    %eax,0x4(%esp)
 804891c:       8b 45 08                mov    0x8(%ebp),%eax
 804891f:       89 04 24                mov    %eax,(%esp)
 8048922:       e8 6d f7 ff ff          call   8048094 <main>
 8048927:       89 04 24                mov    %eax,(%esp)
 804892a:       e8 d4 22 00 00          call   804ac03 <exit>

4. Find the C code for the function you identified above (Hint: it was executed in userspace, so it’s either in do-nothing.c or one of the files in proj-pregame/src/lib or proj-pregame/src/lib/user), and copy it onto Gradescope. For each instruction in the disassembled function in #3, explain in a few words why it’s necessary and/or what it’s trying to do. Hint: read about 80x86 calling convention.

0804890f <_start>:
 804890f:       55                      push   %ebp // save the previous base pointer on the stack
 8048910:       89 e5                   mov    %esp,%ebp // establish a new base pointer - stack frame
 8048912:       83 ec 18                sub    $0x18,%esp // Allocates 24 bytes (0x18) of space on the stack for local variables.
 8048915:       8b 45 0c                mov    0xc(%ebp),%eax // Loads the value at offset 0xc from the base pointer (%ebp) into the eax register, likely argc
 8048918:       89 44 24 04             mov    %eax,0x4(%esp) // Store the env pointer on stack
 804891c:       8b 45 08                mov    0x8(%ebp),%eax // Loads the value at offset 0x8 from the base pointer into the eax register, likely argv
 804891f:       89 04 24                mov    %eax,(%esp) // save that on the stack
 8048922:       e8 6d f7 ff ff          call   8048094 <main> // call main
 8048927:       89 04 24                mov    %eax,(%esp) // 
 804892a:       e8 d4 22 00 00          call   804ac03 <exit> // exit

5. Why did the instruction you identified in #3 try to access memory at the virtual address you identified in #1? Please provide a high-level explanation, rather than simply mentioning register values.

It has to get argv to pass into the main fucntion so that's why it needs to access the memory. However, the base pointer is set up not correctly so it access invalid address space

6. Step into the process_execute function. What is the name and address of the thread running this function? What other threads are present in Pintos at this time? Copy their struct threads. (Hint: for the last part, dumplist &all_list thread allelem may be useful.)

*1    Thread <main>     process_execute (file_name=0xc0007d50 "do-nothing") at ../../userprog/process.c:57

pintos-debug: dumplist #0: 0xc000e000 {tid = 1, status = THREAD_RUNNING, name = "main", '\000' <repeats 11 times>, stack = 0xc000edbc "\335\322\002\300\n", priority = 31, allelem = {
    prev = 0xc003b19c <all_list>, next = 0xc0104020}, elem = {prev = 0xc003b18c <fifo_ready_list>, next = 0xc003b194 <fifo_ready_list+8>}, pcb = 0xc010500c, magic = 3446325067}

pintos-debug: dumplist #1: 0xc0104000 {tid = 2, status = THREAD_BLOCKED, name = "idle", '\000' <repeats 11 times>, stack = 0xc0104f14 "", priority = 0, allelem = {prev = 0xc000e020,
    next = 0xc003b1a4 <all_list+8>}, elem = {prev = 0xc003b18c <fifo_ready_list>, next = 0xc003b194 <fifo_ready_list+8>}, pcb = 0x0, magic = 3446325067}

7. What is the backtrace for the current thread? Copy the backtrace from GDB as your answer and also copy down the line of C code corresponding to each function call.

#0  process_execute (file_name=0xc0007d50 "do-nothing") at ../../userprog/process.c:57
#1  0xc0020a62 in run_task (argv=0xc003b08c <argv+12>) at ../../threads/init.c:315
#2  0xc0020ba4 in run_actions (argv=0xc003b08c <argv+12>) at ../../threads/init.c:388
#3  0xc0020421 in main () at ../../threads/init.c:136

8. Set a breakpoint at start_process and continue to that point. What is the name and address of the thread running this function? What other threads are present in Pintos at this time? Copy their struct threads.

*1    Thread <main>     start_process (file_name_=0xc010a000) at ../../userprog/process.c:75

pintos-debug: dumplist #0: 0xc000e000 {tid = 1, status = THREAD_BLOCKED, name = "main", '\000' <repeats 11 times>, stack = 0xc000ee7c "", priority = 31, allelem = {prev = 0xc003b19c <all_list>,
    next = 0xc0104020}, elem = {prev = 0xc003cbb8 <temporary+4>, next = 0xc003cbc0 <temporary+12>}, pcb = 0xc010500c, magic = 3446325067}
pintos-debug: dumplist #1: 0xc0104000 {tid = 2, status = THREAD_BLOCKED, name = "idle", '\000' <repeats 11 times>, stack = 0xc0104f14 "", priority = 0, allelem = {prev = 0xc000e020,
    next = 0xc010b020}, elem = {prev = 0xc003b18c <fifo_ready_list>, next = 0xc003b194 <fifo_ready_list+8>}, pcb = 0x0, magic = 3446325067}
pintos-debug: dumplist #2: 0xc010b000 {tid = 3, status = THREAD_RUNNING, name = "do-nothing\000\000\000\000\000", stack = 0xc010bfd4 "", priority = 31, allelem = {prev = 0xc0104020,
    next = 0xc003b1a4 <all_list+8>}, elem = {prev = 0xc003b18c <fifo_ready_list>, next = 0xc003b194 <fifo_ready_list+8>}, pcb = 0x0, magic = 3446325067}

9. Step through the start_process function until you have stepped over the call to load. Note that load sets the eip and esp fields in the if_ structure. Print out the value of the if_ structure, displaying the values in hex (hint: print/x if_).

$2 = {edi = 0x0, esi = 0x0, ebp = 0x0, esp_dummy = 0x0, ebx = 0x0, edx = 0x0, ecx = 0x0, eax = 0x0, gs = 0x23, fs = 0x23, es = 0x23, ds = 0x23, vec_no = 0x0, error_code = 0x0,
  frame_pointer = 0x0, eip = 0x804890f, cs = 0x1b, eflags = 0x202, esp = 0xc0000000, ss = 0x23}

10. The first instruction in the asm volatile statement sets the stack pointer to the bottom of the if_ structure. The second one jumps to intr_exit. The comments in the code explain what’s happening here. Step into the asm volatile statement, and then step through the instructions. As you step through the iret instruction, observe that the function “returns” into userspace. Why does the processor switch modes when executing this function? Feel free to explain this in terms of the values in memory and/or registers at the time iret is executed, and the functionality of the iret instruction.

Let me break down what happens step by step when iret executes:

Initial state (kernel mode):

CopyKernel Memory:
if_ structure (ESP points here after movl):
+------------------+ <- ESP
| ss    = 0x23    |    Last to pop
| esp   = 0xc0000000    |
| eflags= 0x202   |
| cs    = 0x1b    |
| eip   = 0x804890f|    First to pop
+------------------+

When iret executes, it pops in this order:

Copypop eip    -> 0x804890f  (_start address)
pop cs     -> 0x1b       (user code segment with CPL=3)
pop eflags -> 0x202
pop esp    -> 0xc0000000 (new stack in user memory)
pop ss     -> 0x23       (user stack segment)

The processor switches modes because:

CS value (0x1b) has its low bits set to 3 (user mode)
SS value (0x23) is a user stack segment
When iret sees these segment values, it knows it needs to switch privilege levels
It validates the switch is allowed
Then completes the transition to user mode

This is why setting those segment values (SEL_UDSEG, SEL_UCSEG) was so important in the if_ setup!

11. Once you’ve executed iret, type info registers to print out the contents of registers. Include the output of this command on Gradescope. How do these values compare to those when you printed out if_?

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
es             0x23                35
fs             0x23                35
gs             0x23                35

12. Notice that if you try to get your current location with backtrace you’ll only get a hex address. This is because because the debugger only loads in the symbols from the kernel. Now that we are in userspace, we have to load in the symbols from the Pintos executable we are running, namely do-nothing. To do this, use loadusersymbols tests/userprog/do-nothing. Now, using backtrace, you’ll see that you’re currently in the _start function. Using the disassemble and stepi commands, step through userspace instruction by instruction until the page fault occurs. At this point, the processor has immediately entered kernel mode to handle the page fault, so backtrace will show the current stack in kernel mode, not the user stack at the time of the page fault. However, you can use btpagefault to find the user stack at the time of the page fault. Copy down the output of btpagefault.

#0  0xc00224d2 in intr0e_stub ()
#1  0x00000005 in ?? ()
Backtrace stopped: previous frame inner to this frame (corrupt stack?)

13. Modify the Pintos kernel so that do-nothing no longer crashes. Your change should be in the Pintos kernel, not the userspace program (do-nothing.c) or libraries in proj-pregame/src/lib. This should not involve extensive changes to the Pintos source code. Our staff solution solves this with a single-line change to process.c. Explain the change you made to Pintos and why it was necessary. After making this change, the do-nothing test should pass but all others will likely fail. Note: It is okay if your change seems like a hack. You will implement a better fix in the user programs project.

it pushes the stack pointer by 0xc, so when it access it executing mov    0xc(%ebp),%eax, the address is valid. The argv/argc should be saved on user stack
