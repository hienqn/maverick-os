/**
 * PintOS debug-pintos tool - type definitions
 *
 * Defines interfaces for GDB/MI debugging sessions, including
 * breakpoints, watchpoints, registers, backtraces, and memory dumps.
 */

import type { Architecture } from "./types";

/**
 * Debug session specification (input)
 */
export interface DebugSpec {
  version: 1;
  test: string;
  arch: Architecture;
  timeout: number;
  breakpoints: BreakpointSpec[];
  watchpoints: WatchpointSpec[];
  memoryDumps: MemoryDumpSpec[];
  commandsAtStop: string[];
  maxStops: number;
  /** Number of source line steps to take after each breakpoint */
  stepCount?: number;
  /** Number of instruction steps to take after each breakpoint */
  stepiCount?: number;
}

/**
 * Breakpoint specification
 */
export interface BreakpointSpec {
  location: string; // function name, file:line, or *address
  condition?: string; // conditional breakpoint expression
  temporary?: boolean; // delete after first hit
}

/**
 * Watchpoint specification
 */
export interface WatchpointSpec {
  expression: string;
  type: "write" | "read" | "access";
}

/**
 * Memory dump specification
 */
export interface MemoryDumpSpec {
  address: string; // hex address or register like "$esp"
  count: number; // number of words to dump
}

/**
 * Debug session result (output)
 */
export interface DebugResult {
  version: 1;
  status: DebugStatus;
  test: string;
  arch: Architecture;
  stops: StopEvent[];
  breakpointsSet: BreakpointInfo[];
  watchpointsSet: WatchpointInfo[];
  serialOutput: string;
  executionTimeMs: number;
  errors: string[];
}

export type DebugStatus = "completed" | "timeout" | "panic" | "error";

/**
 * Information about a set breakpoint
 */
export interface BreakpointInfo {
  id: number;
  location: string;
  address: string;
  condition: string | null;
  hitCount: number;
}

/**
 * Information about a set watchpoint
 */
export interface WatchpointInfo {
  id: number;
  expression: string;
  type: "write" | "read" | "access";
}

/**
 * A single stop event during debugging
 */
export interface StopEvent {
  stopNumber: number;
  reason: StopReason;
  breakpointId?: number;
  watchpoint?: WatchpointTrigger;
  signal?: SignalInfo;
  location: SourceLocation;
  arch: Architecture;
  registers: Registers;
  backtrace: StackFrame[];
  memoryDumps: Record<string, string[]>;
  commandOutputs: Record<string, string>;
  /** Source code context around the current location */
  sourceContext?: SourceContext;
  /** Disassembly around the current instruction */
  disassembly?: DisassemblyLine[];
}

/**
 * Source code context around a location
 */
export interface SourceContext {
  /** Source file path */
  file: string;
  /** Lines of source code with line numbers */
  lines: SourceLine[];
}

/**
 * A single line of source code
 */
export interface SourceLine {
  lineNumber: number;
  text: string;
  /** Whether this is the current execution line */
  isCurrent: boolean;
}

/**
 * A single line of disassembly
 */
export interface DisassemblyLine {
  address: string;
  funcName: string;
  offset: number;
  instruction: string;
  /** Whether this is the current instruction */
  isCurrent: boolean;
}

export type StopReason =
  | "breakpoint-hit"
  | "watchpoint-trigger"
  | "watchpoint-scope"
  | "signal"
  | "exited"
  | "exited-normally"
  | "panic";

/**
 * Watchpoint trigger information
 */
export interface WatchpointTrigger {
  id: number;
  expression: string;
  oldValue: string;
  newValue: string;
}

/**
 * Signal information
 */
export interface SignalInfo {
  name: string;
  meaning: string;
}

/**
 * Source code location
 */
export interface SourceLocation {
  function: string;
  file: string;
  line: number;
  address: string;
}

/**
 * Register values - architecture-specific
 */
export type Registers = I386Registers | Riscv64Registers;

/**
 * i386 register set
 */
export interface I386Registers {
  eax: string;
  ebx: string;
  ecx: string;
  edx: string;
  esi: string;
  edi: string;
  ebp: string;
  esp: string;
  eip: string;
  eflags: string;
  cs: string;
  ds: string;
  es: string;
  fs: string;
  gs: string;
  ss: string;
}

/**
 * RISC-V 64-bit register set
 */
export interface Riscv64Registers {
  // Program counter
  pc: string;
  // Return address
  ra: string;
  // Stack pointer
  sp: string;
  // Global pointer
  gp: string;
  // Thread pointer
  tp: string;
  // Temporaries
  t0: string;
  t1: string;
  t2: string;
  t3: string;
  t4: string;
  t5: string;
  t6: string;
  // Saved registers (frame pointer is s0/fp)
  s0: string; // Also known as fp (frame pointer)
  s1: string;
  s2: string;
  s3: string;
  s4: string;
  s5: string;
  s6: string;
  s7: string;
  s8: string;
  s9: string;
  s10: string;
  s11: string;
  // Function arguments
  a0: string;
  a1: string;
  a2: string;
  a3: string;
  a4: string;
  a5: string;
  a6: string;
  a7: string;
}

/**
 * Stack frame in backtrace
 */
export interface StackFrame {
  frame: number;
  function: string;
  file: string;
  line: number;
  address: string;
  args?: Record<string, string>;
}

/**
 * GDB/MI record types
 */
export type MiRecordType = "result" | "exec" | "status" | "notify" | "console" | "target" | "log";

/**
 * Parsed GDB/MI record
 */
export interface MiRecord {
  type: MiRecordType;
  token?: number;
  class?: string; // "done", "running", "connected", "error", "exit", "stopped", etc.
  data?: Record<string, unknown>;
  text?: string; // For stream records
}

/**
 * GDB/MI result classes
 */
export type MiResultClass = "done" | "running" | "connected" | "error" | "exit";

/**
 * GDB/MI async classes
 */
export type MiAsyncClass =
  | "stopped"
  | "running"
  | "thread-group-added"
  | "thread-group-started"
  | "thread-created"
  | "library-loaded"
  | "breakpoint-modified";

/**
 * Configuration for GDB session
 */
export interface GdbSessionConfig {
  arch: Architecture;
  kernelPath: string;
  gdbMacrosPath?: string;
  gdbPort: number;
  timeout: number;
}

/**
 * CLI arguments parsed from command line
 */
export interface CliArgs {
  test?: string;
  spec?: string;
  breaks: string[];
  breakIfs: string[];
  watches: string[];
  rwatches: string[];
  commands: string[];
  evals: string[];
  memory: string[];
  maxStops: number;
  timeout: number;
  arch: Architecture;
  output?: string;
  stepCount: number;
  stepiCount: number;
}
