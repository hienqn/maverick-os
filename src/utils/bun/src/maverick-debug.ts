#!/usr/bin/env bun
/**
 * maverick-debug - Agent-friendly debugger for PintOS
 *
 * Runs a PintOS test with GDB in batch mode and returns structured JSON output.
 * Designed for AI agent consumption - provides machine-readable debugging state.
 *
 * Usage:
 *   maverick-debug --test alarm-single --break thread_create --commands "bt"
 *   maverick-debug --spec debug-session.json
 */

import * as fs from "fs";
import * as path from "path";
import type { Architecture } from "./lib/types";
import type {
  DebugSpec,
  DebugResult,
  DebugStatus,
  CliArgs,
  BreakpointSpec,
  WatchpointSpec,
  MemoryDumpSpec,
  StopEvent,
  BreakpointInfo,
  WatchpointInfo,
} from "./lib/debug-types";
import { GdbSession } from "./lib/gdb-session";
import { startForDebug, type SimulatorOptions } from "./lib/simulator";
import {
  assembleDisk,
  readLoader,
  findFile,
  roundUp,
} from "./lib/disk";
import { putScratchFile } from "./lib/ustar";
import { ROLE_ORDER, type PartitionMap, type PartitionSource } from "./lib/types";

// ============================================================================
// Constants
// ============================================================================

const DEFAULT_TIMEOUT = 60;
const DEFAULT_MAX_STOPS = 10;
const DEFAULT_MEM = 4;
const GDB_PORT = 1234;

// ============================================================================
// CLI Argument Parsing
// ============================================================================

function parseArgs(argv: string[]): CliArgs {
  const args: CliArgs = {
    breaks: [],
    breakIfs: [],
    watches: [],
    rwatches: [],
    commands: [],
    memory: [],
    maxStops: DEFAULT_MAX_STOPS,
    timeout: DEFAULT_TIMEOUT,
    arch: "i386",
  };

  let i = 2; // Skip 'bun' and script name
  while (i < argv.length) {
    const arg = argv[i];

    if (arg === "--test" || arg === "-t") {
      args.test = argv[++i];
    } else if (arg === "--spec" || arg === "-s") {
      args.spec = argv[++i];
    } else if (arg === "--break" || arg === "-b") {
      args.breaks.push(argv[++i]);
    } else if (arg === "--break-if") {
      args.breakIfs.push(argv[++i]);
    } else if (arg === "--watch" || arg === "-w") {
      args.watches.push(argv[++i]);
    } else if (arg === "--rwatch") {
      args.rwatches.push(argv[++i]);
    } else if (arg === "--commands" || arg === "-c") {
      args.commands.push(...argv[++i].split(","));
    } else if (arg === "--memory" || arg === "-m") {
      args.memory.push(argv[++i]);
    } else if (arg === "--max-stops") {
      args.maxStops = parseInt(argv[++i], 10);
    } else if (arg === "--timeout" || arg === "-T") {
      args.timeout = parseInt(argv[++i], 10);
    } else if (arg === "--arch" || arg === "-a") {
      args.arch = argv[++i] as Architecture;
    } else if (arg === "--output" || arg === "-o") {
      args.output = argv[++i];
    } else if (arg === "--help" || arg === "-h") {
      printHelp();
      process.exit(0);
    } else if (arg.startsWith("--")) {
      console.error(`Unknown option: ${arg}`);
      process.exit(1);
    }

    i++;
  }

  return args;
}

function printHelp(): void {
  console.log(`maverick-debug - Agent-friendly debugger for PintOS

Usage: maverick-debug [OPTIONS]

Required (one of):
  --test, -t NAME       Run test NAME (e.g., "alarm-single")
  --spec, -s FILE       Use JSON spec file for complex sessions

Breakpoint options:
  --break, -b LOC       Set breakpoint (function, file:line, or *address)
  --break-if "LOC if C" Conditional breakpoint
  --watch, -w EXPR      Set write watchpoint
  --rwatch EXPR         Set read watchpoint

Command options:
  --commands, -c CMDS   Commands to run at each stop (comma-separated)
  --memory, -m SPEC     Memory to dump (e.g., "0xc0000000:64" or "$esp:32")
  --max-stops N         Max breakpoint hits (default: ${DEFAULT_MAX_STOPS})
  --timeout, -T SECS    Execution timeout (default: ${DEFAULT_TIMEOUT})

Output options:
  --output, -o FILE     Write JSON to FILE (default: stdout)
  --arch, -a ARCH       Architecture: i386 (default) or riscv64

Examples:
  maverick-debug --test alarm-single --break thread_create
  maverick-debug --test priority-donate-one --break lock_acquire --commands "bt,print lock->holder"
`);
}

