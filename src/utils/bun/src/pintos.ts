#!/usr/bin/env bun
/**
 * pintos - Main utility for running Pintos in a simulator
 *
 * Usage: pintos [OPTION...] -- [ARGUMENT...]
 */

import * as fs from "fs";
import * as os from "os";
import * as path from "path";
import {
  ROLE_ORDER,
  DEFAULT_GEOMETRY,
  ZIP_GEOMETRY,
  type Architecture,
  type DiskGeometry,
  type PartitionSource,
  type PartitionRole,
  type AlignmentMode,
  type PartitionMap,
  type AssembleDiskArgs,
} from "./lib/types";
import {
  assembleDisk,
  readLoader,
  readPartitionTable,
  readMbr,
  findFile,
  roundUp,
  writeFully,
  readFully,
  copyFile,
} from "./lib/disk";
import { putScratchFile, getScratchFile } from "./lib/ustar";
import { runVm, type Simulator, type Debugger, type VgaMode } from "./lib/simulator";

// ============================================================================
// Global state
// ============================================================================

const startTime = Date.now() / 1000;
let arch: Architecture = "i386";
let sim: Simulator = "bochs";
let debug: Debugger = "none";
let mem = 4;
let serial = true;
let vga: VgaMode | undefined;
let jitter: number | undefined;
let realtime = false;
let timeout: number | undefined;
let killOnFailure = false;

const puts: [string, string?][] = [];
const gets: [string, string?][] = [];
let asRef: [string, string?] | undefined;

let kernelArgs: string[] = [];
const parts: PartitionMap = {};
let makeDisk: string | undefined;
let tmpDisk = true;
const disks: string[] = [];
let loaderFn: string | undefined;
let geometry: DiskGeometry = { ...DEFAULT_GEOMETRY };
let align: AlignmentMode = "bochs";

// ============================================================================
// Argument parsing helpers
// ============================================================================

function setSim(newSim: Simulator): void {
  if (sim !== "bochs" && sim !== newSim) {
    throw new Error(`--${newSim} conflicts with --${sim}`);
  }
  sim = newSim;
}

function setDebug(newDebug: Debugger): void {
  if (debug !== "none" && newDebug !== "none" && debug !== newDebug) {
    throw new Error(`--${newDebug} conflicts with --${debug}`);
  }
  debug = newDebug;
}

function setVga(newVga: VgaMode): void {
  if (vga !== undefined && vga !== newVga) {
    console.log("warning: conflicting vga display options");
  }
  vga = newVga;
}

function setJitter(value: number): void {
  if (realtime) throw new Error("--realtime conflicts with --jitter");
  if (jitter !== undefined && jitter !== value) {
    throw new Error("different --jitter already defined");
  }
  jitter = value;
}

function setRealtime(): void {
  if (jitter !== undefined) throw new Error("--realtime conflicts with --jitter");
  realtime = true;
}

function addFile(list: [string, string?][], file: string): void {
  asRef = [file];
  list.push(asRef);
}

function setAs(as: string): void {
  if (!asRef) throw new Error("-a (or --as) is only allowed after -p or -g");
  if (asRef[1] !== undefined) throw new Error("Only one -a (or --as) is allowed after -p or -g");
  asRef[1] = as;
}

function setDisk(disk: string): void {
  disks.push(disk);

  const pt = readPartitionTable(disk);
  for (const role of Object.keys(pt) as PartitionRole[]) {
    if (parts[role]) {
      throw new Error(`can't have two sources for ${role.toLowerCase()} partition`);
    }
    parts[role] = {
      DISK: disk,
      START: pt[role]!.START,
      SECTORS: pt[role]!.SECTORS,
    };
  }
}

