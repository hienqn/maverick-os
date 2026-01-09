# I/O Patterns and Concepts in Operating Systems

*A journey from "please wait" to "don't call us, we'll call you"*

---

## The Fundamental Problem

Picture yourself at a restaurant. You're hungry, you order food, and now you wait. But how do you wait?

You could sit there staring at the kitchen door, doing nothing else until your food arrives. Simple, but wasteful of your time. Or you could read a book, occasionally glancing up to check if the waiter is coming. Better, but you're still checking constantly. Or the restaurant could give you a buzzer that vibrates when your order is ready—now you can wander around the mall, truly free until that buzz.

This is the I/O problem in computing. A process needs data from a device—a disk, a network, a keyboard. The device is *slow*. The CPU is *fast*. What does the process do while waiting?

The history of I/O in operating systems is the story of increasingly clever answers to this question. We've gone from "just wait" to "keep checking" to "tell me when ready" to "just do it for me." Each step represents a fundamental shift in how we think about waiting.

---

## Table of Contents

1. [The Vocabulary of Waiting](#1-the-vocabulary-of-waiting)
2. [Blocking I/O: The Simple Life](#2-blocking-io-the-simple-life)
3. [Non-Blocking I/O: The Impatient Approach](#3-non-blocking-io-the-impatient-approach)
4. [I/O Multiplexing: The Patient Observer](#4-io-multiplexing-the-patient-observer)
5. [Signal-Driven I/O: The Interrupt](#5-signal-driven-io-the-interrupt)
6. [Asynchronous I/O: True Delegation](#6-asynchronous-io-true-delegation)
7. [Inside the Machine: Device Driver Patterns](#7-inside-the-machine-device-driver-patterns)
8. [Moving Data: Buffers and Transfers](#8-moving-data-buffers-and-transfers)
9. [Scaling Up: Event-Driven Architecture](#9-scaling-up-event-driven-architecture)
10. [The Modern Era: io_uring](#10-the-modern-era-io_uring)
11. [When Bits Hit Platters: Data Integrity](#11-when-bits-hit-platters-data-integrity)
12. [Pintos: Our Laboratory](#12-pintos-our-laboratory)
13. [Further Reading](#13-further-reading)

---

## 1. The Vocabulary of Waiting

Before we dive deeper, we need to clear up some terminology that trips up even experienced programmers. The words "blocking" and "synchronous" are often used interchangeably, but they mean different things.

**Blocking** answers the question: *Does this call make me wait?* When you call `read()` on an empty pipe, the call doesn't return until data appears. Your thread is blocked—it cannot do anything else. It's like standing in line; you're stuck until it's your turn.

**Synchronous** answers a different question: *When is the data transferred?* In synchronous I/O, the data moves during the call. When `read()` returns, the data is already in your buffer. The transfer happened right then and there.

Here's where it gets interesting: these concepts are orthogonal. You can have:

- **Blocking + Synchronous**: The classic `read()`. You wait, and when you stop waiting, you have data.
- **Non-blocking + Synchronous**: `read()` with `O_NONBLOCK`. If data is available, you get it immediately. If not, you get an error saying "try again later" (`EAGAIN`). But when you *do* get data, it transfers right then.
- **Asynchronous**: `aio_read()`. You submit a request and immediately continue. Later, you get notified that data has been placed in your buffer—the transfer happened in the background.

The key insight is this: everything from `select()` to `poll()` to `epoll()` is still synchronous. These fancy APIs tell you when data is *ready*, but you still call `read()` to get it. Only true async I/O (`aio_*`, `io_uring`) actually performs the transfer for you.

```
┌─────────────────────────────────────────────────────────────────────┐
│                                                                     │
│  BLOCKING + SYNCHRONOUS:     read() blocks, copies data, returns   │
│  NON-BLOCKING + SYNCHRONOUS: read() returns EAGAIN or copies data  │
│  ASYNCHRONOUS:               aio_read() returns, data copied later │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

With this vocabulary in place, let's trace the evolution of I/O patterns.

---

## 2. Blocking I/O: The Simple Life

In the beginning, there was blocking I/O, and it was... fine.

Imagine you're writing a simple program that reads a file and prints its contents. You call `read()`, the kernel fetches the data from disk, copies it to your buffer, and then—only then—does `read()` return. Your program resumes, prints the data, and everyone is happy.

```
Application              Kernel                  Hardware
     │                     │                        │
     │─── read(fd) ───────►│                        │
     │                     │─── request data ──────►│
     │     (sleeping)      │                        │
     │                     │      (disk spinning)   │
     │                     │◄── data ready ────────│
     │                     │    (copy to buffer)    │
     │◄─── data! ─────────│                        │
```

The beauty of blocking I/O is its simplicity. The program reads like a straightforward narrative: do this, then do that. There's no state machine to manage, no callbacks to wire up. It's just code, flowing from top to bottom.

```c
char buf[1024];
ssize_t n = read(fd, buf, sizeof(buf));
if (n > 0) {
    process(buf, n);
}
```

Three lines. Anyone can understand it. This simplicity is not to be dismissed—software engineering is largely about managing complexity, and blocking I/O manages complexity by eliminating it.

But there's a serpent in this garden.

**The Problem of Multiple Sources**

What if your program needs to read from two sources? Say, a keyboard and a network socket?

```c
read(keyboard_fd, buf1, size);  // Blocks until keypress...
read(network_fd, buf2, size);   // Never reached if user doesn't type!
```

If the user doesn't press any keys, the first `read()` blocks forever. Network packets pile up, unread. Your program is stuck.

The obvious solution—one thread per source—works, but threads aren't free. Each thread needs a stack (typically 1-8 MB), context switches have overhead, and synchronization between threads invites bugs. When you have thousands of connections (think: a web server), you can't afford thousands of threads.

We need something cleverer.

---

## 3. Non-Blocking I/O: The Impatient Approach

What if `read()` never blocked? What if, when there was no data, it just said "nothing yet" and returned immediately?

That's non-blocking I/O. You set a flag (`O_NONBLOCK`), and suddenly `read()` becomes impatient. If data is available, you get it. If not, you get a special error: `EAGAIN` (or equivalently, `EWOULDBLOCK`). The name says it all: "again"—try again later.

```c
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);

ssize_t n = read(fd, buf, sizeof(buf));
if (n == -1 && errno == EAGAIN) {
    // Not an error! Just no data right now.
    // I'll check back later.
}
```

Now you can check multiple sources without getting stuck:

```c
while (running) {
    if ((n = read(keyboard_fd, buf, size)) > 0)
        handle_keyboard(buf, n);

    if ((n = read(network_fd, buf, size)) > 0)
        handle_network(buf, n);

    do_other_work();
}
```

This is called **polling**—repeatedly checking whether something is ready. It's a step forward: at least we're not stuck. But we've traded one problem for another.

**The Polling Tax**

If you poll too often, you waste CPU cycles checking things that haven't changed. If you poll too rarely, you miss events and introduce latency. You can add `usleep()` between checks, but now you're guessing at the right interval.

```c
// Too aggressive: wastes CPU
while ((n = read(fd, buf, size)) == -1 && errno == EAGAIN)
    ;  // spin spin spin

// Too lazy: adds latency
while ((n = read(fd, buf, size)) == -1 && errno == EAGAIN)
    usleep(100000);  // 100ms delay—might miss time-sensitive data
```

Worse, with many file descriptors, you're calling `read()` on each one, most returning `EAGAIN`. The CPU burns cycles asking "is there data? no. is there data? no. is there data? no."

There must be a better way. What if, instead of asking each source individually, we could ask the kernel to watch them all and tell us which ones are ready?

---

## 4. I/O Multiplexing: The Patient Observer

I/O multiplexing flips the polling model on its head. Instead of the application checking each file descriptor, the kernel watches them all and reports back: "Here are the ones with data."

Think of it like a security guard monitoring multiple screens. The guard doesn't walk to each camera location—they sit in front of a wall of monitors and notice which ones show activity.

### select(): The Original

The `select()` system call, born in BSD Unix in 1983, was the first multiplexer. You give it a set of file descriptors, and it blocks until at least one is ready.

```c
fd_set read_fds;
FD_ZERO(&read_fds);
FD_SET(keyboard_fd, &read_fds);
FD_SET(network_fd, &read_fds);

// This blocks until SOMETHING is ready
int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

// Now check which ones are ready
if (FD_ISSET(keyboard_fd, &read_fds)) {
    read(keyboard_fd, buf, size);  // Won't block!
}
if (FD_ISSET(network_fd, &read_fds)) {
    read(network_fd, buf, size);   // Won't block!
}
```

This is a paradigm shift. The blocking has moved from the I/O operation to the *waiting for readiness*. And critically, we wait for *any* of many sources to become ready, not each one individually.

But `select()` has limitations. The file descriptor sets are bitmaps with a fixed maximum (usually 1024 fds). Every call requires copying the entire set into the kernel and scanning every bit. With thousands of connections, this overhead becomes brutal.

### poll(): Minor Improvements

`poll()` (1986) replaced bitmaps with an array of structures, removing the 1024 limit. But it still scans the entire array every time.

```c
struct pollfd fds[3];
fds[0] = (struct pollfd){ .fd = keyboard_fd, .events = POLLIN };
fds[1] = (struct pollfd){ .fd = network_fd,  .events = POLLIN };
fds[2] = (struct pollfd){ .fd = disk_fd,     .events = POLLIN };

int ready = poll(fds, 3, timeout_ms);

for (int i = 0; i < 3; i++) {
    if (fds[i].revents & POLLIN) {
        read(fds[i].fd, buf, size);
    }
}
```

### epoll(): The Revolution

By the late 1990s, people were building web servers that needed to handle 10,000 simultaneous connections—the famous "C10K problem." Both `select()` and `poll()` scanned every file descriptor on every call, making them O(n) where n is the total number of connections. But typically, only a handful have activity at any moment.

Linux's `epoll` (2002) changed everything. It splits registration from waiting. You add file descriptors once; the kernel maintains the list. When an event occurs (a network interrupt, say), the kernel adds that fd to a ready list. When you call `epoll_wait()`, it returns only the ready ones—O(ready), not O(total).

```c
// Create epoll instance (do once)
int epfd = epoll_create1(0);

// Register file descriptors (do once per fd)
struct epoll_event ev = { .events = EPOLLIN, .data.fd = fd };
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

// Wait loop
struct epoll_event events[MAX_EVENTS];
while (running) {
    int n = epoll_wait(epfd, events, MAX_EVENTS, timeout);

    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        handle_event(fd);  // Only ready fds!
    }
}
```

The internal structure tells the story:

```
SELECT/POLL                          EPOLL
─────────────                        ─────

Every call: scan ALL fds             Interest List (Red-Black Tree)
                                     ┌───────┐
┌───┬───┬───┬───┬───┐               │ fd=5  │
│ ? │ ? │ ? │ ? │ ? │               ├───────┤
└───┴───┴───┴───┴───┘               │ fd=42 │
  ↑ check each one                  └───────┘
  O(n) every time!
                                     Ready List (Linked List)
                                     ┌───────┐
                                     │ fd=42 │ ← IRQ added this
                                     └───────┘

                                     Return only ready: O(ready)!
```

### Edge-Triggered vs. Level-Triggered

`epoll` introduces another concept that deserves attention: *when* to notify.

**Level-triggered** (the default) means "the fd IS readable"—a state. As long as there's data in the buffer, `epoll_wait()` returns that fd. Read some data, and if there's still more, it returns again.

**Edge-triggered** (`EPOLLET`) means "new data ARRIVED"—an event. The kernel notifies you once when data arrives. If you don't read all of it, too bad—you won't be notified again until *more* data arrives.

```
Level-triggered:                     Edge-triggered:

Buffer: [████████]                   Buffer: [████████]
  epoll_wait returns                   epoll_wait returns ONCE

You read 4 bytes                     You read 4 bytes
Buffer: [████░░░░]                   Buffer: [████░░░░]
  epoll_wait returns AGAIN             NO notification! Must drain!
```

Edge-triggered is more efficient (fewer wakeups) but dangerous—if you don't drain the buffer, data sits there forever. You must read until `EAGAIN`:

```c
// Edge-triggered: MUST read until EAGAIN
while ((n = read(fd, buf, sizeof(buf))) > 0) {
    process(buf, n);
}
if (n == -1 && errno != EAGAIN)
    handle_error();
```

### The Thundering Herd

One more subtlety: what happens when many threads wait on the same socket? Picture 100 threads blocking in `epoll_wait()`, all interested in the listen socket. A connection arrives. How many wake up?

With naive implementations, all 100. They race to call `accept()`. One succeeds, 99 fail with EAGAIN and go back to sleep. This is the **thundering herd**—massive wasted wakeups.

Solutions include `EPOLLEXCLUSIVE` (Linux 4.5+), which wakes only one thread per event, and `SO_REUSEPORT`, which gives each thread its own socket that hashes incoming connections.

### The Limits of Multiplexing

I/O multiplexing is powerful, but notice what it gives you: *readiness notification*. When `epoll_wait()` returns, it says "fd 42 is readable." You still call `read()` yourself. That `read()` is synchronous—it copies data from kernel to user space, and your thread waits during the copy.

For truly asynchronous I/O—where the kernel does the work and notifies you when it's *done*, not just *ready*—we need to go further.

---

## 5. Signal-Driven I/O: The Interrupt

Before `epoll`, Unix had another notification mechanism: signals. You could ask the kernel to send `SIGIO` when a file descriptor became ready.

```c
void sigio_handler(int sig) {
    // We got interrupted! Something is ready.
    char buf[1024];
    read(fd, buf, sizeof(buf));
    process(buf);
}

signal(SIGIO, sigio_handler);
fcntl(fd, F_SETOWN, getpid());
fcntl(fd, F_SETFL, O_ASYNC | O_NONBLOCK);

// Now go about your business...
// You'll be interrupted when data arrives
```

This is truly notification-driven: no polling, no blocking on a wait call. The kernel literally interrupts your program to say "data's here."

But signal handling is treacherous. Signals can arrive at any time, interrupting any code. What if you're in the middle of `malloc()` and the signal handler calls `malloc()`? Reentrant functions are rare; most library functions are not signal-safe. You end up with a tiny set of safe operations in handlers, usually just setting a flag for the main loop to check.

Signal-driven I/O is rarely used today. `epoll` gives similar benefits without the reentrant nightmares.

---

## 6. Asynchronous I/O: True Delegation

All the mechanisms so far—blocking, non-blocking, multiplexing, signals—share a common thread: the data copy is synchronous. When `read()` returns, your buffer has data. Someone copied those bytes, and that someone was your thread.

True asynchronous I/O breaks this assumption. You say "I want data from here, put it there, and tell me when you're done." Then you walk away. The kernel—often with help from DMA hardware—does the work entirely in the background. When it's done, you get a notification.

### POSIX AIO

The POSIX `aio_*` API (1993) was an early attempt:

```c
struct aiocb cb = {
    .aio_fildes = fd,
    .aio_buf = buffer,
    .aio_nbytes = size,
    .aio_offset = 0,
};

aio_read(&cb);  // Returns immediately!

// Do other work while kernel reads...
render_graphics();
play_audio();
compute_physics();

// Check if done
while (aio_error(&cb) == EINPROGRESS) {
    do_more_work();
}

ssize_t n = aio_return(&cb);
// Data is now in buffer!
```

The timeline tells the story:

```
With synchronous I/O (even non-blocking):
─────────────────────────────────────────
  [submit]──[wait for ready]──[read() copies data]──[process]
                               └── CPU busy here!

With async I/O:
───────────────
  [submit]──[do whatever you want]──────────[notification]──[process]
            └── CPU truly free ──┘           └── data already copied!
```

The difference is subtle but profound. With multiplexing, you wait for *readiness*, then *you* perform the transfer. With async I/O, you wait for *completion*—the transfer already happened.

In practice, POSIX AIO was poorly implemented (often using a thread pool internally, defeating the purpose). Most programs continued using `epoll` or threading. It took two decades for Linux to get truly excellent async I/O.

---

## 7. Inside the Machine: Device Driver Patterns

We've talked about I/O from the application's perspective. But what happens inside the kernel? When a disk read completes or a network packet arrives, how does that turn into waking up a waiting process?

### How Data Physically Moves: PIO vs. DMA

Before understanding driver patterns, understand how bytes actually move between devices and memory.

**Programmed I/O (PIO)** means the CPU moves every byte. Reading a 512-byte disk sector? The CPU executes 256 16-bit input instructions, each one reading two bytes from the device's data register into a CPU register, then storing to memory. The CPU is 100% busy during the transfer.

```c
// PIO: CPU does all the work
static void input_sector(struct channel* c, void* sector) {
    insw(reg_data(c), sector, BLOCK_SECTOR_SIZE / 2);
    // CPU executes 256 'insw' instructions
    // Each reads 2 bytes from device, writes to memory
}
```

**Direct Memory Access (DMA)** offloads this work. The CPU tells the DMA controller: "read 512 bytes from this device to this memory address." Then the CPU does something else while a separate piece of hardware—the DMA controller—does the actual copying. When it's done, an interrupt fires.

```
PIO:                                 DMA:
────                                 ────

CPU ◄─► Device                       CPU ──► DMA Controller ──► Device
 ↓                                    ↓           ↓
Memory                               (free!)    Memory

CPU busy during entire transfer      CPU submits one command, then free
```

DMA is what makes async I/O meaningful. If the CPU had to do the copying anyway, async I/O would just be hiding a busy thread. With DMA, the CPU genuinely isn't involved—hardware does the transfer.

Pintos uses PIO for simplicity. Real operating systems use DMA for everything they can.

### Interrupt-Driven I/O

When a process requests data from disk, what happens? The driver:

1. Sends a command to the hardware ("read sector 42")
2. Blocks the thread (moves it to a wait queue)
3. Returns control to the scheduler, which runs other threads
4. Later, hardware finishes and fires an **interrupt**
5. The interrupt handler wakes the sleeping thread
6. The thread resumes, and the operation returns

This is the heartbeat of modern I/O. The process sleeps; hardware works; an interrupt wakes the process. No busy-waiting, no polling. The CPU runs other code while waiting.

### Top Half and Bottom Half

But interrupt handlers have a problem: they run with interrupts *disabled*. While you're handling one interrupt, others are blocked. Take too long, and you'll miss events or cause latency spikes.

The solution is to split interrupt handling into two phases:

**Top half** (the interrupt handler itself): Do the absolute minimum. Acknowledge the hardware, grab urgent data, schedule the bottom half. Get out fast—microseconds, not milliseconds.

**Bottom half** (deferred work): Do the complex processing later, with interrupts enabled. Parse protocols, copy data, wake processes. Take as long as you need—you're not blocking other interrupts.

```
┌─────────────────────────────────────────────────────────────────┐
│  TOP HALF (Interrupt Context)                                   │
│  ────────────────────────────                                   │
│  • Interrupts DISABLED                                          │
│  • Must be FAST (microseconds)                                  │
│  • Cannot sleep or allocate memory                              │
│                                                                 │
│  Tasks: Ack hardware, read urgent data, schedule bottom half   │
└───────────────────────────────────┬─────────────────────────────┘
                                    │ schedule
                                    ▼
┌─────────────────────────────────────────────────────────────────┐
│  BOTTOM HALF (Process/Thread Context)                           │
│  ────────────────────────────────────                           │
│  • Interrupts ENABLED                                           │
│  • Can take longer                                              │
│  • Can sleep (in some implementations)                          │
│                                                                 │
│  Tasks: Protocol processing, data copying, wake waiting threads│
└─────────────────────────────────────────────────────────────────┘
```

This is the pattern we implemented in `kbd.c`! The keyboard interrupt handler does almost nothing—reads the scancode from hardware and drops it in a buffer. A worker thread processes the scancodes later, with interrupts enabled, at its leisure.

### Solicited vs. Unsolicited I/O

One more distinction matters for understanding buffers: *who initiated the transfer?*

**Solicited I/O** (disk reads, most network sends): The application asked for this specific data. A thread is waiting for exactly this response. No buffering needed—data goes directly to the waiter's buffer.

**Unsolicited I/O** (keyboard, incoming network packets): Data arrives whenever the *device* decides. The application might not be looking right now. You must buffer the data until someone asks for it.

```
Unsolicited (keyboard):
  User types 'H'
  IRQ ──► put in buffer ──► (milliseconds pass) ──► app calls read()
         └── must remember 'H' ──────────────────────► return 'H'

Solicited (disk):
  App calls read(sector=42)
  App sleeps ──► disk works ──► IRQ wakes app ──► data goes to app's buffer
                                └── no intermediate buffer needed!
```

This is why the keyboard driver has a buffer and the disk driver doesn't. The keyboard's data is a surprise; the disk's data is a response to a specific request.

---

## 8. Moving Data: Buffers and Transfers

The fastest I/O is no I/O at all. The second fastest is I/O that touches data as few times as possible. Let's talk about how data moves and how to move it efficiently.

### The Ring Buffer

When data arrives at unpredictable times, you need a buffer. The **ring buffer** (circular buffer) is ubiquitous: keyboards, serial ports, network cards, audio systems—all use it.

```
    ┌───┬───┬───┬───┬───┬───┬───┬───┐
    │   │ A │ B │ C │ D │   │   │   │
    └───┴───┴───┴───┴───┴───┴───┴───┘
          ▲               ▲
         tail           head
         (read)        (write)
```

Producer writes at head, consumer reads at tail. Both pointers wrap around when they hit the end. The buffer is full when head catches up to tail; empty when they're equal.

The beauty is that producer and consumer can operate independently—no copying needed, just moving pointers. In Pintos, `struct intq` is a ring buffer used by the keyboard and serial drivers.

### The Cost of Copies

Consider a web server sending a file to a client. With naive code:

```c
read(file_fd, buf, len);        // Copy 1: Disk → Page Cache → User buffer
write(socket_fd, buf, len);     // Copy 2: User buffer → Socket buffer
                                // Copy 3: Socket buffer → NIC (via DMA)
```

That's three copies and multiple context switches. The data went: disk → kernel → user → kernel → NIC. But it passed through user space just to immediately go back to the kernel!

**Zero-copy I/O** eliminates this redundancy. Linux's `sendfile()` transfers directly from page cache to network card:

```c
sendfile(socket_fd, file_fd, &offset, len);
// Data flow: Disk → Page Cache → NIC
// User space never touched!
```

With scatter-gather DMA, it can even avoid the kernel-side copy. The NIC's DMA engine reads directly from the page cache buffers. True zero-copy.

This is how nginx serves millions of requests per second on a single machine. It's not magic—it's avoiding unnecessary work.

### Memory-Mapped Files

What if files were just... memory? `mmap()` makes a file part of your address space. Read the memory, and page faults load from disk. Write to the memory, and dirty pages flush back.

```c
char *ptr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

ptr[0] = 'X';  // Page fault loads data, then write happens
               // No read() or write() system calls!

munmap(ptr, size);  // Dirty pages written back
```

Databases love this. PostgreSQL maps its buffer cache, accessing files as arrays. No `read()`/`write()` per page—just memory references that the kernel handles.

### Scatter/Gather I/O

Network protocols often have fixed headers and variable bodies. Naive code either copies them together or makes two system calls:

```c
// Option 1: Copy (wastes CPU)
memcpy(buf, header, header_len);
memcpy(buf + header_len, body, body_len);
write(fd, buf, header_len + body_len);

// Option 2: Two writes (wastes syscalls)
write(fd, header, header_len);
write(fd, body, body_len);
```

`writev()` gathers multiple buffers into one atomic write:

```c
struct iovec iov[2] = {
    { .iov_base = header, .iov_len = header_len },
    { .iov_base = body,   .iov_len = body_len }
};
writev(fd, iov, 2);  // One syscall, no copying!
```

Similarly, `readv()` scatters incoming data across multiple buffers. Parse a protocol by pointing vectors at header and body separately.

---

## 9. Scaling Up: Event-Driven Architecture

Armed with `epoll` and non-blocking I/O, a pattern emerges: the **event loop**. Instead of threads blocking on each connection, a single thread multiplexes over all connections, dispatching to handlers as events occur.

```c
while (running) {
    // Wait for any event
    int n = epoll_wait(epfd, events, MAX_EVENTS, timeout);

    // Dispatch each event
    for (int i = 0; i < n; i++) {
        handler_t handler = get_handler(events[i].data.fd);
        handler(events[i]);
    }
}
```

This is the **reactor pattern**: a demultiplexer (epoll) watches for readiness, then dispatches to handlers that perform the actual I/O. Redis, nginx, and Node.js all use this. One thread handling 10,000+ connections, never blocking.

The companion is the **proactor pattern**, where the kernel performs the I/O and notifies you of *completion*. Windows IOCP works this way, and Linux's `io_uring` brings it to Linux.

### The C10K Problem

In 1999, Dan Kegel asked: how do we handle 10,000 concurrent connections? Thread-per-connection was dying (too much memory, too much context-switching). The answer was event-driven architectures with efficient multiplexing.

| Era | Approach | Connections |
|-----|----------|-------------|
| 1990s | Thread per connection | ~1,000 |
| 2000s | select/poll | ~10,000 |
| 2010s | epoll/kqueue | ~100,000 |
| 2020s | io_uring | ~1,000,000+ |

Today we talk about the C10M problem—ten million connections. The techniques keep evolving.

---

## 10. The Modern Era: io_uring

Linux's `io_uring` (2019) represents the current state of the art. It's a true asynchronous interface that works for any file descriptor, batches operations efficiently, and minimizes system calls.

The insight is shared memory. Application and kernel share two ring buffers: a **submission queue** (SQ) where the app writes requests, and a **completion queue** (CQ) where the kernel writes results. The app can submit operations by just writing to memory—no system call needed! The kernel processes them asynchronously and posts completions.

```
┌─────────────────────────────────────────────────────────────────┐
│                     SHARED MEMORY                               │
│                                                                 │
│    Submission Queue (SQ)         Completion Queue (CQ)         │
│    ┌───┬───┬───┬───┐            ┌───┬───┬───┬───┐              │
│    │ op1│op2│   │   │            │res│   │   │   │              │
│    └───┴───┴───┴───┘            └───┴───┴───┴───┘              │
│         │                              ▲                        │
│         └──────────► KERNEL ───────────┘                        │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘

• App writes to SQ (no syscall)
• Kernel processes, writes to CQ
• App reads from CQ (no syscall)
```

```c
struct io_uring ring;
io_uring_queue_init(32, &ring, 0);

// Submit a read
struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_read(sqe, fd, buf, len, offset);
io_uring_submit(&ring);

// Wait for completion
struct io_uring_cqe *cqe;
io_uring_wait_cqe(&ring, &cqe);

// Data is already in buf!
printf("Read %d bytes\n", cqe->res);
io_uring_cqe_seen(&ring, cqe);
```

`io_uring` supports linking operations (read then write), fixed buffers (avoid re-registration), polling mode (zero syscalls), and just about every I/O operation Linux supports. It's the foundation of next-generation servers and databases.

---

## 11. When Bits Hit Platters: Data Integrity

Here's a question that terrifies database developers: when `write()` returns, where is my data?

The answer is unsettling: probably in RAM.

### The Write Path

When you call `write()`, the kernel copies data to the **page cache**—a kernel memory buffer. The call returns. Your data is in RAM. It's not on disk. If power fails now, your data is gone.

The kernel eventually flushes dirty pages to disk—maybe in 30 seconds, maybe when memory is tight. This write-back caching is essential for performance (writes are batched, seeks are minimized), but it means "write returned" doesn't mean "data is safe."

```
write(fd, data, len);   // Returns here—data is in page cache (RAM)

// ... 30 seconds pass ...

// Kernel flushes dirty pages ─────► Data reaches disk platter
```

### fsync(): The Durability Barrier

`fsync(fd)` forces all buffered data for that file to disk. It doesn't return until the data—and the metadata (size, timestamps)—has reached the physical medium. If `fsync()` returns and then power fails, your data survives.

```c
write(fd, important_data, len);
fsync(fd);  // Blocks until data is on platter!
// Now safe against power failure
```

`fdatasync()` is a slightly cheaper variant that syncs data but not all metadata. `O_SYNC` and `O_DSYNC` make every write synchronous, essentially inserting a sync after each write.

### The Database Pattern

Databases are paranoid about this. They typically use a write-ahead log (WAL): before modifying actual data pages, write a log record describing the change and `fsync()` it. If crash occurs, replay the log to reconstruct the state.

```c
write(log_fd, transaction_record, len);
fsync(log_fd);  // Log is safe
write(data_fd, modified_page, len);
// Don't need to fsync data—can recover from log
```

This lets you batch data page writes while still guaranteeing durability through the log.

---

## 12. Pintos: Our Laboratory

How does Pintos, our educational OS, fit into all this?

Pintos is deliberately simple. It uses blocking I/O everywhere. There's no `O_NONBLOCK`, no `select()`, no async I/O. When you call `input_getc()` and there's no keystroke, you sleep until one arrives. When you call `block_read()`, you wait until the disk finishes.

### The Keyboard: Unsolicited I/O

The keyboard implementation shows the buffering pattern for unsolicited I/O:

```c
// In interrupt handler (top half)
input_putc(character);  // Put keystroke in ring buffer

// In application (via syscall)
char c = input_getc();  // Blocks if buffer empty, returns oldest keystroke
```

With our top/bottom half modification in `kbd.c`, we split this further: the interrupt handler just buffers raw scancodes; a worker thread translates them to characters.

### The Disk: Solicited I/O

Disk I/O is solicited—a thread asks for a specific block:

```c
// In block_read():
sema_down(&channel->completion_wait);  // Sleep
send_command(channel, sector);         // Tell disk what we want
                                       // ... disk works ...
// Interrupt handler calls sema_up()
sema_down returns                      // We wake up
copy_sector_to_buffer();               // Get the data
```

No intermediate buffer needed—data goes directly to the caller's buffer.

### What Pintos Lacks

For an educational OS, these are features, not bugs:

- No non-blocking I/O (`O_NONBLOCK`, `EAGAIN`)
- No multiplexing (`select()`, `poll()`, `epoll()`)
- No async I/O
- No DMA (uses PIO—CPU does all transfers)
- No `fsync()` (no durability guarantees)
- No `mmap()` for files (though we have anonymous mmap)
- No device files (`/dev/*`)

The VFS plan would add the device file abstraction, creating a unified interface over the existing drivers.

### The timer_sleep() Analogy

An interesting parallel: `timer_sleep()` demonstrates blocking I/O's transformation from busy-wait to interrupt-driven.

Naive implementation:
```c
void timer_sleep_busy(int64_t ticks) {
    int64_t start = timer_ticks();
    while (timer_elapsed(start) < ticks)
        thread_yield();  // Spin!
}
```

Better implementation:
```c
void timer_sleep(int64_t ticks) {
    add_to_sleeping_list(current_thread, timer_ticks() + ticks);
    thread_block();  // Sleep until woken by timer interrupt
}

// In timer interrupt handler:
void timer_interrupt() {
    ticks++;
    wake_threads_whose_time_has_come();
}
```

Same pattern as disk I/O: thread sleeps, hardware fires interrupt, handler wakes thread. The essence of interrupt-driven I/O.

---

## 13. Further Reading

### Books

If this whetted your appetite:

- **"Linux Device Drivers"** (Corbet et al.) — Free online, the definitive guide to kernel-side I/O
- **"UNIX Network Programming"** (Stevens) — The bible of I/O multiplexing
- **"Understanding the Linux Kernel"** (Bovet & Cesati) — Deep dive into Linux internals
- **"Operating System Concepts"** (Silberschatz) — The academic standard, good on theory

### Papers

- **"The C10K Problem"** (Kegel, 1999) — The original scaling challenge
- **"Efficient Scheduling of Asynchronous I/O"** (Axboe, 2019) — io_uring design

### Online

- **Lord of the io_uring** (unixism.net/loti) — Excellent io_uring tutorial
- **Beej's Guide to Network Programming** — Practical multiplexing examples
- **LWN.net** — Deep kernel development articles

---

## Summary: The Journey

We've traveled from the simplest possible I/O model to the most sophisticated:

```
BLOCKING I/O
  └─ "Just wait"
     Simple, but threads sit idle

NON-BLOCKING I/O
  └─ "Return immediately"
     Never stuck, but must poll

I/O MULTIPLEXING
  └─ "Tell me which are ready"
     Wait efficiently on many sources

ASYNC I/O
  └─ "Do it for me"
     True parallelism, kernel does the work
```

Each step solved problems of the previous, at the cost of complexity. Blocking I/O is still right for simple programs. Non-blocking with epoll powers most web servers. Async I/O with io_uring represents the bleeding edge.

The pattern repeats at every level: in applications (how to wait for I/O), in drivers (how to handle interrupts), in hardware (PIO vs DMA). The fundamental tension—fast CPUs, slow devices—drives all of it.

Now you understand not just *what* these patterns are, but *why* they exist and *when* to use each. Go build something.

---

## Appendix: Quick Reference

### System Calls at a Glance

| Call | Purpose | Blocks? |
|------|---------|---------|
| `read()` / `write()` | Basic I/O | Yes (unless O_NONBLOCK) |
| `select()` / `poll()` | Wait for readiness | Yes |
| `epoll_wait()` | Wait for readiness (scalable) | Yes |
| `aio_read()` / `aio_write()` | Async I/O | No |
| `io_uring_submit()` | Async I/O (modern) | No |
| `sendfile()` | Zero-copy file→socket | Yes |
| `mmap()` | Map file to memory | No (faults block) |
| `fsync()` | Flush to disk | Yes |

### Error Codes

| Code | Meaning |
|------|---------|
| `EAGAIN` / `EWOULDBLOCK` | Try again (no data now) |
| `EINPROGRESS` | Operation started |
| `EINTR` | Interrupted by signal |

### Key Flags

| Flag | Purpose |
|------|---------|
| `O_NONBLOCK` | Non-blocking mode |
| `O_SYNC` | Sync writes (every write is fsync) |
| `EPOLLET` | Edge-triggered epoll |
| `EPOLLEXCLUSIVE` | Wake only one waiter |

### Latency Numbers

| Operation | Time |
|-----------|------|
| L1 cache hit | 1 ns |
| RAM access | 100 ns |
| SSD read | 100 μs |
| HDD read | 10 ms |
| System call | 0.1-1 μs |
| Context switch | 1-10 μs |

---

*Document created for the Pintos educational operating system.*
*Covers I/O concepts from basic blocking I/O through modern async patterns.*