// ============================================================================
// Build Debug Spec from CLI Args
// ============================================================================

function buildSpecFromArgs(args: CliArgs): DebugSpec {
  const breakpoints: BreakpointSpec[] = [];

  // Regular breakpoints
  for (const loc of args.breaks) {
    breakpoints.push({ location: loc });
  }

  // Conditional breakpoints (format: "location if condition")
  for (const spec of args.breakIfs) {
    const match = spec.match(/^(.+?)\s+if\s+(.+)$/);
    if (match) {
      breakpoints.push({ location: match[1], condition: match[2] });
    } else {
      breakpoints.push({ location: spec });
    }
  }

  // Watchpoints
  const watchpoints: WatchpointSpec[] = [];
  for (const expr of args.watches) {
    watchpoints.push({ expression: expr, type: "write" });
  }
  for (const expr of args.rwatches) {
    watchpoints.push({ expression: expr, type: "read" });
  }

  // Memory dumps
  const memoryDumps: MemoryDumpSpec[] = [];
  for (const spec of args.memory) {
    const match = spec.match(/^(.+):(\d+)$/);
    if (match) {
      memoryDumps.push({ address: match[1], count: parseInt(match[2], 10) });
    }
  }

  return {
    version: 1,
    test: args.test || "",
    arch: args.arch,
    timeout: args.timeout,
    breakpoints,
    watchpoints,
    memoryDumps,
    commandsAtStop: args.commands,
    maxStops: args.maxStops,
  };
}

// ============================================================================
// Find Build Directory and Kernel
// ============================================================================

interface BuildPaths {
  buildDir: string;
  kernelO: string;
  kernelBin: string;
  loaderBin: string;
  gdbMacros: string;
}

function findBuildPaths(test: string, arch: Architecture): BuildPaths {
  // Determine which component directory we're in based on test path
  const testParts = test.split("/");
  let component = "threads"; // default

  if (test.includes("userprog") || testParts[0] === "userprog") {
    component = "userprog";
  } else if (test.includes("vm") || testParts[0] === "vm") {
    component = "vm";
  } else if (test.includes("filesys") || testParts[0] === "filesys") {
    component = "filesys";
  } else if (test.includes("threads") || testParts[0] === "threads") {
    component = "threads";
  }

  // Find src directory
  let srcDir = process.cwd();
  while (srcDir !== "/" && !fs.existsSync(path.join(srcDir, "Make.config"))) {
    srcDir = path.dirname(srcDir);
  }

  if (srcDir === "/") {
    // Try relative to script location
    srcDir = path.resolve(path.dirname(process.argv[1]), "../../..");
  }

  const buildDir = path.join(srcDir, component, "build", arch);
  const kernelO = path.join(buildDir, "kernel.o");
  const kernelBin = path.join(buildDir, "kernel.bin");
  const loaderBin = path.join(buildDir, "loader.bin");
  const gdbMacros = path.join(srcDir, "misc", "gdb-macros");

  // Verify files exist
  if (!fs.existsSync(kernelO)) {
    throw new Error(`Kernel not found: ${kernelO}. Run 'make' in ${component}/ first.`);
  }

  return { buildDir, kernelO, kernelBin, loaderBin, gdbMacros };
}

// ============================================================================
// Build Test Disk
// ============================================================================

