# Pintos OS Feature Enhancement Proposals

**Author:** Claude
**Date:** December 27, 2025
**Status:** Proposal Phase

## Executive Summary

This document outlines proposed enhancements to the Pintos operating system. Currently, Pintos implements comprehensive threading (4 schedulers), user programs (28+ syscalls), and a sophisticated filesystem with caching. However, several major OS subsystems remain unimplemented, representing opportunities for significant feature additions.

**Current State:**
- ✅ Thread Management (FIFO, Priority, Fair, MLFQS schedulers)
- ✅ User Programs & Process Management
- ✅ Hierarchical Filesystem with Buffer Cache & Prefetching
- ✅ Basic Device Drivers (Timer, Disk, Keyboard, Serial)
- ❌ Virtual Memory (VM subsystem completely missing)
- ❌ Networking Stack (E1000 driver is stub only)
- ❌ Inter-Process Communication
- ❌ Security & Protection Features

---

## Priority 1: Virtual Memory Subsystem

**Status:** Not Started (VM directory contains build files only)
**Impact:** High - Enables memory efficiency, security, and advanced features
**Complexity:** High
**Estimated Lines:** ~2,500 lines

### Overview
Virtual memory is the foundation for modern OS memory management, enabling demand paging, memory isolation, and efficient resource sharing.

### Proposed Components

#### 1.1 Demand Paging
**Description:** Load pages from disk only when accessed, rather than loading entire programs.

**Key Features:**
- Page fault handler integration with exception system
- Lazy loading of executable segments
- Stack growth on demand (currently limited implementation)
- Zero-fill pages for uninitialized data

**Implementation Points:**
- Hook into `exception.c` for page fault interrupts (INT 14)
- Create supplemental page table alongside hardware page directory
- Track page locations: memory, swap, or filesystem
- Implement `page_fault()` handler to load/allocate pages