function setPart(role: PartitionRole, source: "file" | "from" | "size", arg: string): void {
  if (parts[role]) {
    throw new Error(`can't have two sources for ${role.toLowerCase()} partition`);
  }

  const p: PartitionSource = {};

  if (source === "file") {
    const mbr = readMbr(arg);
    if (mbr) {
      console.error(`warning: ${arg} looks like a partitioned disk `);
      console.error(`(did you want --${role.toLowerCase()}-from=${arg} or --disk=${arg}?)`);
    }

    p.FILE = arg;
    p.OFFSET = 0;
    p.BYTES = fs.statSync(arg).size;
  } else if (source === "from") {
    const pt = readPartitionTable(arg);
    const sp = pt[role];
    if (!sp) {
      throw new Error(`${arg}: does not contain ${role.toLowerCase()} partition`);
    }

    p.FILE = arg;
    p.OFFSET = sp.START * 512;
    p.BYTES = sp.SECTORS * 512;
  } else if (source === "size") {
    if (!/^\d+(\.\d+)?|\.\d+$/.test(arg)) {
      throw new Error(`${arg}: not a valid size in MB`);
    }

    p.FILE = "/dev/zero";
    p.OFFSET = 0;
    p.BYTES = Math.ceil(parseFloat(arg) * 1024 * 1024);
  }

  parts[role] = p;
}

function setGeometry(value: string): void {
  if (value === "zip") {
    geometry = { ...ZIP_GEOMETRY };
  } else {
    const match = value.match(/^(\d+)[,\s]+(\d+)$/);
    if (!match) throw new Error("bad syntax for geometry");
    const h = parseInt(match[1], 10);
    const s = parseInt(match[2], 10);
    if (h > 255) throw new Error("heads limited to 255");
    if (s > 63) throw new Error("sectors per track limited to 63");
    geometry = { H: h, S: s };
  }
}

function setAlign(value: string): void {
  if (value !== "bochs" && value !== "full" && value !== "none") {
    throw new Error(`unknown alignment type "${value}"`);
  }
  align = value;
}

// ============================================================================
// Command line parsing
// ============================================================================

function usage(exitCode: number): never {
  console.log(`pintos, a utility for running Pintos in a simulator
Usage: pintos [OPTION...] -- [ARGUMENT...]
where each OPTION is one of the following options
  and each ARGUMENT is passed to Pintos kernel verbatim.
Architecture selection:
  --arch=ARCH              Target architecture: i386 (default) or riscv64
Simulator selection:
  --bochs                  (default) Use Bochs as simulator
  --qemu                   Use QEMU as simulator
  --player                 Use VMware Player as simulator
Debugger selection:
  --no-debug               (default) No debugger
  --monitor                Debug with simulator's monitor
  --gdb                    Debug with gdb
Display options: (default is both VGA and serial)
  -v, --no-vga             No VGA display or keyboard
  -s, --no-serial          No serial input or output
  -t, --terminal           Display VGA in terminal (Bochs only)
Timing options: (Bochs only)
  -j SEED                  Randomize timer interrupts
  -r, --realtime           Use realistic, not reproducible, timings
Testing options:
  -T, --timeout=N          Kill Pintos after N seconds CPU time or N*load_avg
                           seconds wall-clock time (whichever comes first)
  -k, --kill-on-failure    Kill Pintos a few seconds after a kernel or user
                           panic, test failure, or triple fault
Configuration options:
  -m, --mem=N              Give Pintos N MB physical RAM (default: 4)
File system commands:
  -p, --put-file=HOSTFN    Copy HOSTFN into VM, by default under same name
  -g, --get-file=GUESTFN   Copy GUESTFN out of VM, by default under same name
  -a, --as=FILENAME        Specifies guest (for -p) or host (for -g) file name
Partition options: (where PARTITION is one of: kernel filesys scratch swap)
  --PARTITION=FILE         Use a copy of FILE for the given PARTITION
  --PARTITION-size=SIZE    Create an empty PARTITION of the given SIZE in MB
  --PARTITION-from=DISK    Use of a copy of the given PARTITION in DISK
  (There is no --kernel-size, --scratch, or --scratch-from option.)
Disk configuration options:
  --make-disk=DISK         Name the new DISK and don't delete it after the run
  --disk=DISK              Also use existing DISK (may be used multiple times)
Advanced disk configuration options:
  --loader=FILE            Use FILE as bootstrap loader (default: loader.bin)
  --geometry=H,S           Use H head, S sector geometry (default: 16,63)
  --geometry=zip           Use 64 head, 32 sector geometry for USB-ZIP boot
  --align=bochs            Pad out disk to cylinder to support Bochs (default)
  --align=full             Align partition boundaries to cylinder boundary
  --align=none             Don't align partitions at all, to save space
Other options:
  -h, --help               Display this help message.`);
  process.exit(exitCode);
}