async function buildTestDisk(
  test: string,
  arch: Architecture,
  buildPaths: BuildPaths
): Promise<string[]> {
  // Extract test name from path
  const testName = test.includes("/")
    ? path.basename(test)
    : test;

  const kernelArgs = [`run ${testName}`];

  if (arch === "riscv64") {
    // RISC-V doesn't need disk assembly, just kernel.bin and args
    return kernelArgs;
  }

  // i386: Build bootable disk
  const tmpDir = fs.mkdtempSync("/tmp/maverick-debug-");
  const diskPath = path.join(tmpDir, "os.dsk");

  // Read loader
  const loader = readLoader(buildPaths.loaderBin);

  // Prepare partitions
  const parts: PartitionMap = {};

  // Kernel partition
  const kernelStat = fs.statSync(buildPaths.kernelBin);
  parts.KERNEL = {
    FILE: buildPaths.kernelBin,
    BYTES: kernelStat.size,
  };

  // Assemble disk
  // Note: assembleDisk closes the file descriptor itself
  const diskFd = fs.openSync(diskPath, "w");
  assembleDisk({
    DISK: diskPath,
    HANDLE: diskFd,
    LOADER: loader,
    ARGS: kernelArgs,
    KERNEL: parts.KERNEL,
  });

  // Return disks array for QEMU
  return [diskPath];
}

// ============================================================================
// Main Debug Session
// ============================================================================

async function runDebugSession(spec: DebugSpec): Promise<DebugResult> {
  const startTime = Date.now();
  const errors: string[] = [];
  const stops: StopEvent[] = [];
  const breakpointsSet: BreakpointInfo[] = [];
  const watchpointsSet: WatchpointInfo[] = [];

  let status: DebugStatus = "completed";
  let serialOutput = "";

  // Find build paths
  let buildPaths: BuildPaths;
  try {
    buildPaths = findBuildPaths(spec.test, spec.arch);
  } catch (err) {
    return {
      version: 1,
      status: "error",
      test: spec.test,
      arch: spec.arch,
      stops: [],
      breakpointsSet: [],
      watchpointsSet: [],
      serialOutput: "",
      executionTimeMs: Date.now() - startTime,
      errors: [(err as Error).message],
    };
  }

  // Build test disk (for i386) or get kernel args (for riscv64)
  let disks: string[] = [];
  let kernelArgs: string[] = [];

  try {
    if (spec.arch === "riscv64") {
      kernelArgs = [`run ${path.basename(spec.test)}`];
    } else {
      const result = await buildTestDisk(spec.test, spec.arch, buildPaths);
      if (spec.arch === "i386") {
        disks = result;
      }
    }
  } catch (err) {
    return {
      version: 1,
      status: "error",
      test: spec.test,
      arch: spec.arch,
      stops: [],
      breakpointsSet: [],
      watchpointsSet: [],
      serialOutput: "",
      executionTimeMs: Date.now() - startTime,
      errors: [`Failed to build test disk: ${(err as Error).message}`],
    };
  }

  // Start QEMU
  let debugHandle;
  try {
    const simOptions: SimulatorOptions = {
      sim: "qemu",
      arch: spec.arch,
      debug: "gdb",
      mem: spec.arch === "riscv64" ? 128 : DEFAULT_MEM,
      serial: true,
      vga: "none",
      timeout: spec.timeout,
      killOnFailure: false,
      disks: disks,
      kernelBin: spec.arch === "riscv64" ? buildPaths.kernelBin : undefined,
      kernelArgs: spec.arch === "riscv64" ? kernelArgs : undefined,
    };

    debugHandle = await startForDebug(simOptions);
  } catch (err) {
    return {
      version: 1,
      status: "error",
      test: spec.test,
      arch: spec.arch,
      stops: [],
      breakpointsSet: [],
      watchpointsSet: [],
      serialOutput: "",
      executionTimeMs: Date.now() - startTime,
      errors: [`Failed to start QEMU: ${(err as Error).message}`],
    };
  }

  // Create GDB session
  const gdbSession = new GdbSession({
    arch: spec.arch,
    kernelPath: buildPaths.kernelO,
    gdbMacrosPath: fs.existsSync(buildPaths.gdbMacros) ? buildPaths.gdbMacros : undefined,
    gdbPort: GDB_PORT,
    timeout: spec.timeout,
  });

  // Capture serial output from QEMU
  let qemuOutput = "";
  if (debugHandle.process.stdout) {
    captureOutput(debugHandle.process.stdout, (data) => {
      qemuOutput += data;
    });
  }

  try {
    // Wait a bit for QEMU to start GDB server
    await sleep(1500);

    // Connect GDB (QEMU is already started via startForDebug)
    await gdbSession.connectGdb();

    // Set breakpoints
    for (const bp of spec.breakpoints) {
      try {
        const info = await gdbSession.setBreakpoint(bp);
        breakpointsSet.push(info);
      } catch (err) {
        errors.push(`Failed to set breakpoint at ${bp.location}: ${(err as Error).message}`);
      }
    }

    // Set watchpoints
    for (const wp of spec.watchpoints) {
      try {
        const info = await gdbSession.setWatchpoint(wp);
        watchpointsSet.push(info);
      } catch (err) {
        errors.push(`Failed to set watchpoint on ${wp.expression}: ${(err as Error).message}`);
      }
    }

    // Main debug loop
    let stopNumber = 0;
    while (stopNumber < spec.maxStops) {
      // Continue execution
      const stopEvent = await gdbSession.continue();
      stopNumber++;
      stopEvent.stopNumber = stopNumber;

      // Read memory dumps
      for (const memSpec of spec.memoryDumps) {
        try {
          const data = await gdbSession.readMemory(memSpec);
          const key = `${memSpec.address}:${memSpec.count}`;
          stopEvent.memoryDumps[key] = data;
        } catch (err) {
          errors.push(`Failed to read memory at ${memSpec.address}: ${(err as Error).message}`);
        }
      }

      // Execute custom commands
      for (const cmd of spec.commandsAtStop) {
        try {
          const output = await gdbSession.execute(cmd);
          stopEvent.commandOutputs[cmd] = output.trim();
        } catch (err) {
          errors.push(`Failed to execute command '${cmd}': ${(err as Error).message}`);
        }
      }

      stops.push(stopEvent);

      // Check exit conditions
      if (stopEvent.reason === "exited" || stopEvent.reason === "exited-normally") {
        status = "completed";
        break;
      }

      if (stopEvent.reason === "panic") {
        status = "panic";
        break;
      }

      // Check for timeout signal
      if (stopEvent.reason === "signal" && stopEvent.signal?.name === "TIMEOUT") {
        status = "timeout";
        break;
      }
    }

    serialOutput = qemuOutput;

    // Check serial output for panic
    if (serialOutput.includes("PANIC") || serialOutput.includes("Kernel PANIC")) {
      status = "panic";
    }
  } catch (err) {
    status = "error";
    errors.push((err as Error).message);
  } finally {
    // Clean up
    try {
      await gdbSession.terminate();
    } catch {
      // Ignore cleanup errors
    }

    try {
      await debugHandle.terminate();
    } catch {
      // Ignore cleanup errors
    }
  }

  return {
    version: 1,
    status,
    test: spec.test,
    arch: spec.arch,
    stops,
    breakpointsSet,
    watchpointsSet,
    serialOutput,
    executionTimeMs: Date.now() - startTime,
    errors,
  };
}

