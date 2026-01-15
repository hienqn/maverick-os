# UART Serial Communication in Pintos

This document explains how Pintos communicates with the terminal through the 16550A UART (Universal Asynchronous Receiver/Transmitter) serial port.

## Key Takeaways

- Pintos uses the 16550A UART chip at I/O port `0x3F8` (COM1) for serial communication
- Two modes: polling (early boot) and interrupt-driven (after initialization)
- Output flows: user `printf()` → syscall → kernel console → UART → QEMU → terminal
- Input flows: terminal → QEMU → UART interrupt → input buffer → kernel

---

## 1. Hardware Overview: The 16550A UART

The 16550A is a standard PC serial port controller. Pintos uses it as the primary interface for terminal I/O when running under QEMU.

### Register Map (I/O Base: 0x3F8)

| Offset | Register | Name | Description |
|--------|----------|------|-------------|
| +0 | RBR | Receiver Buffer | Read received byte (read-only) |
| +0 | THR | Transmitter Holding | Write byte to transmit (write-only) |
| +1 | IER | Interrupt Enable | Enable RX/TX interrupts |
| +2 | IIR | Interrupt ID | Identify pending interrupt (read-only) |
| +2 | FCR | FIFO Control | Configure FIFO (write-only) |
| +3 | LCR | Line Control | Data format (8N1), DLAB access |
| +4 | MCR | Modem Control | Hardware flow control |
| +5 | LSR | Line Status | TX ready, RX data available |

### Key Status Bits (LSR Register)

```c
#define LSR_DR   0x01   /* Data Ready: byte available in RBR */
#define LSR_THRE 0x20   /* THR Empty: ready to accept byte for TX */
```

Source: `devices/serial.c:48-49`

---

## 2. Output Flow: printf() to Terminal

### ASCII Diagram

```
  USER SPACE                          KERNEL SPACE
 ┌─────────────────┐
 │  User Program   │
 │                 │
 │  printf("hi")   │
 │       │         │
 │       ▼         │
 │  lib/user/      │
 │  stdio_fmt.c    │
 │       │         │
 │       ▼         │
 │  write(1,buf,n) │  ◄── lib/user/syscall.c:199
 │       │         │
 └───────┼─────────┘
         │
         │ int $0x30 (x86)
         │ ecall    (RISC-V)
         ▼
 ════════════════════════════════════════════════════
         │  SYSCALL BOUNDARY
         ▼
 ┌─────────────────────────────────────────────────┐
 │              syscall_handler()                   │
 │              userprog/syscall.c                  │
 │                     │                            │
 │      SYS_WRITE ─────┴───► write to fd=1 (stdout)│
 └─────────────────────────────────────────────────┘
                       │
                       ▼
 ┌─────────────────────────────────────────────────┐
 │              putbuf() / printf()                 │
 │              lib/kernel/console.c                │
 │                     │                            │
 │    acquires console_lock (prevents interleaving) │
 │                     │                            │
 │         for each character:                      │
 │                     ▼                            │
 │              putchar_have_lock()                 │
 └─────────────────────────────────────────────────┘
                       │
         ┌─────────────┴─────────────┐
         ▼                           ▼
 ┌───────────────┐          ┌───────────────┐
 │  serial_putc()│          │  vga_putc()   │
 │  devices/     │          │  devices/     │
 │  serial.c:94  │          │  vga.c        │
 └───────┬───────┘          └───────────────┘
         │                    (VGA display)
         ▼
 ┌─────────────────────────────────────────────────┐
 │           16550A UART DRIVER                     │
 ├─────────────────────────────────────────────────┤
 │                                                  │
 │   Mode?  ──► POLL: putc_poll() - busy wait      │
 │          ──► QUEUE: intq_putc() + interrupt     │
 │                                                  │
 │   Transmit Queue (struct intq txq)              │
 │   ┌───┬───┬───┬───┬───┬───┬───┬───┐            │
 │   │ h │ i │ \n│   │   │   │   │   │  ──► FIFO  │
 │   └───┴───┴───┴───┴───┴───┴───┴───┘            │
 └─────────────────────────────────────────────────┘
                       │
                       ▼
 ┌─────────────────────────────────────────────────┐
 │         UART HARDWARE REGISTERS                  │
 │         I/O Port: 0x3F8 (COM1)                  │
 │                                                  │
 │  outb(THR_REG, byte)  ───► Transmit byte        │
 └─────────────────────────────────────────────────┘
                       │
                       │ Physical serial (emulated)
                       ▼
 ┌─────────────────────────────────────────────────┐
 │                    QEMU                          │
 │   Emulated 16550A  ────►  Host stdout/pty       │
 └─────────────────────────────────────────────────┘
                       │
                       ▼
 ┌─────────────────────────────────────────────────┐
 │              YOUR TERMINAL                       │
 │              $ pintos -- run mytest              │
 │              hi                                  │
 └─────────────────────────────────────────────────┘
```

### Step-by-Step Walkthrough

1. **User calls printf()**: The user program's `printf()` in `lib/user/stdio_fmt.c` formats the string and calls `write()`.

2. **System call**: `write(1, buf, n)` triggers `int $0x30` (x86) or `ecall` (RISC-V), transitioning to kernel mode.

3. **Syscall handler**: `syscall_handler()` in `userprog/syscall.c` processes `SYS_WRITE`. For fd=1 (stdout), it calls the kernel's console output.

4. **Console layer**: `lib/kernel/console.c` acquires `console_lock` to prevent interleaved output from multiple threads, then calls output functions.

5. **Dual output**: Characters go to both `serial_putc()` and `vga_putc()` for serial and screen output.

