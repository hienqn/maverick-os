# PintOS Utilities - Bun/TypeScript Port

This directory contains a TypeScript rewrite of the PintOS Perl utilities, designed to run with [Bun](https://bun.sh/).

## Installation

```bash
cd src/utils/bun
bun install
```

## Usage

The wrapper scripts in `bin/` provide drop-in replacements for the original Perl scripts:

```bash
# Add to PATH
export PATH="$PWD/src/utils/bun/bin:$PATH"

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
| `debug-pintos.ts` | Agent-friendly debugger with JSON output |

## Debug Tool for AI Agents

The `debug-pintos` tool provides machine-readable debugging for AI assistants. It runs GDB in batch mode with the MI (Machine Interface) protocol and returns structured JSON.

```bash
# Basic breakpoint debugging
debug-pintos --test alarm-single --break thread_create

# With custom GDB commands and memory dumps
debug-pintos --test priority-donate-one \
  --break lock_acquire \
  --commands "bt,print lock->holder->priority" \
  --memory '$esp:8' \
  --max-stops 5

# Conditional breakpoint
debug-pintos --test priority-donate-one \
  --break-if "lock_acquire if lock->holder != 0"
```

**Output includes:**
- Register values at each stop
- Full backtrace with file/line info
- Memory dumps at specified addresses
- Custom GDB command outputs
- QEMU serial console output
- Breakpoint/watchpoint confirmation

See CLAUDE.md for full documentation.

## PTY/Socket Utilities

For Bochs serial console and VMware Player socket communication, the TypeScript scripts use the existing C utilities:
- `squish-pty` - PTY handling for Bochs
- `squish-unix` - Unix socket relay for VMware

These must be compiled from the parent `utils/` directory and be in your PATH.

## File Structure

```
src/utils/bun/
├── package.json
├── tsconfig.json
├── src/
│   ├── lib/
│   │   ├── types.ts         # Shared type definitions
│   │   ├── disk.ts          # Disk operations, MBR, partitions
│   │   ├── ustar.ts         # ustar archive format
│   │   ├── subprocess.ts    # Process management with timeout
│   │   ├── simulator.ts     # QEMU/Bochs/VMware launchers
│   │   ├── debug-types.ts   # Debug tool type definitions
│   │   ├── gdb-mi-parser.ts # GDB/MI protocol parser
│   │   └── gdb-session.ts   # GDB session management
│   ├── pintos.ts            # Main entry point
│   ├── mkdisk.ts            # Disk creation
│   ├── backtrace.ts         # Symbol resolution
│   ├── set-cmdline.ts       # Cmdline modification
│   └── debug-pintos.ts      # Agent-friendly debugger
└── bin/                     # Wrapper shell scripts
```

## Compatibility

These scripts aim for 100% CLI compatibility with the original Perl versions. All the same flags and arguments should work identically.

## Development

```bash
# Type checking
bun run tsc --noEmit

# Run a script directly
bun run src/pintos.ts --help
```
