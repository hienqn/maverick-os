# PintOS Utilities - Bun/TypeScript Port

This directory contains a TypeScript rewrite of the PintOS Perl utilities, designed to run with [Bun](https://bun.sh/).

## Installation

```bash
cd src/utils
bun install
```

## Usage

The wrapper scripts in `bin/` provide drop-in replacements for the original Perl scripts:

```bash
# Add to PATH
export PATH="$PWD/src/utils/bin:$PATH"

# Use as normal
pintos --qemu -- run alarm-multiple
pintos-mkdisk --filesys-size=2 mydisk.dsk
backtrace kernel.o 0xc0100000 0xc0100010
```

Or run directly with Bun:

```bash
bun run src/pintos.ts --qemu -- run alarm-multiple
bun run src/mkdisk.ts --filesys-size=2 mydisk.dsk
bun run src/backtrace.ts 0xc0100000
bun run src/set-cmdline.ts disk.dsk -- -q run test
```

## Scripts

| Script | Description |
|--------|-------------|
| `pintos.ts` | Main simulator launcher (QEMU, Bochs, VMware) |
| `mkdisk.ts` | Create virtual disk images |
| `backtrace.ts` | Convert crash addresses to symbols |
| `set-cmdline.ts` | Modify kernel command line in disk |
| `maverick-debug.ts` | Agent-friendly debugger with JSON output |

---

# maverick-debug: Agent-Friendly Debugger

The `maverick-debug` tool provides machine-readable debugging output for AI agents and automated systems. It orchestrates QEMU, GDB, and the PintOS kernel to capture structured debugging state at breakpoints.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           maverick-debug                                     │
│                                                                              │
│  ┌─────────────┐    ┌──────────────┐    ┌─────────────┐    ┌─────────────┐  │
│  │  CLI Parser │───▶│  DebugSpec   │───▶│DebugSession │───▶│DebugResult  │  │
│  │             │    │  (config)    │    │   (main)    │    │   (JSON)    │  │
│  └─────────────┘    └──────────────┘    └──────┬──────┘    └─────────────┘  │
│                                                │                             │
│                          ┌─────────────────────┼─────────────────────┐       │
│                          │                     │                     │       │
│                          ▼                     ▼                     ▼       │
│                   ┌────────────┐        ┌────────────┐        ┌──────────┐   │
│                   │  Disk      │        │   QEMU     │        │   GDB    │   │
│                   │  Builder   │        │  Launcher  │        │ Session  │   │
│                   └────────────┘        └─────┬──────┘        └────┬─────┘   │
│                                               │                    │         │
└───────────────────────────────────────────────┼────────────────────┼─────────┘
                                                │                    │
                    ┌───────────────────────────┼────────────────────┼─────────┐
                    │                           │    localhost:PORT  │         │
                    │                           ▼                    ▼         │
                    │  ┌─────────────────────────────────────────────────────┐ │
                    │  │                    QEMU                             │ │
                    │  │  ┌───────────┐    ┌───────────┐    ┌─────────────┐  │ │
                    │  │  │  CPU      │◀──▶│  Memory   │◀──▶│  GDB Stub   │  │ │
                    │  │  │  (i386/   │    │  (4-128MB)│    │  (TCP:PORT) │  │ │
                    │  │  │  riscv64) │    │           │    │             │  │ │
                    │  │  └─────┬─────┘    └───────────┘    └─────────────┘  │ │
                    │  │        │                                            │ │
                    │  │        ▼                                            │ │
                    │  │  ┌───────────────────────────────────────────────┐  │ │
                    │  │  │              PintOS Kernel                    │  │ │
                    │  │  │  ┌─────────┐  ┌─────────┐  ┌───────────────┐  │  │ │
                    │  │  │  │ Threads │  │ Synch   │  │ Test Program  │  │  │ │
                    │  │  │  │         │  │ (locks, │  │ (alarm-single │  │  │ │
                    │  │  │  │         │  │  sema)  │  │  etc.)        │  │  │ │
                    │  │  │  └─────────┘  └─────────┘  └───────────────┘  │  │ │
                    │  │  └───────────────────────────────────────────────┘  │ │
                    │  └─────────────────────────────────────────────────────┘ │
                    │                      Virtual Machine                     │
                    └──────────────────────────────────────────────────────────┘
```

## GDB/MI Protocol Flow

The tool uses GDB's Machine Interface (MI) protocol for structured communication:

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                         GDB/MI Communication Flow                            │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   GdbSession                          GDB Process                            │
│   ──────────                          ───────────                            │
│       │                                   │                                  │
│       │  1. spawn gdb --interpreter=mi3   │                                  │
│       │──────────────────────────────────▶│                                  │
│       │                                   │                                  │
│       │  2. (gdb) prompt                  │                                  │
│       │◀──────────────────────────────────│                                  │
│       │                                   │                                  │
│       │  3. -target-select remote :PORT   │                                  │
│       │──────────────────────────────────▶│                                  │
│       │                                   │───────┐                          │
│       │                                   │       │ Connect to QEMU          │
│       │                                   │◀──────┘ GDB stub                 │
│       │  4. ^connected                    │                                  │
│       │◀──────────────────────────────────│                                  │
│       │                                   │                                  │
│       │  5. -break-insert thread_create   │                                  │
│       │──────────────────────────────────▶│                                  │
│       │                                   │                                  │
│       │  6. ^done,bkpt={number="1",...}   │                                  │
│       │◀──────────────────────────────────│                                  │
│       │                                   │                                  │
│       │  7. -exec-continue                │                                  │
│       │──────────────────────────────────▶│                                  │
│       │                                   │───────┐                          │
│       │                                   │       │ Kernel runs until        │
│       │                                   │◀──────┘ breakpoint hit           │
│       │  8. *stopped,reason="breakpoint-  │                                  │
│       │     hit",bkptno="1",frame={...}   │                                  │
│       │◀──────────────────────────────────│                                  │
│       │                                   │                                  │
│       │  9. -data-list-register-values x  │                                  │
│       │──────────────────────────────────▶│                                  │
│       │                                   │                                  │
│       │  10. ^done,register-values=[...]  │                                  │
│       │◀──────────────────────────────────│                                  │
│       │                                   │                                  │
│       ▼                                   ▼                                  │
│                                                                              │
│   MI Record Types:                                                           │
│   ────────────────                                                           │
│     ^result    - Command response (done, error, running, connected, exit)    │
│     *exec      - Async execution state (stopped, running)                    │
│     +status    - Progress information                                        │
│     =notify    - Async notifications (thread-created, library-loaded)        │
│     ~console   - GDB console output (for "interpreter-exec console" cmds)    │
│     @target    - Target output                                               │
│     &log       - GDB internal log                                            │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

## Debug Session Lifecycle

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                         Debug Session State Machine                          │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│                              ┌─────────────┐                                 │
│                              │   START     │                                 │
│                              └──────┬──────┘                                 │
│                                     │                                        │
│                                     ▼                                        │
│                         ┌───────────────────────┐                            │
│                         │   Parse CLI / Spec    │                            │
│                         └───────────┬───────────┘                            │
│                                     │                                        │
│                                     ▼                                        │
│                         ┌───────────────────────┐                            │
│                         │   Find Build Paths    │──────▶ ERROR: kernel.o    │
│                         │   (kernel.o, loader)  │        not found           │
│                         └───────────┬───────────┘                            │
│                                     │                                        │
│                                     ▼                                        │
│               ┌─────────────────────────────────────────┐                    │
│               │        Build Bootable Disk (i386)       │                    │
│               │  ┌─────────────────────────────────┐    │                    │
│               │  │ loader.bin + kernel.bin + args  │    │                    │
│               │  │         → os.dsk                │    │                    │
│               │  └─────────────────────────────────┘    │                    │
│               └─────────────────┬───────────────────────┘                    │
│                                 │                                            │
│                                 ▼                                            │
│               ┌─────────────────────────────────────────┐                    │
│               │         Start QEMU with GDB Stub        │                    │
│               │  ┌─────────────────────────────────┐    │                    │
│               │  │ qemu -s -S -gdb tcp::PORT       │    │                    │
│               │  │ -S = start paused               │    │                    │
│               │  │ -s = enable GDB stub            │    │                    │
│               │  └─────────────────────────────────┘    │                    │
│               └─────────────────┬───────────────────────┘                    │
│                                 │                                            │
│                                 ▼                                            │
│               ┌─────────────────────────────────────────┐                    │
│               │      Wait for GDB Port (TCP Poll)       │──▶ TIMEOUT        │
│               │                                         │                    │
│               │  while (!portOpen && time < 10s) {      │                    │
│               │    try { connect(localhost:PORT) }      │                    │
│               │    sleep(50ms)                          │                    │
│               │  }                                      │                    │
│               └─────────────────┬───────────────────────┘                    │
│                                 │                                            │
│                                 ▼                                            │
│               ┌─────────────────────────────────────────┐                    │
│               │         Connect GDB to QEMU             │                    │
│               │  -target-select remote localhost:PORT   │                    │
│               └─────────────────┬───────────────────────┘                    │
│                                 │                                            │
│                                 ▼                                            │
│               ┌─────────────────────────────────────────┐                    │
│               │       Set Breakpoints/Watchpoints       │                    │
│               │  -break-insert <location>               │                    │
│               │  -break-watch <expression>              │                    │
│               └─────────────────┬───────────────────────┘                    │
│                                 │                                            │
│                                 ▼                                            │
│               ┌─────────────────────────────────────────┐                    │
│               │            Main Debug Loop              │                    │
│               │  ┌───────────────────────────────────┐  │                    │
│               │  │                                   │  │                    │
│               │  │    while (stops < maxStops) {    │  │                    │
│          ┌────┼──│      -exec-continue              │  │                    │
│          │    │  │      waitForStop()               │◀─┼──┐                 │
│          │    │  │      getRegisters()              │  │  │                 │
│          │    │  │      getBacktrace()              │  │  │                 │
│          │    │  │      getSourceContext()          │  │  │                 │
│          │    │  │      getDisassembly()            │  │  │                 │
│          │    │  │      readMemory()                │  │  │                 │
│          │    │  │      executeCommands()           │  │  │                 │
│          │    │  │      stops.push(event)           │──┼──┘                 │
│          │    │  │    }                             │  │                    │
│          │    │  │                                   │  │                    │
│          │    │  └───────────────────────────────────┘  │                    │
│          │    └─────────────────┬───────────────────────┘                    │
│          │                      │                                            │
│          │    ┌─────────────────┼─────────────────┐                          │
│          │    │                 │                 │                          │
│          ▼    ▼                 ▼                 ▼                          │
│     ┌─────────────┐      ┌───────────┐     ┌───────────┐                     │
│     │  PANIC      │      │ TIMEOUT   │     │ COMPLETED │                     │
│     │  (serial    │      │ (max time │     │ (clean    │                     │
│     │   output)   │      │  reached) │     │  exit)    │                     │
│     └──────┬──────┘      └─────┬─────┘     └─────┬─────┘                     │
│            │                   │                 │                           │
│            └───────────────────┼─────────────────┘                           │
│                                │                                             │
│                                ▼                                             │
│                    ┌───────────────────────┐                                 │
│                    │   Cleanup & Terminate │                                 │
│                    │   - GDB: -gdb-exit    │                                 │
│                    │   - QEMU: SIGKILL     │                                 │
│                    └───────────┬───────────┘                                 │
│                                │                                             │
│                                ▼                                             │
│                    ┌───────────────────────┐                                 │
│                    │  Output JSON Result   │                                 │
│                    └───────────────────────┘                                 │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

## Data Structures

### Input: DebugSpec

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              DebugSpec                                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  {                                                                           │
│    "version": 1,                        // Schema version                    │
│    "test": "alarm-single",              // Test name to run                  │
│    "arch": "i386",                       // i386 | riscv64                   │
│    "timeout": 60,                        // Max seconds                      │
│    "maxStops": 10,                       // Max breakpoint hits              │
│                                                                              │
│    "breakpoints": [                                                          │
│      ┌──────────────────────────────────────────────────────────────────┐   │
│      │ { "location": "thread_create" }           // Function name       │   │
│      │ { "location": "thread.c:123" }            // File:line           │   │
│      │ { "location": "*0xc00214dd" }             // Raw address         │   │
│      │ { "location": "lock_acquire",                                    │   │
│      │   "condition": "lock->holder != 0" }      // Conditional         │   │
│      │ { "location": "main", "temporary": true } // One-shot            │   │
│      └──────────────────────────────────────────────────────────────────┘   │
│    ],                                                                        │
│                                                                              │
│    "watchpoints": [                                                          │
│      ┌──────────────────────────────────────────────────────────────────┐   │
│      │ { "expression": "ready_list", "type": "write" }   // On write    │   │
│      │ { "expression": "*0xc0020000", "type": "read" }   // On read     │   │
│      │ { "expression": "thread->priority", "type": "access" }           │   │
│      └──────────────────────────────────────────────────────────────────┘   │
│    ],                                                                        │
│                                                                              │
│    "memoryDumps": [                                                          │
│      ┌──────────────────────────────────────────────────────────────────┐   │
│      │ { "address": "$esp", "count": 16 }        // Stack top           │   │
│      │ { "address": "0xc0000000", "count": 8 }   // Kernel base         │   │
│      └──────────────────────────────────────────────────────────────────┘   │
│    ],                                                                        │
│                                                                              │
│    "commandsAtStop": [                   // Run at each breakpoint           │
│      "bt",                               // Backtrace                        │
│      "print lock->holder->priority",     // Examine variables                │
│      "info threads"                      // Thread list                      │
│    ]                                                                         │
│  }                                                                           │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Output: DebugResult

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              DebugResult                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  {                                                                           │
│    "version": 1,                                                             │
│    "status": "completed",                // completed|timeout|panic|error    │
│    "test": "alarm-single",                                                   │
│    "arch": "i386",                                                           │
│    "executionTimeMs": 2345,                                                  │
│    "errors": [],                         // Any warnings/errors              │
│                                                                              │
│    "breakpointsSet": [                                                       │
│      ┌──────────────────────────────────────────────────────────────────┐   │
│      │ {                                                                │   │
│      │   "id": 1,                        // GDB breakpoint number       │   │
│      │   "location": "thread_create",                                   │   │
│      │   "address": "0xc00214dd",        // Resolved address            │   │
│      │   "condition": null,                                             │   │
│      │   "hitCount": 0                                                  │   │
│      │ }                                                                │   │
│      └──────────────────────────────────────────────────────────────────┘   │
│    ],                                                                        │
│                                                                              │
│    "watchpointsSet": [...],                                                  │
│                                                                              │
│    "stops": [                            // Array of StopEvent               │
│      ┌──────────────────────────────────────────────────────────────────┐   │
│      │  (See StopEvent diagram below)                                   │   │
│      └──────────────────────────────────────────────────────────────────┘   │
│    ],                                                                        │
│                                                                              │
│    "serialOutput": "Booting from...\n..."  // QEMU console output           │
│  }                                                                           │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### StopEvent (captured at each breakpoint)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                               StopEvent                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  {                                                                           │
│    "stopNumber": 1,                                                          │
│    "reason": "breakpoint-hit",           // See StopReason enum              │
│    "breakpointId": 1,                    // Which BP was hit                 │
│    "arch": "i386",                                                           │
│                                                                              │
│    ┌─── location ──────────────────────────────────────────────────────┐    │
│    │ {                                                                 │    │
│    │   "function": "thread_create",                                    │    │
│    │   "file": "../../../threads/thread.c",                            │    │
│    │   "line": 387,                                                    │    │
│    │   "address": "0xc00214dd"                                         │    │
│    │ }                                                                 │    │
│    └───────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│    ┌─── registers (i386) ──────────────────────────────────────────────┐    │
│    │ {                                                                 │    │
│    │   "eax": "0x00000000",  "ebx": "0xc0020f80",                      │    │
│    │   "ecx": "0x00000001",  "edx": "0xc00543a0",                      │    │
│    │   "esi": "0x00000000",  "edi": "0x00000000",                      │    │
│    │   "ebp": "0xc0026bbc",  "esp": "0xc0026b9c",                      │    │
│    │   "eip": "0xc00214dd",  "eflags": "0x00000046",                   │    │
│    │   "cs": "0x0008", "ds": "0x0010", "es": "0x0010",                 │    │
│    │   "fs": "0x0010", "gs": "0x0010", "ss": "0x0010"                  │    │
│    │ }                                                                 │    │
│    └───────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│    ┌─── backtrace ─────────────────────────────────────────────────────┐    │
│    │ [                                                                 │    │
│    │   { "frame": 0, "function": "thread_create",                      │    │
│    │     "file": "thread.c", "line": 387, "address": "0xc00214dd" },   │    │
│    │   { "frame": 1, "function": "main",                               │    │
│    │     "file": "init.c", "line": 179, "address": "0xc0020b45" },     │    │
│    │   { "frame": 2, "function": "pintos_init",                        │    │
│    │     "file": "init.c", "line": 68, "address": "0xc0020a12" }       │    │
│    │ ]                                                                 │    │
│    └───────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│    ┌─── sourceContext ─────────────────────────────────────────────────┐    │
│    │ {                                                                 │    │
│    │   "file": "../../../threads/thread.c",                            │    │
│    │   "lines": [                                                      │    │
│    │     { "lineNumber": 384, "text": "   ASSERT(googol != NULL);",    │    │
│    │       "isCurrent": false },                                       │    │
│    │     { "lineNumber": 385, "text": "",                              │    │
│    │       "isCurrent": false },                                       │    │
│    │     { "lineNumber": 386, "text": "   /* Allocate thread. */",     │    │
│    │       "isCurrent": false },                                       │    │
│    │     { "lineNumber": 387, "text": "   t = palloc_get_page(...);",  │    │
│    │       "isCurrent": true },        ◀── Current line                │    │
│    │     { "lineNumber": 388, "text": "   if (t == NULL)",             │    │
│    │       "isCurrent": false },                                       │    │
│    │     { "lineNumber": 389, "text": "     return TID_ERROR;",        │    │
│    │       "isCurrent": false }                                        │    │
│    │   ]                                                               │    │
│    │ }                                                                 │    │
│    └───────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│    ┌─── disassembly ───────────────────────────────────────────────────┐    │
│    │ [                                                                 │    │
│    │   { "address": "0xc00214d8", "funcName": "thread_create",         │    │
│    │     "offset": 0, "instruction": "push %ebp",                      │    │
│    │     "isCurrent": false },                                         │    │
│    │   { "address": "0xc00214d9", "funcName": "thread_create",         │    │
│    │     "offset": 1, "instruction": "mov %esp,%ebp",                  │    │
│    │     "isCurrent": false },                                         │    │
│    │   { "address": "0xc00214dd", "funcName": "thread_create",         │    │
│    │     "offset": 5, "instruction": "push %edi",                      │    │
│    │     "isCurrent": true },          ◀── Current instruction         │    │
│    │   ...                                                             │    │
│    │ ]                                                                 │    │
│    └───────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│    ┌─── memoryDumps ───────────────────────────────────────────────────┐    │
│    │ {                                                                 │    │
│    │   "$esp:16": [                                                    │    │
│    │     "0xc0026b9b", "0xc00543a0", "0xc00200b5", "0x00001000",       │    │
│    │     "0xc0026bbc", "0xc0020b45", "0xc00543a0", "0xc0022340",       │    │
│    │     ...                                                           │    │
│    │   ]                                                               │    │
│    │ }                                                                 │    │
│    └───────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│    ┌─── commandOutputs ────────────────────────────────────────────────┐    │
│    │ {                                                                 │    │
│    │   "print name": "$1 = 0xc00543a0 \"kbd-worker\"",                 │    │
│    │   "bt": "#0  thread_create (...) at thread.c:387\n#1  ..."        │    │
│    │ }                                                                 │    │
│    └───────────────────────────────────────────────────────────────────┘    │
│  }                                                                           │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Component Interaction

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        Component Dependencies                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   maverick-debug.ts                                                          │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │  • CLI parsing (parseArgs)                                          │   │
│   │  • Spec building (buildSpecFromArgs)                                │   │
│   │  • Session orchestration (runDebugSession)                          │   │
│   │  • JSON output formatting                                           │   │
│   │  • Signal handlers (SIGINT, SIGTERM)                                │   │
│   └───────────────────────────────┬─────────────────────────────────────┘   │
│                                   │                                          │
│               ┌───────────────────┼───────────────────┐                      │
│               │                   │                   │                      │
│               ▼                   ▼                   ▼                      │
│   ┌───────────────────┐  ┌───────────────┐  ┌───────────────────┐           │
│   │    disk.ts        │  │ simulator.ts  │  │  gdb-session.ts   │           │
│   │  ┌─────────────┐  │  │ ┌───────────┐ │  │  ┌─────────────┐  │           │
│   │  │readLoader() │  │  │ │startFor-  │ │  │  │connectGdb() │  │           │
│   │  │assembleDisk │  │  │ │Debug()    │ │  │  │setBreakpoint│  │           │
│   │  │()           │  │  │ │           │ │  │  │continue()   │  │           │
│   │  └─────────────┘  │  │ │           │ │  │  │getRegisters │  │           │
│   │                   │  │ └───────────┘ │  │  │getBacktrace │  │           │
│   │  Assembles boot-  │  │               │  │  │getSource-   │  │           │
│   │  able disk with   │  │  Spawns QEMU  │  │  │Context()    │  │           │
│   │  loader + kernel  │  │  with GDB     │  │  │getDisassem- │  │           │
│   │  + arguments      │  │  stub enabled │  │  │bly()        │  │           │
│   └───────────────────┘  └───────────────┘  │  │readMemory() │  │           │
│                                             │  │execute()    │  │           │
│                                             │  │terminate()  │  │           │
│                                             │  └─────────────┘  │           │
│                                             │                   │           │
│                                             │  Wraps GDB/MI     │           │
│                                             │  protocol         │           │
│                                             └─────────┬─────────┘           │
│                                                       │                      │
│                                                       ▼                      │
│                                             ┌───────────────────┐           │
│                                             │ gdb-mi-parser.ts  │           │
│                                             │ ┌───────────────┐ │           │
│                                             │ │parseMiOutput()│ │           │
│                                             │ │findResult-    │ │           │
│                                             │ │Record()       │ │           │
│                                             │ │isResultError()│ │           │
│                                             │ │collectConsole-│ │           │
│                                             │ │Output()       │ │           │
│                                             │ │parseStoppedRe-│ │           │
│                                             │ │cord()         │ │           │
│                                             │ └───────────────┘ │           │
│                                             │                   │           │
│                                             │  Parses GDB/MI    │           │
│                                             │  text protocol    │           │
│                                             │  to structured    │           │
│                                             │  records          │           │
│                                             └───────────────────┘           │
│                                                                              │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │                        debug-types.ts                               │   │
│   │  Type definitions shared across all components:                     │   │
│   │  DebugSpec, DebugResult, StopEvent, BreakpointSpec,                │   │
│   │  WatchpointSpec, Registers, StackFrame, MiRecord, etc.             │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Memory Layout Reference

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    i386 PintOS Memory Map                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   0xFFFFFFFF ┌─────────────────────────────────────────┐                    │
│              │                                         │                    │
│              │           Kernel Space                  │                    │
│              │                                         │                    │
│              │   Kernel code & data loaded at          │                    │
│              │   physical 0x100000, mapped to          │                    │
│              │   virtual 0xC0100000                    │                    │
│              │                                         │                    │
│   0xC0000000 ├─────────────────────────────────────────┤ ◀── PHYS_BASE     │
│              │                                         │                    │
│              │           User Space                    │                    │
│              │                                         │                    │
│              │   User stack grows down from            │                    │
│              │   below PHYS_BASE                       │                    │
│              │                                         │                    │
│   0x08048000 │   ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─  │ ◀── User code     │
│              │                                         │     starts here   │
│              │   Code, data, BSS                       │                    │
│              │                                         │                    │
│   0x00000000 └─────────────────────────────────────────┘                    │
│                                                                              │
│                                                                              │
│   Register Quick Reference (i386):                                          │
│   ───────────────────────────────                                            │
│     EIP  = Instruction pointer (current code address)                       │
│     ESP  = Stack pointer (top of stack)                                     │
│     EBP  = Base pointer (current stack frame)                               │
│     EAX  = Return value / accumulator                                       │
│     EBX  = Callee-saved (preserved across calls)                            │
│     ECX  = Counter / 4th argument                                           │
│     EDX  = 3rd argument / I/O                                               │
│     ESI  = Source index / 2nd argument                                      │
│     EDI  = Destination index / 1st argument                                 │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                    RISC-V PintOS Memory Map                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   Sv39 Virtual Address Space (39 bits = 512GB)                              │
│                                                                              │
│   0xFFFFFFFF_FFFFFFFF ┌───────────────────────────────┐                     │
│                       │                               │                     │
│   0xFFFFFFFF_80000000 │      Kernel Space             │ ◀── PHYS_BASE      │
│                       │      (upper 2GB)              │                     │
│                       ├───────────────────────────────┤                     │
│                       │                               │                     │
│                       │      User Space               │                     │
│                       │      (lower 256GB)            │                     │
│                       │                               │                     │
│   0x00000000_00000000 └───────────────────────────────┘                     │
│                                                                              │
│   Register Quick Reference (RISC-V):                                        │
│   ─────────────────────────────────                                          │
│     PC   = Program counter                                                  │
│     RA   = Return address (x1)                                              │
│     SP   = Stack pointer (x2)                                               │
│     GP   = Global pointer (x3)                                              │
│     TP   = Thread pointer (x4)                                              │
│     A0-A7 = Arguments / return values (x10-x17)                             │
│     S0-S11 = Saved registers (x8-x9, x18-x27)                               │
│     T0-T6 = Temporaries (x5-x7, x28-x31)                                    │
│     S0/FP = Frame pointer (x8, alias for s0)                                │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## CLI Reference

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         maverick-debug CLI                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  maverick-debug [OPTIONS]                                                    │
│                                                                              │
│  REQUIRED (one of):                                                          │
│  ─────────────────                                                           │
│    --test, -t NAME        Test to run (e.g., "alarm-single")                │
│    --spec, -s FILE        Load session config from JSON file                │
│                                                                              │
│  BREAKPOINTS:                                                                │
│  ────────────                                                                │
│    --break, -b LOC        Set breakpoint at LOC                             │
│                           LOC can be:                                        │
│                             • Function name: thread_create                   │
│                             • File:line: thread.c:387                        │
│                             • Address: *0xc00214dd                           │
│                                                                              │
│    --break-if "LOC if C"  Conditional breakpoint                            │
│                           Example: "lock_acquire if lock->holder != 0"       │
│                                                                              │
│  WATCHPOINTS:                                                                │
│  ────────────                                                                │
│    --watch, -w EXPR       Break when EXPR is written                        │
│    --rwatch EXPR          Break when EXPR is read                           │
│                                                                              │
│  COMMANDS:                                                                   │
│  ─────────                                                                   │
│    --commands, -c CMDS    GDB commands to run at each stop                  │
│                           Semicolon-separated: "bt; print x; info regs"     │
│                                                                              │
│    --memory, -m SPEC      Memory to dump at each stop                       │
│                           Format: ADDRESS:COUNT                              │
│                           Examples: "$esp:16", "0xc0000000:8"               │
│                                                                              │
│  LIMITS:                                                                     │
│  ───────                                                                     │
│    --max-stops N          Max breakpoint hits (default: 10)                 │
│    --timeout, -T SECS     Execution timeout (default: 60)                   │
│                                                                              │
│  OUTPUT:                                                                     │
│  ───────                                                                     │
│    --output, -o FILE      Write JSON to FILE (default: stdout)              │
│    --arch, -a ARCH        Architecture: i386 (default), riscv64             │
│                                                                              │
│  EXAMPLES:                                                                   │
│  ─────────                                                                   │
│                                                                              │
│    # Basic: Break at thread_create                                          │
│    maverick-debug --test alarm-single --break thread_create                 │
│                                                                              │
│    # Multiple breakpoints with commands                                     │
│    maverick-debug --test priority-donate-one \                              │
│      --break lock_acquire \                                                 │
│      --break lock_release \                                                 │
│      --commands "bt; print lock->holder->priority" \                        │
│      --max-stops 5                                                          │
│                                                                              │
│    # Memory dump of stack                                                   │
│    maverick-debug --test alarm-single \                                     │
│      --break timer_interrupt \                                              │
│      --memory '$esp:16'                                                     │
│                                                                              │
│    # Conditional breakpoint                                                 │
│    maverick-debug --test priority-donate-one \                              │
│      --break-if "lock_acquire if lock->holder != 0"                         │
│                                                                              │
│    # Write watchpoint                                                       │
│    maverick-debug --test mlfqs-load-1 \                                     │
│      --watch "load_avg"                                                     │
│                                                                              │
│    # Save to file                                                           │
│    maverick-debug --test alarm-single \                                     │
│      --break thread_create \                                                │
│      --output debug-results.json                                            │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Error Handling & Robustness

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       Robustness Features                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  1. RANDOM GDB PORTS                                                         │
│  ────────────────────                                                        │
│     • Each session uses a random port (10000-60000)                         │
│     • Enables parallel debugging of multiple tests                          │
│     • Avoids "port already in use" conflicts                                │
│                                                                              │
│  2. TCP PORT POLLING                                                         │
│  ───────────────────                                                         │
│     • Polls for GDB port availability (50ms interval)                       │
│     • 10-second timeout for QEMU startup                                    │
│     • More reliable than fixed sleep delays                                 │
│                                                                              │
│        ┌────────────────────────────────────────────────┐                   │
│        │  while (time < 10s) {                         │                   │
│        │    if (canConnect(localhost:PORT)) break;     │                   │
│        │    sleep(50ms);                               │                   │
│        │  }                                            │                   │
│        └────────────────────────────────────────────────┘                   │
│                                                                              │
│  3. SIGNAL HANDLERS                                                          │
│  ──────────────────                                                          │
│     • SIGINT (Ctrl+C): Graceful cleanup before exit                         │
│     • SIGTERM: Clean shutdown                                               │
│     • Exit handler: Force-kill any zombie QEMU processes                    │
│                                                                              │
│  4. TIMEOUT HANDLING                                                         │
│  ───────────────────                                                         │
│     • Per-command timeout in GDB session                                    │
│     • Overall session timeout                                               │
│     • Returns partial results on timeout (not hard failure)                 │
│                                                                              │
│  5. ERROR ACCUMULATION                                                       │
│  ─────────────────────                                                       │
│     • Non-fatal errors collected in errors[] array                          │
│     • Session continues even if some breakpoints fail                       │
│     • All warnings preserved for debugging                                  │
│                                                                              │
│  6. GRACEFUL DEGRADATION                                                     │
│  ───────────────────────                                                     │
│     • Source context: Optional, errors silently ignored                     │
│     • Disassembly: Optional, falls back to x/i if disassemble fails        │
│     • Memory dumps: Errors logged but don't stop session                    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Troubleshooting

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        Common Issues & Solutions                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ERROR: "Kernel not found: .../kernel.o"                                    │
│  ───────────────────────────────────────                                     │
│    Cause: Kernel hasn't been built for the test's component                 │
│    Fix:   cd src/<component> && make ARCH=i386                              │
│                                                                              │
│  ERROR: "GDB server port did not become available"                          │
│  ─────────────────────────────────────────────────                           │
│    Cause: QEMU failed to start or crashed                                   │
│    Fix:   Check if QEMU is installed: qemu-system-i386 --version            │
│           Check kernel binary: file build/i386/kernel.bin                   │
│                                                                              │
│  ERROR: "Failed to connect to GDB server"                                   │
│  ────────────────────────────────────────                                    │
│    Cause: GDB can't connect to QEMU's stub                                  │
│    Fix:   Ensure correct architecture GDB: i386-elf-gdb or gdb-multiarch    │
│                                                                              │
│  ERROR: "Failed to set breakpoint at <location>"                            │
│  ───────────────────────────────────────────────                             │
│    Cause: Symbol not found or stripped                                      │
│    Fix:   Verify symbol exists: nm kernel.o | grep <symbol>                 │
│           Check debug info: file kernel.o (should show "with debug_info")   │
│                                                                              │
│  STATUS: "timeout"                                                           │
│  ─────────────────                                                           │
│    Cause: Test didn't hit breakpoints within timeout                        │
│    Fix:   Increase --timeout, verify breakpoint locations                   │
│           Check if test runs: pintos --qemu -- run <test>                   │
│                                                                              │
│  STATUS: "panic"                                                             │
│  ────────────────                                                            │
│    Cause: Kernel panicked (assertion failed, etc.)                          │
│    Fix:   Check serialOutput in result for panic message                    │
│           Use backtrace from last stop to identify crash location           │
│                                                                              │
│  EMPTY disassembly or sourceContext                                         │
│  ────────────────────────────────────                                        │
│    Cause: GDB can't find source/debug info for location                     │
│    Fix:   Ensure kernel built with debug symbols (default)                  │
│           Check file paths in GDB: info sources                             │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## File Structure

```
src/utils/
├── package.json               # Bun/npm package config
├── tsconfig.json              # TypeScript config
├── bun.lock                   # Dependency lock file
├── README.md                  ◀── This file
├── Makefile                   # Build C utilities (squish-pty, etc.)
├── src/                       # TypeScript source files
│   ├── lib/
│   │   ├── types.ts           # Shared type definitions (Architecture, etc.)
│   │   ├── disk.ts            # Disk operations, MBR, partitions
│   │   ├── ustar.ts           # ustar archive format
│   │   ├── subprocess.ts      # Process management with timeout
│   │   ├── simulator.ts       # QEMU/Bochs/VMware launchers
│   │   ├── debug-types.ts     # Debug tool type definitions
│   │   ├── gdb-mi-parser.ts   # GDB/MI protocol parser
│   │   └── gdb-session.ts     # GDB session management
│   ├── pintos.ts              # Main simulator entry point
│   ├── mkdisk.ts              # Disk creation
│   ├── backtrace.ts           # Symbol resolution
│   ├── set-cmdline.ts         # Cmdline modification
│   └── maverick-debug.ts      # Agent-friendly debugger
├── bin/                       # Wrapper shell scripts
│   ├── pintos
│   ├── pintos-mkdisk
│   ├── backtrace
│   ├── set-cmdline
│   └── maverick-debug
├── pintos-gdb                 # GDB wrapper script
├── pintos-test                # Test runner script
├── squish-pty.c               # PTY handling for Bochs
├── squish-unix.c              # Unix socket relay for VMware
└── setitimer-helper.c         # Timer helper utility
```

## Compatibility

These scripts aim for 100% CLI compatibility with the original Perl versions. All the same flags and arguments should work identically.

## Development

```bash
# Type checking
bun run tsc --noEmit

# Run a script directly
bun run src/pintos.ts --help

# Run maverick-debug
bun run src/maverick-debug.ts --test alarm-single --break thread_create
```