function parseCommandLine(): void {
  const args = process.argv.slice(2);

  if (args.length === 0 || (args.length === 1 && args[0] === "--help")) {
    usage(0);
  }

  // Split at --
  const dashIdx = args.indexOf("--");
  let optArgs: string[];

  if (dashIdx !== -1) {
    optArgs = args.slice(0, dashIdx);
    kernelArgs = args.slice(dashIdx + 1);
  } else {
    optArgs = [];
    kernelArgs = args;
  }

  // Parse options
  let i = 0;
  while (i < optArgs.length) {
    const arg = optArgs[i];

    // Architecture selection
    if (arg.startsWith("--arch=")) {
      const archVal = arg.substring(7);
      if (archVal !== "i386" && archVal !== "riscv64") {
        throw new Error(`Unknown architecture: ${archVal}. Use i386 or riscv64.`);
      }
      arch = archVal;
    }

    // Simulator selection
    else if (arg === "--bochs") setSim("bochs");
    else if (arg === "--qemu") setSim("qemu");
    else if (arg === "--player") setSim("player");
    else if (arg.startsWith("--sim=")) setSim(arg.substring(6) as Simulator);

    // Debugger selection
    else if (arg === "--no-debug") setDebug("none");
    else if (arg === "--monitor") setDebug("monitor");
    else if (arg === "--gdb") setDebug("gdb");
    else if (arg.startsWith("--debug=")) setDebug(arg.substring(8) as Debugger);

    // Display options
    else if (arg === "-v" || arg === "--no-vga") setVga("none");
    else if (arg === "-s" || arg === "--no-serial") serial = false;
    else if (arg === "-t" || arg === "--terminal") setVga("terminal");

    // Timing options
    else if (arg === "-r" || arg === "--realtime") setRealtime();
    else if (arg.startsWith("-j")) {
      const val = arg.length > 2 ? arg.substring(2) : optArgs[++i];
      setJitter(parseInt(val, 10));
    }

    // Testing options
    else if (arg.startsWith("-T") || arg.startsWith("--timeout=")) {
      const val = arg.startsWith("-T")
        ? arg.length > 2
          ? arg.substring(2)
          : optArgs[++i]
        : arg.substring(10);
      timeout = parseInt(val, 10);
    } else if (arg === "-k" || arg === "--kill-on-failure") killOnFailure = true;

    // Configuration
    else if (arg.startsWith("-m") || arg.startsWith("--memory=") || arg.startsWith("--mem=")) {
      const val = arg.startsWith("-m")
        ? arg.length > 2
          ? arg.substring(2)
          : optArgs[++i]
        : arg.includes("memory=")
          ? arg.substring(9)
          : arg.substring(6);
      mem = parseInt(val, 10);
    }

    // File operations
    else if (arg.startsWith("-p") || arg.startsWith("--put-file=")) {
      const val = arg.startsWith("-p")
        ? arg.length > 2
          ? arg.substring(2)
          : optArgs[++i]
        : arg.substring(11);
      addFile(puts, val);
    } else if (arg.startsWith("-g") || arg.startsWith("--get-file=")) {
      const val = arg.startsWith("-g")
        ? arg.length > 2
          ? arg.substring(2)
          : optArgs[++i]
        : arg.substring(11);
      addFile(gets, val);
    } else if (arg.startsWith("-a") || arg.startsWith("--as=")) {
      const val = arg.startsWith("-a")
        ? arg.length > 2
          ? arg.substring(2)
          : optArgs[++i]
        : arg.substring(5);
      setAs(val);
    }

    // Help
    else if (arg === "-h" || arg === "--help") usage(0);

    // Partition options
    else if (arg.startsWith("--kernel=")) setPart("KERNEL", "file", arg.substring(9));
    else if (arg.startsWith("--filesys=")) setPart("FILESYS", "file", arg.substring(10));
    else if (arg.startsWith("--swap=")) setPart("SWAP", "file", arg.substring(7));
    else if (arg.startsWith("--filesys-size=")) setPart("FILESYS", "size", arg.substring(15));
    else if (arg.startsWith("--scratch-size=")) setPart("SCRATCH", "size", arg.substring(15));
    else if (arg.startsWith("--swap-size=")) setPart("SWAP", "size", arg.substring(12));
    else if (arg.startsWith("--kernel-from=")) setPart("KERNEL", "from", arg.substring(14));
    else if (arg.startsWith("--filesys-from=")) setPart("FILESYS", "from", arg.substring(15));
    else if (arg.startsWith("--swap-from=")) setPart("SWAP", "from", arg.substring(12));

    // Disk options
    else if (arg.startsWith("--make-disk=")) {
      makeDisk = arg.substring(12);
      tmpDisk = false;
    } else if (arg.startsWith("--disk=")) setDisk(arg.substring(7));
    else if (arg.startsWith("--loader=")) loaderFn = arg.substring(9);
    else if (arg.startsWith("--geometry=")) setGeometry(arg.substring(11));
    else if (arg.startsWith("--align=")) setAlign(arg.substring(8));
    else {
      console.error(`Unknown option: ${arg}`);
      usage(1);
    }

    i++;
  }

  // Set defaults
  if (vga === undefined) {
    vga = process.env.DISPLAY ? "window" : "none";
  }

  // Warnings
  if (timeout !== undefined && debug !== "none") {
    console.log(`warning: disabling timeout with --${debug}`);
    timeout = undefined;
  }

  if (killOnFailure && !serial) {
    console.log("warning: enabling serial port for -k or --kill-on-failure");
  }

  if (sim === "bochs" && align === "none") {
    console.error("warning: setting --align=bochs for Bochs support");
    align = "bochs";
  }
}

