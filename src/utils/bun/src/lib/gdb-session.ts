/**
 * GDB session management for debug-pintos
 *
 * Manages a GDB/MI session connected to a QEMU instance running PintOS.
 * Provides high-level methods for debugging operations.
 */

import { spawn, type Subprocess } from "bun";
import type { Architecture } from "./types";
import type {
  GdbSessionConfig,
  BreakpointInfo,
  WatchpointInfo,
  StopEvent,
  StopReason,
  SourceLocation,
  Registers,
  I386Registers,
  Riscv64Registers,
  StackFrame,
  BreakpointSpec,
  WatchpointSpec,
  MemoryDumpSpec,
} from "./debug-types";
import {
  parseMiOutput,
  findResultRecord,
  findAsyncRecord,
  isResultDone,
  isResultError,
  getErrorMessage,
  collectConsoleOutput,
  parseStoppedRecord,
  type MiRecord,
} from "./gdb-mi-parser";
import { findInPath } from "./subprocess";

/**
 * GDB session for debugging PintOS
 */
export class GdbSession {
  private config: GdbSessionConfig;
  private qemuProc: Subprocess | null = null;
  private gdbProc: Subprocess<"pipe", "pipe", "pipe"> | null = null;
  private gdbStdout: string = "";
  private tokenCounter = 1;
  private serialOutput: string = "";
  private connected = false;

  constructor(config: GdbSessionConfig) {
    this.config = config;
  }

  /**
   * Start QEMU for debugging (does not block)
   */
  async startQemu(qemuCmd: string[]): Promise<void> {
    // Add GDB server flags if not present
    if (!qemuCmd.includes("-s")) {
      qemuCmd.push("-s", "-S");
    }

    this.qemuProc = spawn({
      cmd: qemuCmd,
      stdin: "pipe",
      stdout: "pipe",
      stderr: "pipe",
    });

    // Capture serial output in background
    if (this.qemuProc.stdout) {
      this.captureQemuOutput(this.qemuProc.stdout);
    }

    // Give QEMU time to start GDB server
    await sleep(500);
  }