**Benefits:**
- Programs larger than physical memory can execute
- Faster startup times (don't load entire binary)
- Better memory utilization across multiple processes

#### 1.2 Page Replacement Policies
**Description:** Evict pages when physical memory is full.

**Proposed Algorithms:**
- **Clock/Second-Chance:** LRU approximation (similar to buffer cache)
- **LRU (Least Recently Used):** Track access times
- **Working Set:** Evict based on reference patterns
- **FIFO with Second Chance:** Simple baseline implementation

**Implementation Points:**
- Frame table tracking all physical pages
- Access bit management using x86 page table flags
- Eviction policy selector (similar to scheduler selection)

**Metrics to Track:**
- Page fault rate
- Eviction count
- Access bit hits
- Thrashing detection

#### 1.3 Swap Space Management
**Description:** Disk-backed storage for evicted pages.

**Key Features:**
- Bitmap-based swap slot allocation (similar to filesystem free blocks)
- Integration with block device layer
- Swap in/out operations
- Compressed swap (optional advanced feature)

**Implementation Points:**
- Swap partition/file creation
- Swap bitmap for free slot tracking
- Write evicted pages to swap
- Read pages back on fault
- Track swap slot in supplemental page table

#### 1.4 Memory-Mapped Files (mmap/munmap)
**Description:** Map file contents directly into process address space.

**System Calls:**
```c
void *mmap(int fd, void *addr, size_t length);
int munmap(void *addr);
```

**Use Cases:**
- Fast file I/O without read/write syscalls
- Shared memory between processes (map same file)
- Dynamic library loading
- Database buffer management

**Implementation Points:**
- Extend supplemental page table with file-backed pages
- Lazy-load pages from file on fault
- Write dirty pages back to file on unmap/eviction
- Shared mappings for read-only code/data

#### 1.5 Copy-on-Write (COW) for fork()
**Description:** Optimize process creation by sharing pages until writes occur.

**Benefits:**
- 10-100x faster fork() operations
- Reduced memory usage when child execs immediately
- Enables efficient process spawning

**Implementation Points:**
- Mark shared pages read-only in both parent and child
- On write fault, allocate new frame and copy
- Update page table to point to private copy
- Track reference counts for shared pages

#### 1.6 Advanced Features
- **Page Coloring:** Reduce cache conflicts by mapping virtual pages to specific physical frames
- **Superpages/Huge Pages:** 4MB pages for large allocations (reduces TLB misses)
- **NUMA Awareness:** Allocate memory from local node in multi-processor systems
- **Transparent Huge Pages:** Automatically promote small pages to huge pages

---

## Priority 2: Network Stack & Protocol Implementation

**Status:** E1000 driver stub exists (39 lines), no stack
**Impact:** High - Enables distributed systems, internet connectivity
**Complexity:** Very High
**Estimated Lines:** ~5,000-8,000 lines

### Overview
Networking transforms Pintos from a standalone OS into a connected system capable of client-server applications, distributed computing, and internet protocols.

### Proposed Components

#### 2.1 Ethernet Driver (E1000)
**Description:** Complete the Intel E1000 network interface driver.

**Key Features:**
- Ring buffer management for TX/RX queues
- DMA (Direct Memory Access) for efficient packet transfer
- Interrupt handling for packet arrival
- MAC address configuration
- Link status detection

**Implementation Points:**
- Initialize E1000 registers (MMIO-mapped)
- Set up transmit/receive descriptor rings
- Implement `e1000_transmit()` and `e1000_receive()`
- Handle TX/RX interrupts
- Buffer management for network packets

**Files to Create:**
- `src/devices/e1000.c` (expand from stub)
- `src/devices/e1000.h`

#### 2.2 Network Layer 2: Ethernet & ARP
**Description:** Handle Ethernet frames and address resolution.

**Components:**
- **Ethernet Frame Processing:**
  - Parse Ethernet headers (dest MAC, src MAC, EtherType)
  - Validate frame checksums
  - Demultiplex to upper layers (IP, ARP)

- **ARP (Address Resolution Protocol):**
  - Maintain ARP cache (IP → MAC mappings)
  - Send ARP requests for unknown addresses
  - Reply to ARP requests
  - Handle gratuitous ARP

**Implementation Points:**
- `net_rx_thread()` processes incoming packets
- ARP cache with expiration (similar to hash table)
- Broadcast ARP requests on local network

#### 2.3 Network Layer 3: IP (Internet Protocol)
**Description:** Implement IPv4 packet routing and forwarding.

**Key Features:**
- IP packet parsing and validation
- Header checksum calculation
- TTL (Time To Live) management
- IP fragmentation and reassembly
- Routing table for multi-interface systems
- ICMP (ping, traceroute)

**Implementation Points:**
- Parse IP headers (source, dest, protocol, flags)
- Validate checksums using Internet checksum algorithm
- Route packets: local delivery vs. forwarding
- Fragment large packets to fit MTU
- Reassemble fragments using fragment ID tracking

**Protocols:**
- IPv4 (full implementation)
- ICMP (echo request/reply, destination unreachable)
- IPv6 (optional future extension)

#### 2.4 Network Layer 4: TCP & UDP
**Description:** Implement reliable (TCP) and unreliable (UDP) transport.

##### UDP (User Datagram Protocol)
**Simpler Protocol - Implement First:**
- Connectionless, best-effort delivery
- Port-based demultiplexing
- Checksum validation
- Socket interface: `bind()`, `sendto()`, `recvfrom()`

**Implementation Points:**
- UDP socket table (port → process mapping)
- Receive queue per socket
- Simple packet delivery

##### TCP (Transmission Control Protocol)
**Full Implementation:**
- Connection-oriented with 3-way handshake
- Reliable delivery with retransmission
- Flow control (sliding window)
- Congestion control (TCP Reno/Cubic)
- Out-of-order packet handling

**State Machine:**
```
CLOSED → LISTEN → SYN_RECEIVED → ESTABLISHED
       ↓ SYN_SENT ↗

ESTABLISHED → FIN_WAIT_1 → FIN_WAIT_2 → TIME_WAIT → CLOSED
           ↓ CLOSE_WAIT → LAST_ACK ↗
```

**Key Features:**
- Sequence number tracking (send/receive windows)
- Retransmission timer (exponential backoff)
- Fast retransmit/fast recovery
- Nagle's algorithm for small packet coalescence
- Delayed ACKs

**Implementation Points:**
- TCP Control Block (TCB) per connection
- Retransmission queue
- Receive buffer with out-of-order handling
- Congestion window (cwnd) management
- Timer management for retransmits

#### 2.5 Socket API
**Description:** POSIX-compliant socket interface for user programs.

**System Calls:**
```c
int socket(int domain, int type, int protocol);
int bind(int sockfd, struct sockaddr *addr, int addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, int *addrlen);
int connect(int sockfd, struct sockaddr *addr, int addrlen);
ssize_t send(int sockfd, void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
int shutdown(int sockfd, int how);
int setsockopt(int sockfd, int level, int optname, void *optval, int optlen);
int getsockopt(int sockfd, int level, int optname, void *optval, int *optlen);
```

**Integration:**
- Add socket FD type to process FD table
- Socket-specific operations (read/write redirect to recv/send)
- Blocking/non-blocking I/O support
- Select/poll for multiplexing (optional)

#### 2.6 DNS Client (Optional)
**Description:** Domain name resolution for human-readable addresses.

**Features:**
- UDP-based DNS queries to resolver
- A record (IPv4) lookups
- DNS response caching
- Recursive vs. iterative resolution

**System Calls:**
```c
int gethostbyname(const char *name, struct hostent *result);
```

#### 2.7 Application Protocols (Examples)
Once sockets work, implement demo applications:
- **HTTP Server:** Simple web server serving static files
- **Telnet Server:** Remote shell access
- **FTP Client:** File transfer
- **NFS Client:** Network filesystem
- **DHCP Client:** Dynamic IP configuration

---

## Priority 3: Inter-Process Communication (IPC)

**Status:** Not Implemented
**Impact:** Medium - Enables process cooperation
**Complexity:** Medium
**Estimated Lines:** ~1,500 lines

### Overview
IPC mechanisms allow processes to communicate and synchronize, enabling modular system design and concurrent applications.

### Proposed Components

#### 3.1 Pipes
**Description:** Unidirectional byte streams between processes.

**Types:**
- **Anonymous Pipes:** Created with `pipe()`, used for parent-child communication
- **Named Pipes (FIFOs):** Filesystem entries, used between unrelated processes

**System Calls:**
```c
int pipe(int pipefd[2]);           // Create pipe (returns read/write FDs)
int mkfifo(const char *path);      // Create named pipe
```

**Implementation Points:**
- Pipe buffer (circular queue, 4KB default)
- Blocking read when empty, blocking write when full
- Reference counting: close when both ends closed
- Integration with FD table

**Use Cases:**
- Shell pipelines: `ls | grep foo | wc -l`
- Producer-consumer patterns
- Filter chains

#### 3.2 Message Queues
**Description:** Structured message passing with priorities.

**System Calls:**
```c
int msgget(key_t key, int flags);                    // Create/get queue
int msgsnd(int msgid, void *msgp, size_t size, int flags);
int msgrcv(int msgid, void *msgp, size_t size, long type, int flags);
int msgctl(int msgid, int cmd, struct msqid_ds *buf);
```

**Features:**
- Message types for selective reception
- Priority ordering
- Persistent queues (survive process exit)
- Size limits and permissions

**Implementation Points:**
- Global message queue table
- Per-queue message list (sorted by priority)
- Blocking send/receive with semaphores
- IPC permissions (similar to file permissions)

#### 3.3 Shared Memory
**Description:** Fast zero-copy communication via mapped pages.

**System Calls:**
```c
int shmget(key_t key, size_t size, int flags);      // Create segment
void *shmat(int shmid, void *addr, int flags);       // Attach to address space
int shmdt(void *addr);                                // Detach
int shmctl(int shmid, int cmd, struct shmid_ds *buf);
```

**Implementation Points:**
- Global shared memory segment table
- Map same physical frames to multiple page tables
- Copy-on-write protection (optional)
- Synchronization left to user (combine with semaphores)

**Use Cases:**
- High-performance data sharing
- Producer-consumer with large data
- Shared caches between processes

#### 3.4 Signals (POSIX-style)
**Description:** Asynchronous notifications to processes.

**System Calls:**
```c
int kill(pid_t pid, int sig);                       // Send signal
sighandler_t signal(int signum, sighandler_t handler);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigsuspend(const sigset_t *mask);
```

**Standard Signals:**
- `SIGINT`: Interrupt (Ctrl+C)
- `SIGTERM`: Termination request
- `SIGKILL`: Forced termination (uncatchable)
- `SIGSEGV`: Segmentation fault
- `SIGCHLD`: Child process status change
- `SIGALRM`: Timer expiration
- `SIGUSR1/SIGUSR2`: User-defined

**Implementation Points:**
- Per-thread signal mask
- Pending signal bitmap in thread structure
- User-space handler invocation (push context to user stack)
- Kernel-mode signal delivery on return from syscall/interrupt

**Use Cases:**
- Graceful shutdown (SIGTERM handler)
- Timeout implementation (SIGALRM)
- Asynchronous I/O completion notification

---

## Priority 4: Security & Protection Features

**Status:** Basic user/kernel separation exists
**Impact:** Medium - Improves security posture
**Complexity:** Medium
**Estimated Lines:** ~1,200 lines

### Proposed Components

#### 4.1 User Authentication & Access Control
**Description:** Multi-user system with login and permissions.

**Features:**
- **User Database:**
  - `/etc/passwd` style user list
  - User ID (UID) and Group ID (GID)
  - Home directories
  - Login shells

- **File Permissions:**
  - Extend inode with owner UID/GID
  - Permission bits: read/write/execute for owner/group/others
  - `chmod`, `chown`, `chgrp` syscalls
  - Permission checks on file operations

**System Calls:**
```c
int setuid(uid_t uid);
int getuid(void);
int setgid(gid_t gid);
int getgid(void);
int chmod(const char *path, mode_t mode);
int chown(const char *path, uid_t uid, gid_t gid);
```

**Implementation Points:**
- Add UID/GID fields to process structure
- Check permissions in filesystem operations
- Root user (UID 0) bypasses checks
- Setuid bit for privilege escalation (e.g., `passwd` command)

#### 4.2 Capabilities System
**Description:** Fine-grained privileges instead of all-or-nothing root.

**Capabilities:**
- `CAP_NET_ADMIN`: Network configuration
- `CAP_SYS_ADMIN`: System administration
- `CAP_CHOWN`: Change file ownership
- `CAP_KILL`: Send signals to any process
- `CAP_SYS_PTRACE`: Debug processes

**Benefits:**
- Principle of least privilege
- Containment of compromised services
- Safer privilege delegation

#### 4.3 Address Space Layout Randomization (ASLR)
**Description:** Randomize memory layout to mitigate exploits.

**Components:**
- Randomize stack base address
- Randomize heap base address
- Randomize shared library load addresses
- Randomize executable load address (PIE - Position Independent Executable)

**Implementation Points:**
- Use random number generator in `process_load()`
- Offset stack pointer by random amount (up to 16MB)
- Randomize `mmap()` allocation base

**Security Benefit:**
- Makes buffer overflow exploits harder (unpredictable addresses)
- Defeats return-to-libc attacks

#### 4.4 Non-Executable Stack (NX Bit)
**Description:** Mark stack pages as non-executable.

**Implementation Points:**
- Set NX bit in page table entries for stack pages
- Requires PAE (Physical Address Extension) mode on x86
- Trigger page fault on instruction fetch from stack

**Security Benefit:**
- Prevents code injection attacks (shellcode on stack)

#### 4.5 System Call Filtering (seccomp)
**Description:** Restrict syscalls available to process.

**Modes:**
- **Strict Mode:** Only read/write/exit allowed
- **Filter Mode:** BPF program defines allowed syscalls

**Use Cases:**
- Sandboxing untrusted code
- Container isolation
- Browser renderer processes

#### 4.6 Audit Logging
**Description:** Record security-relevant events.

**Events to Log:**
- Login attempts (success/failure)
- Privilege escalation (setuid calls)
- File access (reads/writes to sensitive files)
- Network connections
- Process creation

**Implementation:**
- Append events to `/var/log/audit.log`
- Include timestamp, UID, process ID, event type

---

## Priority 5: Advanced Filesystem Features

**Status:** Hierarchical FS with cache exists, no advanced features
**Impact:** Medium - Improves robustness and functionality
**Complexity:** Medium-High
**Estimated Lines:** ~2,000 lines

### Proposed Components

#### 5.1 Journaling (Write-Ahead Logging)
**Description:** Ensure filesystem consistency after crashes.

**Approach:**
- **Journal Structure:**
  - Reserved disk region for log entries
  - Transaction record: begin, operations (write block X), commit
  - Checkpoint: log flushed to main filesystem

- **Recovery Process:**
  1. Read journal on mount
  2. Replay committed but unapplied transactions
  3. Discard incomplete transactions

**Benefits:**
- Fast recovery (seconds vs. minutes with fsck)
- Guarantees metadata consistency
- Optional data journaling for full consistency

**Implementation Points:**
- Reserve journal blocks at filesystem creation
- Wrap filesystem modifications in transactions
- Write journal entries before modifying actual blocks
- Periodic checkpoint to apply journal to main FS

#### 5.2 Symbolic Links (Symlinks)
**Description:** Indirect references to files/directories.

**System Calls:**
```c
int symlink(const char *target, const char *linkpath);
int readlink(const char *path, char *buf, size_t size);
```

**Implementation Points:**
- New inode type: `INODE_SYMLINK`
- Store target path in inode data blocks
- Path resolution follows symlinks (with loop detection)
- `lstat()` vs. `stat()` (don't follow vs. follow)

**Use Cases:**
- `/usr/bin/python` → `/usr/bin/python3.9`
- Shared library versioning: `libc.so.6` → `libc.so.6.2.5`

#### 5.3 Extended Attributes (xattrs)
**Description:** Arbitrary metadata attached to files.

**System Calls:**
```c
int setxattr(const char *path, const char *name, const void *value, size_t size, int flags);
int getxattr(const char *path, const char *name, void *value, size_t size);
int listxattr(const char *path, char *list, size_t size);
int removexattr(const char *path, const char *name);
```

**Namespaces:**
- `user.*`: User-defined attributes
- `security.*`: Security labels (SELinux)
- `system.*`: System attributes (ACLs)

**Implementation Points:**
- Store xattrs in inode blocks (small attributes)
- Allocate separate blocks for large attributes
- Hash table for fast lookup

**Use Cases:**
- File checksums: `user.checksum=sha256:abc123...`
- MIME types: `user.mime_type=text/html`
- SELinux contexts: `security.selinux=unconfined_u:object_r:user_home_t`

#### 5.4 Access Control Lists (ACLs)
**Description:** More flexible permissions than traditional UNIX model.

**Features:**
- Per-user and per-group permissions
- Default ACLs for directories (inherited by new files)
- Mask entry limits effective permissions

**Example:**
```
user::rw-                 # Owner permissions
user:alice:r--            # Alice can read
group::r--                # Group permissions
group:developers:rw-      # Developers group can read/write
mask::rw-                 # Maximum effective permissions
other::---                # Everyone else has no access
```

**System Calls:**
```c
int acl_get_file(const char *path, acl_type_t type, acl_t *acl);
int acl_set_file(const char *path, acl_type_t type, acl_t acl);
```

#### 5.5 Compression
**Description:** Transparent file compression.

**Algorithms:**
- LZ4: Fast compression/decompression
- ZSTD: Better compression ratio
- GZIP: Standard compression

**Implementation Points:**
- Mark inodes as compressed (flag bit)
- Compress on write, decompress on read
- Store compressed size in inode
- Integrate with buffer cache (cache decompressed pages)

**Benefits:**
- Save disk space (2-5x reduction typical)
- Faster I/O for sequential reads (fewer blocks to transfer)

#### 5.6 Snapshots
**Description:** Point-in-time filesystem images.

**Approach:**
- Copy-on-write for modified blocks
- Snapshot metadata tracks original blocks
- Multiple snapshots share unchanged blocks

**Use Cases:**
- Backups (instant snapshot, then copy offline)
- Rollback after failed updates
- Testing (snapshot before risky operation)

---

## Priority 6: Device Driver Expansion

**Status:** Basic drivers exist (Timer, Disk, Keyboard, Serial)
**Impact:** Medium - Expands hardware support
**Complexity:** Varies (Medium-High)
**Estimated Lines:** ~1,000-3,000 lines

### Proposed Drivers

#### 6.1 USB Support
**Description:** Universal Serial Bus controller and devices.

**Components:**
- **USB Host Controller Drivers:**
  - UHCI (Universal Host Controller Interface) - USB 1.1
  - EHCI (Enhanced Host Controller Interface) - USB 2.0
  - XHCI (eXtensible Host Controller Interface) - USB 3.0

- **USB Device Drivers:**
  - USB Mass Storage (flash drives)
  - USB HID (keyboards, mice)
  - USB Serial adapters

**Implementation Points:**
- USB enumeration and device discovery
- URB (USB Request Block) management
- Interrupt/bulk/isochronous transfer types
- Hot-plug support

#### 6.2 Graphics Driver
**Description:** Beyond basic VGA text mode.

**Options:**
- **Framebuffer Driver:**
  - Linear framebuffer in memory
  - Pixel plotting, line drawing
  - Bitmap blitting
  - Text rendering

- **Accelerated Graphics:**
  - Intel HD Graphics driver
  - NVIDIA/AMD basic mode-setting

**Use Cases:**
- GUI desktop environment
- Graphics demos
- Video playback

#### 6.3 Sound Driver
**Description:** Audio output support.

**Devices:**
- **AC97:** Audio Codec '97 (common in VMs)
- **Intel HDA:** High Definition Audio
- **Sound Blaster:** Legacy ISA sound cards

**Features:**
- PCM audio playback
- Sample rate conversion
- Volume control
- Mixing multiple audio streams

**System Calls:**
```c
int open_audio(int sample_rate, int channels, int format);
ssize_t write_audio(int fd, const void *samples, size_t count);
```

#### 6.4 RTC (Real-Time Clock) Enhancement
**Description:** Expand beyond current basic RTC support.

**Features:**
- Date/time reading and setting
- Alarm interrupts
- Periodic interrupt timer
- Battery-backed storage

**System Calls:**
```c
time_t time(time_t *t);                      // Get current time
int settimeofday(const struct timeval *tv);  // Set time
int alarm(unsigned int seconds);             // Set alarm
```

---

## Priority 7: Scheduler Enhancements

**Status:** 4 schedulers exist, but CFS/EEVDF are stubs
**Impact:** Low-Medium - Improves performance
**Complexity:** Medium
**Estimated Lines:** ~800 lines

### Proposed Enhancements

#### 7.1 Complete CFS (Completely Fair Scheduler)
**Description:** Finish the Linux-style fair scheduler.

**Current Status:** Stub that panics with TODO

**Key Concepts:**
- **Virtual Runtime (vruntime):** Tracks CPU time weighted by priority
- **Red-Black Tree:** Sorts threads by vruntime (leftmost = next to run)
- **Targeted Latency:** Time period for all threads to run once (e.g., 20ms)
- **Minimum Granularity:** Shortest timeslice (e.g., 1ms)

**Algorithm:**
1. Select thread with minimum vruntime (leftmost node in tree)
2. Run for timeslice = (targeted_latency / num_threads), but >= min_granularity
3. Update vruntime += (actual_runtime × nice_weight)
4. Reinsert into tree
5. Repeat

**Implementation Points:**
- Replace panic in `schedule_fair_cfs()` (thread.c:814)
- Add `vruntime` field to thread structure
- Implement red-black tree for runqueue (or reuse lib/rbtree if available)
- Calculate timeslice dynamically based on thread count
- Update vruntime on context switch

#### 7.2 Complete EEVDF (Earliest Eligible Virtual Deadline First)
**Description:** Implement deadline-based fair scheduler.

**Current Status:** Stub that panics with TODO

**Key Concepts:**
- **Virtual Deadline:** vruntime + (request_size / weight)
- **Eligibility:** Thread is eligible if vruntime <= global_vruntime
- **Selection:** Pick eligible thread with earliest virtual deadline

**Benefits Over CFS:**
- Better latency guarantees
- Improved responsiveness for interactive tasks
- Reduces starvation scenarios

**Implementation Points:**
- Replace panic in `schedule_fair_eevdf()` (thread.c:834)
- Add `virtual_deadline` and `eligible` fields to thread
- Maintain global virtual time (min vruntime of all threads)
- Priority queue sorted by virtual deadline

#### 7.3 Real-Time Scheduling
**Description:** Hard deadline support for time-critical tasks.

**Policies:**
- **SCHED_FIFO_RT:** Real-time FIFO (runs until blocks or preempted by higher RT priority)
- **SCHED_RR_RT:** Real-time round-robin with timeslice
- **SCHED_DEADLINE:** Earliest Deadline First (EDF)

**Features:**
- RT priorities (0-99, higher than normal priorities)
- Preempt normal tasks immediately
- Deadline inheritance (similar to priority donation)

**System Calls:**
```c
int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param);
int sched_getscheduler(pid_t pid);
int sched_yield(void);
```

#### 7.4 Multi-Core/SMP Support
**Description:** Symmetric multiprocessing for parallel execution.

**Components:**
- **CPU Discovery:** Detect multiple cores/processors
- **Per-CPU Runqueues:** Each CPU has independent queue (reduces contention)
- **Load Balancing:** Migrate threads between CPUs
- **CPU Affinity:** Pin threads to specific CPUs

**Challenges:**
- Locking overhead (spinlocks for per-CPU data)
- Cache coherency (false sharing)
- Load balancing heuristics

**Benefits:**
- Utilize all CPU cores (currently Pintos uses 1 core)
- Better throughput for multi-threaded workloads

---

## Priority 8: Modern OS Features

**Status:** Not Implemented
**Impact:** Varies
**Complexity:** High
**Estimated Lines:** Varies

### Proposed Features

#### 8.1 Containers & Namespaces
**Description:** Lightweight virtualization for process isolation.

**Namespace Types:**
- **PID Namespace:** Separate process ID space
- **Mount Namespace:** Separate filesystem view
- **Network Namespace:** Separate network stack
- **IPC Namespace:** Separate IPC objects
- **UTS Namespace:** Separate hostname

**System Calls:**
```c
int unshare(int flags);                    // Create new namespace
int setns(int fd, int nstype);             // Join existing namespace
```

**Use Cases:**
- Containers (Docker-style)
- Sandboxing untrusted code
- Multi-tenancy

#### 8.2 Control Groups (cgroups)
**Description:** Resource limiting and accounting.

**Resources:**
- CPU time (limit CPU usage percentage)
- Memory (limit RAM allocation)
- Disk I/O (limit bandwidth)
- Network bandwidth

**Hierarchy:**
```
/cgroup/
  ├── webserver/      (50% CPU, 2GB RAM)
  └── database/       (80% CPU, 4GB RAM)
```

**Implementation Points:**
- Track resource usage per cgroup
- Enforce limits in scheduler/allocator
- Hierarchical accounting

#### 8.3 Kernel Modules (Dynamic Loading)
**Description:** Load/unload kernel code at runtime.

**System Calls:**
```c
int insmod(const char *module, const char *params);
int rmmod(const char *module);
int lsmod(void);
```

**Benefits:**
- Reduce kernel binary size (load only needed drivers)
- Update drivers without reboot
- Third-party driver support

**Implementation Points:**
- ELF loading for kernel modules
- Symbol resolution (exported kernel symbols)
- Module init/exit functions
- Reference counting to prevent unload while in use

#### 8.4 KVM (Kernel-based Virtual Machine)
**Description:** Hardware virtualization support (run VMs inside Pintos).

**Requirements:**
- CPU virtualization extensions (Intel VT-x or AMD-V)
- Memory virtualization (EPT/NPT)
- I/O virtualization

**Use Cases:**
- Run multiple OS instances on Pintos
- Educational: teach virtualization concepts

#### 8.5 eBPF (Extended Berkeley Packet Filter)
**Description:** Safe in-kernel programmability.

**Use Cases:**
- Network packet filtering
- System call tracing
- Performance monitoring
- Security policies

**Features:**
- JIT compilation of eBPF bytecode
- Verifier ensures safety (no crashes)
- Maps for state storage

---

## Priority 9: Development & Debugging Tools

**Status:** Basic debugging exists
**Impact:** Low-Medium - Improves developer experience
**Complexity:** Medium
**Estimated Lines:** ~1,000 lines

### Proposed Tools

#### 9.1 Kernel Debugger (kdb/kgdb)
**Description:** Interactive kernel debugging.

**Features:**
- Breakpoints and watchpoints
- Stack traces
- Memory inspection
- Thread/process listing
- Single-step execution

**Integration:**
- GDB remote protocol over serial port
- Keyboard interrupt to enter debugger (Ctrl+Alt+D)

#### 9.2 Performance Profiling
**Description:** Identify performance bottlenecks.

**Tools:**
- **perf-style Sampling:** Periodic timer interrupt samples PC (program counter)
- **Function Tracing:** Record all function entries/exits
- **Lock Contention Analysis:** Track lock wait times

**Output:**
- Flamegraphs showing CPU usage by function
- Lock contention reports

#### 9.3 Memory Leak Detection
**Description:** Detect unreleased memory.

**Approach:**
- Track all `malloc()`/`free()` calls
- Tag allocations with call stack
- Report unfreed allocations on shutdown

**Implementation:**
- Wrapper functions around allocator
- Hash table: address → allocation info

#### 9.4 System Tap / DTrace-style Tracing
**Description:** Dynamic instrumentation framework.

**Features:**
- Insert probes at arbitrary kernel functions
- Collect data (arguments, return values, timing)
- Aggregate and report

**Use Cases:**
- "Why is syscall X slow?"
- "Which process is causing disk I/O?"

---

## Priority 10: User-Space Enhancements

**Status:** Basic libc exists
**Impact:** Medium - Improves usability
**Complexity:** Medium
**Estimated Lines:** ~2,000 lines

### Proposed Enhancements

#### 10.1 Shell Improvements
**Description:** Feature-rich command interpreter.

**Features:**
- Job control (background jobs with `&`, fg/bg commands)
- Redirection (`>`, `>>`, `<`, `2>&1`)
- Pipes (`|`)
- Environment variables (`export`, `$PATH`)
- Command history (up/down arrows)
- Tab completion
- Scripting (conditionals, loops)

**Implementation Points:**
- Expand current shell (if exists) or write new one
- Use pipe syscall for pipelines
- Parse command lines with lex/yacc or hand-written parser

#### 10.2 Standard Utilities
**Description:** UNIX-style command-line tools.

**Core Utilities:**
- File: `cp`, `mv`, `rm`, `ls`, `cat`, `head`, `tail`, `grep`, `find`, `wc`
- Process: `ps`, `top`, `kill`, `nice`
- System: `df`, `du`, `mount`, `umount`
- Text: `sed`, `awk`, `sort`, `uniq`
- Network: `ping`, `wget`, `nc` (netcat)

#### 10.3 GUI Framework (Optional, Very Advanced)
**Description:** Graphical desktop environment.

**Components:**
- **Window Manager:** Manage windows, decorations, focus
- **Widget Toolkit:** Buttons, text boxes, menus
- **Display Server:** Coordinate rendering (like X11 or Wayland)

**Example Applications:**
- File manager
- Text editor
- Terminal emulator
- Web browser (very ambitious)

---

## Implementation Roadmap

### Phase 1: Foundation (Virtual Memory)
**Duration:** 4-6 weeks
**Components:**
1. Demand paging infrastructure
2. Page fault handler
3. Swap space management
4. Page replacement (Clock algorithm)
5. Memory-mapped files (mmap/munmap)
6. Copy-on-write fork()

**Milestone:** Programs larger than physical RAM can execute

### Phase 2: Networking Core
**Duration:** 6-8 weeks
**Components:**
1. Complete E1000 driver
2. Ethernet frame processing
3. ARP protocol
4. IP routing
5. ICMP (ping)
6. UDP protocol
7. Socket API (basic)

**Milestone:** Ping between Pintos instances works

### Phase 3: TCP & Applications
**Duration:** 4-6 weeks
**Components:**
1. TCP state machine
2. Retransmission and flow control
3. Socket API completion
4. Simple HTTP server demo
5. Telnet server demo

**Milestone:** Web server serves static pages

### Phase 4: IPC & Security
**Duration:** 3-4 weeks
**Components:**
1. Pipes (anonymous and named)
2. Message queues
3. Shared memory
4. Basic signals
5. File permissions (UID/GID)
6. ASLR

**Milestone:** Shell pipelines work, multi-user support

### Phase 5: Advanced Features
**Duration:** Ongoing
**Components:**
1. Filesystem journaling
2. Scheduler improvements (CFS, EEVDF, RT)
3. USB support
4. Containers/namespaces
5. Performance tools

**Milestone:** Production-quality OS features

---

## Testing Strategy

### Virtual Memory Tests
- Page fault handling (demand paging, stack growth)
- Eviction and swap (fill memory, verify no crashes)
- Mmap file operations (map, access, unmap)
- Copy-on-write (fork, verify parent/child isolation)

### Networking Tests
- Loopback (send to 127.0.0.1)
- ARP resolution (verify MAC address learning)
- Ping (ICMP echo)
- UDP echo server
- TCP connection establishment (3-way handshake)
- TCP data transfer (large files)
- Packet loss scenarios (verify retransmission)

### IPC Tests
- Pipe communication (parent writes, child reads)
- Message queue ordering (priority)
- Shared memory (concurrent updates)
- Signal delivery (verify handler invocation)

### Security Tests
- Permission checks (unauthorized access denied)
- ASLR (verify randomization)
- Setuid programs (privilege escalation)

### Integration Tests
- Multi-process network server (fork on accept)
- Distributed computation (multiple Pintos instances)
- Filesystem over network (NFS-style)

---

## Metrics for Success

### Performance Metrics
- **Virtual Memory:**
  - Page fault latency < 500μs
  - Swap throughput > 10 MB/s

- **Networking:**
  - TCP throughput > 50 Mbps (loopback)
  - Round-trip latency < 1ms (loopback)
  - Connection establishment < 10ms

- **IPC:**
  - Pipe throughput > 100 MB/s
  - Shared memory latency < 50ns
  - Signal delivery < 100μs

### Correctness Metrics
- All test cases pass
- No kernel panics under stress
- No memory leaks (valgrind-style)
- No race conditions (thread sanitizer)

### Code Quality Metrics
- Code coverage > 80%
- All public functions documented
- Consistent coding style
- Static analysis clean (no warnings)

---

## Risks & Mitigation

### Risk 1: Complexity Explosion
**Mitigation:**
- Implement incrementally (VM first, then networking)
- Thorough testing at each stage
- Code reviews before merging

### Risk 2: Hardware Incompatibility
**Mitigation:**
- Test on QEMU (guaranteed compatibility)
- Abstract hardware (use device interfaces)
- Fallback drivers for common hardware

### Risk 3: Performance Regression
**Mitigation:**
- Benchmark before and after changes
- Profile hot paths
- Optimize critical sections

### Risk 4: Security Vulnerabilities
**Mitigation:**
- Input validation (all syscalls)
- Memory safety (bounds checking)
- Privilege separation
- Security audits

---

## Conclusion

This proposal outlines a comprehensive set of enhancements to transform Pintos from an educational OS into a feature-rich, modern operating system. The priorities are structured to build upon existing components, with **Virtual Memory** as the critical foundation, followed by **Networking** for connectivity, and **IPC/Security** for robustness.

Each feature is designed to be:
- **Modular:** Can be implemented independently
- **Testable:** Clear success criteria
- **Educational:** Demonstrates core OS concepts
- **Practical:** Provides real functionality

**Recommended Starting Point:** **Virtual Memory (Priority 1)** - It's the missing Project 4, enables many other features (mmap for shared memory, swap for large programs), and completes the standard CS162 curriculum.

**Most Exciting Feature:** **Networking Stack (Priority 2)** - Transforms Pintos into a connected OS, enabling distributed systems, web servers, and modern applications. High complexity but high reward.

**Quick Win:** **Complete CFS/EEVDF Schedulers (Priority 7)** - Remove TODO panics, implement algorithms based on existing scheduler infrastructure, demonstrable performance improvements.

---

**Next Steps:**
1. Review and prioritize features based on goals
2. Set up development environment for chosen priority
3. Create detailed design documents for selected features
4. Implement test cases before implementation (TDD)
5. Iterate: code → test → review → merge

Let's build something amazing! 🚀