// ============================================================================
// Disk preparation
// ============================================================================

function findDisks(): void {
  // Find kernel if not specified
  if (!parts.KERNEL) {
    const name = findFile("kernel.bin");
    if (!name) throw new Error("Cannot find kernel");
    setPart("KERNEL", "file", name);
  }

  // Try to find filesys and swap disks
  if (!parts.FILESYS) {
    const name = findFile("filesys.dsk");
    if (name) setDisk(name);
  }
  if (!parts.SWAP) {
    const name = findFile("swap.dsk");
    if (name) setDisk(name);
  }

  // Warn about missing partitions based on project
  const cwd = process.cwd();
  const projectMatch = cwd.match(/\b(threads|userprog|vm|filesys)\b/);
  if (projectMatch) {
    const project = projectMatch[1];
    if (["userprog", "vm", "filesys"].includes(project) && !parts.FILESYS) {
      console.error(
        `warning: it looks like you're running the ${project} project, but no file system partition is present`
      );
    }
    if (project === "vm" && !parts.SWAP) {
      console.error(
        `warning: it looks like you're running the ${project} project, but no swap partition is present`
      );
    }
  }

  // Create disk file
  if (!makeDisk) {
    makeDisk = `/tmp/pintos-${process.pid}.dsk`;
  } else if (fs.existsSync(makeDisk)) {
    throw new Error(`${makeDisk}: already exists`);
  }

  const diskHandle = fs.openSync(makeDisk, "w");

  // Prepare kernel arguments
  const args: string[] = [];

  // Move leading flags from kernelArgs to args
  while (kernelArgs.length > 0 && kernelArgs[0].startsWith("-")) {
    args.push(kernelArgs.shift()!);
  }

  if (puts.length > 0) args.push("extract");
  args.push(...kernelArgs);
  for (const get of gets) {
    args.push("append", get[0]);
  }

  // Build disk configuration
  const diskConfig: AssembleDiskArgs = {
    DISK: makeDisk,
    HANDLE: diskHandle,
    ALIGN: align,
    GEOMETRY: geometry,
    FORMAT: "partitioned",
    LOADER: readLoader(loaderFn),
    ARGS: args,
  };

  // Add partitions that need to be assembled
  for (const role of ROLE_ORDER) {
    const p = parts[role];
    if (p && !p.DISK) {
      diskConfig[role] = p;
    }
  }

  assembleDisk(diskConfig);

  // Add to disk list
  disks.unshift(makeDisk);
  if (disks.length > 4) {
    throw new Error(`can't use more than ${disks.length} disks`);
  }
}

