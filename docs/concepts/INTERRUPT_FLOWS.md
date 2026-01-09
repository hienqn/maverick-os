# Interrupt, Exception, and System Call Flows in Pintos

This document explains how Pintos transitions between user mode and kernel mode for three classes of events:

- **Hardware interrupts**
- **Process exceptions**
- **System calls**

## Key takeaways

- All three flows share the same low-level entry path through IDT stubs and `intr_entry` onto the kernel stack.
- Only a subset of exceptions push a hardware error code; most interrupts synthesize `0` for consistency.
- External interrupt handlers run with interrupts off and should defer heavy work.
- System calls must validate all user pointers from `f->esp` before use.
- Page faults hinge on `CR2` and `error_code` to decide recovery vs. termination.

---

## 1. User-to-Kernel Transition: The Low-Level Mechanism

Before the specific flows, here is how the x86 CPU switches from Ring 3 (user) to Ring 0 (kernel).

### Key components

- **IDT (Interrupt Descriptor Table)**: Entry points for vectors 0–255.
- **GDT (Global Descriptor Table)**: Segment descriptors for code, data, TSS.
- **TSS (Task-State Segment)**: CPU uses `esp0` to locate the current thread’s kernel stack on privilege switches.

### The stack switch

1. CPU verifies the IDT entry’s DPL.
    - `int $0x30` (syscall) has DPL 3 (user-accessible).
    - Most others require DPL 0 (kernel-only).
2. On Ring 3 → Ring 0, the CPU switches to the kernel stack.
3. CPU reads `tss->esp0` (updated at each thread switch) for the top of the thread’s 4 KiB kernel stack.
4. Hardware pushes the user context on the kernel stack:
    - `SS`, `ESP`, `EFLAGS`, `CS`, `EIP`
    - For certain exceptions, the CPU also pushes an error code.

### Common entry path (intr-stubs.S)

1. A per-vector stub (`intrNN_stub`) runs.
    - For most interrupts, the stub pushes a synthetic `0` error code for a consistent frame.
    - For exceptions with real error codes (vectors 8, 10–14, 17), it preserves the hardware-pushed code.
    - The stub pushes the vector number and jumps to `intr_entry`.
2. `intr_entry`:
    - Saves segment registers (`DS`, `ES`, `FS`, `GS`).
    - Saves general-purpose registers (`pushal`).
    - Loads kernel segments (`DS`, `ES` = `SEL_KDSEG`).
    - Calls `intr_handler(struct intr_frame *f)`.

<aside>
💡

At this point, `struct intr_frame *f` points to the full saved state on the kernel stack.

</aside>

---

## Flow 1: Hardware Interrupts

**Trigger**: Asynchronous device signal (Timer, Keyboard, Disk)

### Characteristics

- Vectors: 0x20–0x2F
- Interrupts: disabled on entry (INTR_OFF)
- May preempt current thread

### Detailed flow

1. Device asserts IRQ to the PIC.
2. CPU handles the interrupt between instructions (see entry path above).
3. `intr_handler()` dispatches to the registered C handler via `intr_register_ext()`.
    - Example: `timer_interrupt()` increments `ticks`.
4. For external interrupts, a handler can request a yield by setting `yield_on_return = true`.
5. On exit, `intr_exit` restores registers and `iret` resumes execution.

<aside>
⚠️

External interrupt handlers run with interrupts off and must be fast. Defer expensive work.

</aside>

---

## Flow 2: Process Exceptions

**Trigger**: Synchronous CPU fault during an instruction (e.g., page fault, divide-by-zero)

### Characteristics

- Vectors: 0x00–0x1F
- Interrupts: usually enabled (INTR_ON), but page fault starts with them off
- Typical outcome: kill the user process unless the OS can legally resolve it (e.g., lazy load, stack growth)

### Exceptions with error codes

The CPU pushes an error code for these vectors: 0x08 (DF), 0x0A (TS), 0x0B (NP), 0x0C (SS), 0x0D (GP), 0x0E (PF), 0x11 (AC).

### Page fault example (vector 14)

