---
description: Debug page faults, triple faults, and memory errors
---

You are debugging a memory-related fault in PintOS (page fault, triple fault, or invalid memory access).

## Fault Types

| Fault | Symptom | Cause |
|-------|---------|-------|
| Page fault | "Page fault at 0x..." message | Invalid pointer dereference |
| Triple fault | QEMU reboots immediately | Unhandled exception in exception handler |
| General protection | "GP fault" message | Segment violation or privilege error |

## Step 1: Identify the fault type

```bash
maverick-test --test {test-name} --json
```

Look for:
- `panic.message` containing "Page fault"
- Very short output (triple fault - QEMU rebooted)
- No "Powering off" message

## Step 2: Debug page faults

```bash
maverick-debug --test {test-name} \
  --break page_fault \
  --eval "fault_addr" \
  --eval "not_present" \
  --eval "write" \
  --eval "user" \
  --max-stops 5
```

Interpret the flags:
- `not_present=1`: Page not mapped
- `not_present=0`: Permission violation
- `write=1`: Write access caused fault
- `user=1`: Fault from user code

### Common fault addresses

| Address | Meaning |
|---------|---------|
| `0x0` - `0xfff` | NULL pointer dereference |
| `0xcccccccc` | Uninitialized stack variable |
| `0xdeadbeef` | Freed memory marker |
| Near stack pointer | Stack overflow |

## Step 3: Debug triple faults

Triple faults are harder because the system reboots before you can inspect state.

```bash
# Break at interrupt entry
maverick-debug --test {test-name} \
  --break intr_handler \
  --eval "frame->vec_no" \
  --eval "frame->error_code" \
  --max-stops 20
```

Common causes:
1. **Stack overflow in kernel** - thread stack exhausted
2. **Invalid IDT/GDT** - corrupted interrupt tables
3. **Exception in exception handler** - double fault becomes triple

### Check kernel stack

```bash
maverick-debug --test {test-name} \
  --break thread_current \
  --eval "thread_current()->stack" \
  --eval "(char*)thread_current() + PGSIZE - (char*)thread_current()->stack" \
  --max-stops 10
```

If stack usage approaches 4096 bytes, you have stack overflow.

## Step 4: Debug NULL pointer dereferences

```bash
# Find what's being dereferenced
maverick-debug --test {test-name} \
  --break-if "page_fault if fault_addr < 0x1000" \
  --commands "bt" \
  --max-stops 1
```

The backtrace shows which function dereferenced NULL.

## Step 5: Debug user pointer validation

```bash
maverick-debug --test {test-name} \
  --break syscall_handler \
  --eval "f->R.eax" \
  --eval "f->R.ebx" \
  --step 5 \
  --max-stops 10
```

Check if syscall arguments are valid user pointers.

## x86 vs RISC-V Differences

### x86 (i386)
- Page fault handler in `userprog/exception.c`
- `fault_addr` from CR2 register
- Triple fault = immediate reboot

### RISC-V
- Trap handler in `arch/riscv64/trap.S`
- `fault_addr` from `stval` CSR
- Different exception codes (see `mcause` values)

## Analysis Output

Provide:
1. **Fault type**: Page fault, triple fault, GP fault
2. **Fault address**: The address that caused the fault
3. **Fault context**: User or kernel mode
4. **Backtrace**: How execution reached the fault
5. **Root cause**: What code bug caused this
6. **Fix**: Specific changes needed

## Example

```
Page fault at 0x00000004
  not_present=1, write=0, user=0

Analysis:
- Kernel tried to READ from address 0x4
- This is NULL + 4 offset (accessing struct member of NULL pointer)
- Backtrace shows: process_execute -> load -> ... -> NULL deref
- Root cause: `file_open()` returned NULL, not checked before use
- Fix: Add NULL check after file_open() in load()
```