function prepareScratchDisk(): void {
  if (gets.length === 0 && puts.length === 0) return;

  // Create temporary partition file
  const partFn = `/tmp/pintos-scratch-${process.pid}.part`;
  const partHandle = fs.openSync(partFn, "w+");

  // Write files to put
  for (const put of puts) {
    const srcName = put[0];
    const dstName = put[1] ?? put[0];
    putScratchFile(srcName, dstName, partHandle, partFn);
  }

  // Write end-of-archive marker
  writeFully(partHandle, partFn, Buffer.alloc(1024));

  // Calculate required size
  const existingBytes = parts.SCRATCH?.BYTES || 0;
  const size = roundUp(Math.max(gets.length * 1024 * 1024, existingBytes), 512);

  // Extend file to size
  const currentSize = fs.fstatSync(partHandle).size;
  if (currentSize < size) {
    fs.ftruncateSync(partHandle, size);
  }

  fs.closeSync(partHandle);

  // Either copy to existing partition or set as new source
  const existingPart = parts.SCRATCH;
  if (existingPart?.DISK) {
    if ((existingPart.SECTORS || 0) * 512 < size) {
      throw new Error(`${existingPart.DISK}: scratch partition too small`);
    }

    const srcHandle = fs.openSync(partFn, "r");
    const dstHandle = fs.openSync(existingPart.DISK, "r+");
    const startPos = (existingPart.START || 0) * 512;

    // Seek and copy
    const data = Buffer.alloc(size);
    fs.readSync(srcHandle, data, 0, size, 0);
    fs.writeSync(dstHandle, data, 0, size, startPos);

    fs.closeSync(srcHandle);
    fs.closeSync(dstHandle);
  } else {
    setPart("SCRATCH", "file", partFn);
  }
}

function finishScratchDisk(): void {
  if (gets.length === 0) return;

  const p = parts.SCRATCH;
  if (!p || !p.DISK) {
    console.error("No scratch partition available for file retrieval");
    return;
  }

  const partHandle = fs.openSync(p.DISK, "r");
  const startPos = (p.START || 0) * 512;

  // Seek to partition start
  fs.readSync(partHandle, Buffer.alloc(0), 0, 0, startPos);

  let ok = true;
  const partEnd = ((p.START || 0) + (p.SECTORS || 0)) * 512;

  for (const get of gets) {
    const name = get[1] ?? get[0];

    if (ok) {
      const error = getScratchFile(name, partHandle, p.DISK);
      // Check if we've overrun the partition
      // Note: simplified check, the full Perl version tracks position more carefully
      if (error) {
        console.error(`getting ${name} failed (${error})`);
        ok = false;
      }
    }

    if (!ok) {
      try {
        fs.unlinkSync(name);
      } catch {
        // Ignore if file doesn't exist
      }
    }
  }

  fs.closeSync(partHandle);
}

// ============================================================================
// Main
// ============================================================================

async function main(): Promise<void> {
  parseCommandLine();

  // RISC-V mode: run kernel.bin directly without disk setup
  if (arch === "riscv64") {
    // For RISC-V, we need kernel.bin in the current directory
    const kernelBin = findFile("kernel.bin");
    if (!kernelBin) {
      throw new Error("Cannot find kernel.bin for RISC-V mode");
    }

    // Force QEMU for RISC-V
    sim = "qemu";
    mem = Math.max(mem, 128); // RISC-V needs more memory

    await runVm({
      sim,
      arch,
      debug,
      mem,
      serial,
      vga: vga ?? "none",
      jitter,
      realtime,
      timeout,
      killOnFailure,
      disks: [],
      kernelBin,
      kernelArgs,
    });
    return;
  }

  // i386 mode: traditional disk-based setup
  prepareScratchDisk();
  findDisks();

  await runVm({
    sim,
    arch,
    debug,
    mem,
    serial,
    vga: vga!,
    jitter,
    realtime,
    timeout,
    killOnFailure,
    disks,
  });

  finishScratchDisk();

  // Clean up temporary disk
  if (tmpDisk && makeDisk) {
    try {
      fs.unlinkSync(makeDisk);
    } catch {
      // Ignore
    }
  }
}

main().catch((err) => {
  console.error(err.message);
  process.exit(1);
});