// ============================================================================
// Main Entry Point
// ============================================================================

async function main(): Promise<void> {
  const args = parseArgs(process.argv);

  let spec: DebugSpec;

  if (args.spec) {
    // Load spec from file
    const content = fs.readFileSync(args.spec, "utf-8");
    spec = JSON.parse(content) as DebugSpec;
  } else if (args.test) {
    // Build spec from CLI args
    spec = buildSpecFromArgs(args);
  } else {
    console.error("Error: Either --test or --spec is required");
    printHelp();
    process.exit(1);
  }

  // Run debug session
  const result = await runDebugSession(spec);

  // Output result
  const output = JSON.stringify(result, null, 2);

  if (args.output) {
    fs.writeFileSync(args.output, output);
    console.error(`Results written to ${args.output}`);
  } else {
    console.log(output);
  }

  // Exit with appropriate code
  process.exit(result.status === "completed" ? 0 : 1);
}

// ============================================================================
// Utilities
// ============================================================================

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/**
 * Capture output from a readable stream
 */
function captureOutput(
  stream: ReadableStream<Uint8Array>,
  callback: (data: string) => void
): void {
  const reader = stream.getReader();
  const decoder = new TextDecoder();

  (async () => {
    try {
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        callback(decoder.decode(value, { stream: true }));
      }
    } catch {
      // Stream closed
    }
  })();
}

// Run main
main().catch((err) => {
  console.error(`Fatal error: ${err.message}`);
  process.exit(1);
});