6. **UART transmission**: `serial_putc()` either busy-waits (polling) or queues the byte for interrupt-driven transmission.

---

## 3. Transmission Modes

### Polling Mode (Early Boot)

Used before interrupts are initialized. Simple but wastes CPU cycles.

```c
static void putc_poll(uint8_t byte) {
  ASSERT(intr_get_level() == INTR_OFF);

  while ((inb(LSR_REG) & LSR_THRE) == 0)
    continue;              /* Busy-wait until TX ready */
  outb(THR_REG, byte);     /* Send the byte */
}
```

Source: `devices/serial.c:180-186`

### Interrupt-Driven Mode (Normal Operation)

After `serial_init_queue()` is called, characters are queued and transmitted via IRQ 4.

```c
void serial_putc(uint8_t byte) {
  enum intr_level old_level = intr_disable();

  if (mode != QUEUE) {
    /* Polling mode */
    if (mode == UNINIT)
      init_poll();
    putc_poll(byte);
  } else {
    /* Queue the byte and enable TX interrupt */
    intq_putc(&txq, byte);
    write_ier();
  }

  intr_set_level(old_level);
}
```

Source: `devices/serial.c:94-120`

### Mode Transition

```
UNINIT ──► init_poll() ──► POLL ──► serial_init_queue() ──► QUEUE
                                           │
                                    Registers IRQ 4
                                    (vector 0x24)
```

---

## 4. Input Flow: Terminal to Pintos

### ASCII Diagram

```
 Terminal ──► QEMU ──► UART RBR ──► serial_interrupt()
                        0x3F8        devices/serial.c:189
                                            │
                                            ▼
                                     input_putc()
                                     devices/input.c
                                            │
                                            ▼
                                     input buffer (intq)
                                            │
                                            ▼
                                     input_getc() ◄── keyboard/shell read
```

### Interrupt Handler

When a character arrives, the UART triggers IRQ 4:

```c
static void serial_interrupt(struct intr_frame* f UNUSED) {
  /* Acknowledge interrupt */
  inb(IIR_REG);

  /* Receive all available bytes */
  while (!input_full() && (inb(LSR_REG) & LSR_DR) != 0)
    input_putc(inb(RBR_REG));

  /* Transmit queued bytes */
  while (!intq_empty(&txq) && (inb(LSR_REG) & LSR_THRE) != 0)
    outb(THR_REG, intq_getc(&txq));

  write_ier();
}
```

Source: `devices/serial.c:189-206`

---

## 5. Configuration

### Baud Rate Setting

Pintos configures 9600 bps, 8 data bits, no parity, 1 stop bit (8N1):

```c
static void set_serial(int bps) {
  int base_rate = 1843200 / 16;       /* Base rate of 16550A */
  uint16_t divisor = base_rate / bps;

  /* Enable DLAB to access divisor registers */
  outb(LCR_REG, LCR_N81 | LCR_DLAB);

  /* Set divisor */
  outb(LS_REG, divisor & 0xff);
  outb(MS_REG, divisor >> 8);

  /* Disable DLAB, set 8N1 format */
  outb(LCR_REG, LCR_N81);
}
```

Source: `devices/serial.c:142-157`

### Interrupt Enable Register

The IER controls which events trigger interrupts:

```c
static void write_ier(void) {
  uint8_t ier = 0;

  /* Enable TX interrupt if we have data to send */
  if (!intq_empty(&txq))
    ier |= IER_XMIT;

  /* Enable RX interrupt if buffer has room */
  if (!input_full())
    ier |= IER_RECV;

  outb(IER_REG, ier);
}
```

Source: `devices/serial.c:160-176`

---

## 6. Key Data Structures

### Transmit Queue

```c
static struct intq txq;   /* Interrupt-safe queue for TX bytes */
```

The `intq` (interrupt queue) is a circular buffer that safely handles producer/consumer access between interrupt handlers and regular code.

### Transmission Mode

```c
static enum { UNINIT, POLL, QUEUE } mode;
```

| Mode | Description |
|------|-------------|
| UNINIT | Not yet initialized |
| POLL | Busy-wait transmission (early boot) |
| QUEUE | Interrupt-driven transmission |

---

## 7. QEMU Integration

When running Pintos in QEMU, the emulator provides a virtual 16550A UART that bridges to the host system:

```bash
# QEMU command (simplified)
qemu-system-i386 -serial stdio ...
```

- **Output**: Bytes written to THR appear on your terminal's stdout
- **Input**: Terminal keystrokes appear in the UART's RBR register
- **Interrupts**: QEMU emulates IRQ 4 for the serial port

---

## 8. Debugging Tips

### Check if serial is working

Add early debug output:

```c
// In kernel main(), before interrupts enabled
serial_putc('!');  // Should appear in terminal
```

### Monitor transmit queue

```c
printf("TX queue: %s\n", intq_empty(&txq) ? "empty" : "has data");
```

### Common issues

| Symptom | Cause | Fix |
|---------|-------|-----|
| No output | Serial not initialized | Ensure `serial_init_queue()` called |
| Garbled output | Baud rate mismatch | Check QEMU serial settings |
| Missing chars | Buffer overflow | Check `intq` capacity |
| Hung output | TX interrupt disabled | Check `write_ier()` logic |

---

## 9. Source File Reference

| File | Purpose |
|------|---------|
| `devices/serial.c` | UART driver implementation |
| `devices/serial.h` | Public interface (`serial_putc`, etc.) |
| `devices/input.c` | Input buffer management |
| `devices/intq.c` | Interrupt-safe queue |
| `lib/kernel/console.c` | High-level console output |
| `lib/user/syscall.c` | User-space `write()` wrapper |