  /**
   * Capture QEMU serial output
   */
  private async captureQemuOutput(stdout: ReadableStream<Uint8Array>): Promise<void> {
    const reader = stdout.getReader();
    const decoder = new TextDecoder();

    try {
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        this.serialOutput += decoder.decode(value, { stream: true });
      }
    } catch {
      // Stream closed
    }
  }

  /**
   * Connect GDB to the running QEMU instance
   */
  async connectGdb(): Promise<void> {
    const gdbBinary = this.findGdb();

    const gdbArgs = [
      gdbBinary,
      "--interpreter=mi3",
      "-q", // Quiet mode
      this.config.kernelPath,
    ];

    this.gdbProc = spawn({
      cmd: gdbArgs,
      stdin: "pipe",
      stdout: "pipe",
      stderr: "pipe",
    });

    // Wait for initial prompt
    await this.readUntilPrompt();

    // Set architecture to avoid register mismatch issues
    // Use interpreter-exec to run the command in console mode for better compatibility
    if (this.config.arch === "i386") {
      await this.executeCommand(`-interpreter-exec console "set architecture i386"`);
    } else if (this.config.arch === "riscv64") {
      await this.executeCommand(`-interpreter-exec console "set architecture riscv:rv64"`);
    }

    // Connect to remote target
    const connectResult = await this.executeCommand(
      `-target-select remote localhost:${this.config.gdbPort}`
    );

    if (isResultError(connectResult)) {
      const msg = getErrorMessage(connectResult);
      throw new Error(`Failed to connect to GDB server: ${msg}`);
    }

    this.connected = true;

    // Load GDB macros if available
    if (this.config.gdbMacrosPath) {
      await this.executeCommand(`source ${this.config.gdbMacrosPath}`);
    }
  }

  /**
   * Find the appropriate GDB binary for the architecture
   */
  private findGdb(): string {
    const candidates =
      this.config.arch === "riscv64"
        ? ["riscv64-unknown-elf-gdb", "riscv64-elf-gdb", "gdb-multiarch", "gdb"]
        : ["i386-elf-gdb", "i686-elf-gdb", "gdb-multiarch", "gdb"];

    for (const candidate of candidates) {
      if (findInPath(candidate)) {
        return candidate;
      }
    }

    return "gdb-multiarch"; // Fall back to gdb-multiarch which handles multiple architectures
  }

  /**
   * Execute a GDB/MI command and return parsed records
   */
  async executeCommand(command: string): Promise<MiRecord[]> {
    if (!this.gdbProc || !this.gdbProc.stdin) {
      throw new Error("GDB not connected");
    }

    const token = this.tokenCounter++;
    const fullCommand = `${token}${command}\n`;

    // Send command
    this.gdbProc.stdin.write(fullCommand);
    await this.gdbProc.stdin.flush();

    // Read response until we get the result for our token
    return await this.readUntilResult(token);
  }

  /**
   * Read GDB output until we see the prompt
   */
  private async readUntilPrompt(): Promise<MiRecord[]> {
    if (!this.gdbProc || !this.gdbProc.stdout) {
      throw new Error("GDB not connected");
    }

    const records: MiRecord[] = [];
    const reader = this.gdbProc.stdout.getReader();
    const decoder = new TextDecoder();

    try {
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;

        this.gdbStdout += decoder.decode(value, { stream: true });

        // Process complete lines
        while (this.gdbStdout.includes("\n")) {
          const newlineIdx = this.gdbStdout.indexOf("\n");
          const line = this.gdbStdout.slice(0, newlineIdx);
          this.gdbStdout = this.gdbStdout.slice(newlineIdx + 1);

          if (line.trim() === "(gdb)") {
            reader.releaseLock();
            return records;
          }

          const parsed = parseMiOutput(line);
          records.push(...parsed);
        }
      }
    } catch {
      // Stream error
    }

    reader.releaseLock();
    return records;
  }

  /**
   * Read until we get a result record with the given token
   */
  private async readUntilResult(token: number): Promise<MiRecord[]> {
    if (!this.gdbProc || !this.gdbProc.stdout) {
      throw new Error("GDB not connected");
    }

    const records: MiRecord[] = [];
    const decoder = new TextDecoder();

    const timeoutMs = this.config.timeout * 1000;
    const startTime = Date.now();

    while (Date.now() - startTime < timeoutMs) {
      // Check if we have buffered data
      while (this.gdbStdout.includes("\n")) {
        const newlineIdx = this.gdbStdout.indexOf("\n");
        const line = this.gdbStdout.slice(0, newlineIdx);
        this.gdbStdout = this.gdbStdout.slice(newlineIdx + 1);

        if (line.trim() === "(gdb)") {
          continue;
        }

        const parsed = parseMiOutput(line);
        records.push(...parsed);

        // Check if we got our result
        const result = parsed.find((r) => r.type === "result" && r.token === token);
        if (result) {
          return records;
        }
      }

      // Read more data with timeout
      const chunk = await this.readWithTimeout(100);
      if (chunk) {
        this.gdbStdout += chunk;
      }
    }

    throw new Error("Timeout waiting for GDB response");
  }

  /**
   * Read from GDB stdout with timeout
   */
  private async readWithTimeout(timeoutMs: number): Promise<string | null> {
    if (!this.gdbProc || !this.gdbProc.stdout) {
      return null;
    }

    const reader = this.gdbProc.stdout.getReader();
    const decoder = new TextDecoder();

    try {
      const result = await Promise.race([
        reader.read(),
        sleep(timeoutMs).then(() => ({ done: true, value: undefined, timeout: true })),
      ]);

      if ("timeout" in result) {
        reader.releaseLock();
        return null;
      }

      if (result.done) {
        reader.releaseLock();
        return null;
      }

      reader.releaseLock();
      return decoder.decode(result.value, { stream: true });
    } catch {
      reader.releaseLock();
      return null;
    }
  }

  /**
   * Set a breakpoint
   */
  async setBreakpoint(spec: BreakpointSpec): Promise<BreakpointInfo> {
    let cmd = `-break-insert`;
    if (spec.temporary) {
      cmd += " -t";
    }
    if (spec.condition) {
      cmd += ` -c "${spec.condition}"`;
    }
    cmd += ` ${spec.location}`;

    const records = await this.executeCommand(cmd);

    if (isResultError(records)) {
      const msg = getErrorMessage(records);
      throw new Error(`Failed to set breakpoint at ${spec.location}: ${msg}`);
    }

    const result = findResultRecord(records);
    const bkpt = result?.data?.bkpt as Record<string, unknown> | undefined;

    return {
      id: parseInt((bkpt?.number as string) || "0", 10),
      location: spec.location,
      address: (bkpt?.addr as string) || "0x0",
      condition: spec.condition || null,
      hitCount: 0,
    };
  }

  /**
   * Set a watchpoint
   */
  async setWatchpoint(spec: WatchpointSpec): Promise<WatchpointInfo> {
    let cmd: string;
    switch (spec.type) {
      case "read":
        cmd = `-break-watch -r ${spec.expression}`;
        break;
      case "access":
        cmd = `-break-watch -a ${spec.expression}`;
        break;
      case "write":
      default:
        cmd = `-break-watch ${spec.expression}`;
        break;
    }

    const records = await this.executeCommand(cmd);

    if (isResultError(records)) {
      const msg = getErrorMessage(records);
      throw new Error(`Failed to set watchpoint on ${spec.expression}: ${msg}`);
    }

    const result = findResultRecord(records);
    const wpt = result?.data?.wpt as Record<string, unknown> | undefined;

    return {
      id: parseInt((wpt?.number as string) || "0", 10),
      expression: spec.expression,
      type: spec.type,
    };
  }

  /**
   * Continue execution until next stop
   */
  async continue(): Promise<StopEvent> {
    const records = await this.executeCommand("-exec-continue");

    // Wait for stopped async record
    return await this.waitForStop();
  }

  /**
   * Wait for execution to stop
   */
  private async waitForStop(): Promise<StopEvent> {
    const timeoutMs = this.config.timeout * 1000;
    const startTime = Date.now();
    const allRecords: MiRecord[] = [];

    while (Date.now() - startTime < timeoutMs) {
      // Read more data
      const chunk = await this.readWithTimeout(100);
      if (chunk) {
        this.gdbStdout += chunk;
      }

      // Process buffered lines
      while (this.gdbStdout.includes("\n")) {
        const newlineIdx = this.gdbStdout.indexOf("\n");
        const line = this.gdbStdout.slice(0, newlineIdx);
        this.gdbStdout = this.gdbStdout.slice(newlineIdx + 1);

        const parsed = parseMiOutput(line);
        allRecords.push(...parsed);

        // Check for stopped record
        const stopped = parsed.find((r) => r.type === "exec" && r.class === "stopped");
        if (stopped) {
          return await this.buildStopEvent(stopped, allRecords.length);
        }
      }
    }

    // Timeout - return a timeout stop event
    return {
      stopNumber: 0,
      reason: "signal" as StopReason,
      signal: { name: "TIMEOUT", meaning: "Execution timeout" },
      location: { function: "??", file: "??", line: 0, address: "0x0" },
      arch: this.config.arch,
      registers: this.getEmptyRegisters(),
      backtrace: [],
      memoryDumps: {},
      commandOutputs: {},
    };
  }

  /**
   * Build a stop event from a stopped record
   */
  private async buildStopEvent(stoppedRecord: MiRecord, stopNumber: number): Promise<StopEvent> {
    const parsed = parseStoppedRecord(stoppedRecord);

    // Map GDB reason to our StopReason
    let reason: StopReason = "signal";
    if (parsed?.reason === "breakpoint-hit") reason = "breakpoint-hit";
    else if (parsed?.reason === "watchpoint-trigger") reason = "watchpoint-trigger";
    else if (parsed?.reason === "watchpoint-scope") reason = "watchpoint-scope";
    else if (parsed?.reason === "exited") reason = "exited";
    else if (parsed?.reason === "exited-normally") reason = "exited-normally";

    // Check for panic in serial output
    if (this.serialOutput.includes("PANIC") || this.serialOutput.includes("Kernel PANIC")) {
      reason = "panic";
    }

    const location: SourceLocation = {
      function: parsed?.frame?.func || "??",
      file: parsed?.frame?.file || "??",
      line: parsed?.frame?.line || 0,
      address: parsed?.frame?.addr || "0x0",
    };

    const event: StopEvent = {
      stopNumber,
      reason,
      location,
      arch: this.config.arch,
      registers: await this.getRegisters(),
      backtrace: await this.getBacktrace(),
      memoryDumps: {},
      commandOutputs: {},
    };

    // Add breakpoint info if applicable
    if (parsed?.bkptno) {
      event.breakpointId = parsed.bkptno;
    }

    // Add watchpoint info if applicable
    if (parsed?.wpt && parsed?.value) {
      event.watchpoint = {
        id: parsed.wpt.number,
        expression: parsed.wpt.exp,
        oldValue: parsed.value.old,
        newValue: parsed.value.new,
      };
    }

    // Add signal info if applicable
    if (parsed?.signalName) {
      event.signal = {
        name: parsed.signalName,
        meaning: parsed.signalMeaning || "",
      };
    }

    return event;
  }

  /**
   * Get current register values
   */
  async getRegisters(): Promise<Registers> {
    const records = await this.executeCommand("-data-list-register-values x");

    if (isResultError(records)) {
      return this.getEmptyRegisters();
    }

    const result = findResultRecord(records);
    const registerValues = result?.data?.["register-values"] as Array<{
      number: string;
      value: string;
    }>;

    if (!registerValues) {
      return this.getEmptyRegisters();
    }

    // Map register numbers to names based on architecture
    const regMap = new Map<number, string>();
    for (const reg of registerValues) {
      regMap.set(parseInt(reg.number, 10), reg.value);
    }

    if (this.config.arch === "riscv64") {
      return this.buildRiscvRegisters(regMap);
    } else {
      return this.buildI386Registers(regMap);
    }
  }

  /**
   * Build i386 register object
   */
  private buildI386Registers(regMap: Map<number, string>): I386Registers {
    // GDB i386 register numbers (standard order)
    return {
      eax: regMap.get(0) || "0x0",
      ecx: regMap.get(1) || "0x0",
      edx: regMap.get(2) || "0x0",
      ebx: regMap.get(3) || "0x0",
      esp: regMap.get(4) || "0x0",
      ebp: regMap.get(5) || "0x0",
      esi: regMap.get(6) || "0x0",
      edi: regMap.get(7) || "0x0",
      eip: regMap.get(8) || "0x0",
      eflags: regMap.get(9) || "0x0",
      cs: regMap.get(10) || "0x0",
      ss: regMap.get(11) || "0x0",
      ds: regMap.get(12) || "0x0",
      es: regMap.get(13) || "0x0",
      fs: regMap.get(14) || "0x0",
      gs: regMap.get(15) || "0x0",
    };
  }

  /**
   * Build RISC-V register object
   */
  private buildRiscvRegisters(regMap: Map<number, string>): Riscv64Registers {
    // RISC-V register numbers (standard ABI order)
    return {
      pc: regMap.get(32) || "0x0",
      ra: regMap.get(1) || "0x0",
      sp: regMap.get(2) || "0x0",
      gp: regMap.get(3) || "0x0",
      tp: regMap.get(4) || "0x0",
      t0: regMap.get(5) || "0x0",
      t1: regMap.get(6) || "0x0",
      t2: regMap.get(7) || "0x0",
      s0: regMap.get(8) || "0x0",
      s1: regMap.get(9) || "0x0",
      a0: regMap.get(10) || "0x0",
      a1: regMap.get(11) || "0x0",
      a2: regMap.get(12) || "0x0",
      a3: regMap.get(13) || "0x0",
      a4: regMap.get(14) || "0x0",
      a5: regMap.get(15) || "0x0",
      a6: regMap.get(16) || "0x0",
      a7: regMap.get(17) || "0x0",
      s2: regMap.get(18) || "0x0",
      s3: regMap.get(19) || "0x0",
      s4: regMap.get(20) || "0x0",
      s5: regMap.get(21) || "0x0",
      s6: regMap.get(22) || "0x0",
      s7: regMap.get(23) || "0x0",
      s8: regMap.get(24) || "0x0",
      s9: regMap.get(25) || "0x0",
      s10: regMap.get(26) || "0x0",
      s11: regMap.get(27) || "0x0",
      t3: regMap.get(28) || "0x0",
      t4: regMap.get(29) || "0x0",
      t5: regMap.get(30) || "0x0",
      t6: regMap.get(31) || "0x0",
    };
  }

  /**
   * Get empty register set for timeout/error cases
   */
  private getEmptyRegisters(): Registers {
    if (this.config.arch === "riscv64") {
      return {
        pc: "0x0",
        ra: "0x0",
        sp: "0x0",
        gp: "0x0",
        tp: "0x0",
        t0: "0x0",
        t1: "0x0",
        t2: "0x0",
        t3: "0x0",
        t4: "0x0",
        t5: "0x0",
        t6: "0x0",
        s0: "0x0",
        s1: "0x0",
        s2: "0x0",
        s3: "0x0",
        s4: "0x0",
        s5: "0x0",
        s6: "0x0",
        s7: "0x0",
        s8: "0x0",
        s9: "0x0",
        s10: "0x0",
        s11: "0x0",
        a0: "0x0",
        a1: "0x0",
        a2: "0x0",
        a3: "0x0",
        a4: "0x0",
        a5: "0x0",
        a6: "0x0",
        a7: "0x0",
      } as Riscv64Registers;
    }
    return {
      eax: "0x0",
      ebx: "0x0",
      ecx: "0x0",
      edx: "0x0",
      esi: "0x0",
      edi: "0x0",
      ebp: "0x0",
      esp: "0x0",
      eip: "0x0",
      eflags: "0x0",
      cs: "0x0",
      ds: "0x0",
      es: "0x0",
      fs: "0x0",
      gs: "0x0",
      ss: "0x0",
    } as I386Registers;
  }

  /**
   * Get current backtrace
   */
  async getBacktrace(): Promise<StackFrame[]> {
    const records = await this.executeCommand("-stack-list-frames");

    if (isResultError(records)) {
      return [];
    }

    const result = findResultRecord(records);
    const stack = result?.data?.stack as Array<{ frame: Record<string, unknown> }>;

    if (!stack) {
      return [];
    }

    return stack.map((item) => {
      const f = item.frame || item;
      return {
        frame: parseInt((f.level as string) || "0", 10),
        function: (f.func as string) || "??",
        file: (f.file as string) || "??",
        line: parseInt((f.line as string) || "0", 10),
        address: (f.addr as string) || "0x0",
      };
    });
  }

  /**
   * Read memory at specified address
   */
  async readMemory(spec: MemoryDumpSpec): Promise<string[]> {
    // Resolve address (may be register like $esp)
    const address = spec.address.startsWith("$")
      ? spec.address
      : spec.address.startsWith("0x")
        ? spec.address
        : `0x${spec.address}`;

    const wordSize = this.config.arch === "riscv64" ? 8 : 4;
    const records = await this.executeCommand(
      `-data-read-memory ${address} x ${wordSize} 1 ${spec.count}`
    );

    if (isResultError(records)) {
      return [];
    }

    const result = findResultRecord(records);
    const memory = result?.data?.memory as Array<{ data: string[] }>;

    if (!memory || memory.length === 0) {
      return [];
    }

    return memory[0].data || [];
  }

  /**
   * Execute an arbitrary GDB command and return output
   */
  async execute(command: string): Promise<string> {
    const records = await this.executeCommand(`-interpreter-exec console "${command}"`);
    return collectConsoleOutput(records);
  }

  /**
   * Get accumulated serial output from QEMU
   */
  getSerialOutput(): string {
    return this.serialOutput;
  }

  /**
   * Terminate the debug session
   */
  async terminate(): Promise<void> {
    // Quit GDB
    if (this.gdbProc) {
      try {
        await this.executeCommand("-gdb-exit");
      } catch {
        // Ignore errors during cleanup
      }
      this.gdbProc.kill();
      this.gdbProc = null;
    }

    // Kill QEMU
    if (this.qemuProc) {
      this.qemuProc.kill();
      this.qemuProc = null;
    }

    this.connected = false;
  }

  /**
   * Check if session is connected
   */
  isConnected(): boolean {
    return this.connected;
  }
}

/**
 * Sleep for specified milliseconds
 */
function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