1. Faulting user access occurs.
2. CPU aborts the instruction, pushes context + error code, and jumps to `intr14_stub`.
3. Stub preserves the error code and continues to `intr_entry` → `intr_handler()`.
4. `intr_handler()` calls `page_fault()`.
5. `page_fault()`:
    - Reads `CR2` (faulting virtual address).
    - Decodes `f->error_code`.
    - If `f->cs == SEL_KCSEG`, panic (kernel bug).
    - Else in user mode, decide whether to resolve (e.g., load page, grow stack) or kill the process.
6. If resolved, return and retry the instruction. Otherwise, terminate the process.

<aside>
🧭

Use `CR2` and error-code bits to distinguish present vs. not-present, write vs. read, and user vs. kernel faults.

</aside>

---

## Flow 3: System Calls

**Trigger**: `int $0x30` from user space

### Characteristics

- Vector: 0x30
- Interrupts: enabled (INTR_ON)
- DPL: 3 (user-accessible)

### Detailed flow

1. User wrapper (e.g., `write(fd, buf, size)`) pushes args on the user stack, then the syscall number, then executes `int $0x30`.
2. CPU switches to the kernel stack (via TSS) and enters the common path.
3. `intr_handler()` calls `syscall_handler()`.
4. `syscall_handler()` reads `f->esp` (the saved user ESP) to fetch arguments from user memory.
    - Validate all user pointers with `is_user_vaddr` and `pagedir_get_page` before dereferencing.
5. Dispatch based on syscall number, run kernel logic, and place the return value in `f->eax`.
6. `intr_exit` restores registers, `iret` resumes in user space; user wrapper returns the value.

<aside>
🔒

Always validate user pointers and buffers derived from `f->esp` before use in the kernel.

</aside>

---

## 5. Summary and Comparison

| Feature | Hardware Interrupt | Process Exception | System Call |
| --- | --- | --- | --- |
| Trigger | External device (Timer, I/O) | CPU instruction error | `int $0x30` instruction |
| Vector | 0x20–0x2F | 0x00–0x1F | 0x30 |
| Interrupts | Disabled on entry (INTR_OFF) | Varies (page fault starts OFF) | Enabled (INTR_ON) |
| Can sleep? | No (atomic context) | Yes, unless interrupts are off | Yes (normal kernel context) |
| Return | `iret` | `iret` (retry or death) | `iret` (EAX holds return) |
| Stack | Kernel stack via TSS | Kernel stack via TSS | Kernel stack via TSS |

### Visualizing the stack frame (struct intr_frame)

| Order (top→bottom) | What is saved | Pushed by | Notes |
| --- | --- | --- | --- |
| 1 | `SS`, `ESP`, `EFLAGS`, `CS`, `EIP` | CPU hardware | User return context |
| 2 | Error code (if applicable) | CPU or stub | Real for vectors 8, 10–14, 17 |
| 3 | Vector number | Stub | Identifies the event |
| 4 | Segment regs and GPRs | `intr_entry` | `DS`, `ES`, `FS`, `GS`, then `pushal` |

```
[ High Address ]
+----------------+
|      SS        |  <-- Pushed by CPU
|      ESP       |
|     EFLAGS     |
|      CS        |
|      EIP       |
+----------------+
|  Error Code    |  <-- CPU (selected exceptions) or stub (0 otherwise)
|   Vec No       |  <-- Stub
+----------------+
|  DS, ES, FS, GS|
|  GPRs (pushal) |
+----------------+
[ Low Address ]   <-- `struct intr_frame *f` points here in handler
```

---

## 6. Frequently Asked Questions (FAQ)

### How does the OS know which interrupt occurred?

The vector number identifies it:

1. CPU jumps to the IDT entry for that vector.
2. The per-vector stub pushes the vector number.
3. The common handler (`intr_handler`) reads it from the `intr_frame` to dispatch.

### Why use stubs instead of jumping straight to `intr_handler`?

- To save all registers the C code may clobber.
- To build a consistent stack layout (`struct intr_frame`).

### Why do only some exceptions have error codes?

Simple events do not need extra info; complex protection and paging faults do. Hardware pushes an error code for a defined subset of exceptions.

### Where are syscall arguments located?

On the user stack. `syscall_handler` reads them via `f->esp` after validating that addresses are user-accessible.

