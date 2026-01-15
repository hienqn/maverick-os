/**
 * PintOS utilities - subprocess management
 *
 * Handles running simulators with timeout, signal relay, and output filtering.
 */

import { spawn, type Subprocess } from "bun";
import * as fs from "fs";
import * as os from "os";

/**
 * Options for xsystem
 */
export interface XSystemOptions {
  timeout?: number; // Timeout in seconds
  killOnFailure?: boolean; // Kill early on panic/failure
  startTime?: number; // Start time for timeout messages
}

/**
 * Get system load average (or 1.0 if not available)
 */
export function getLoadAverage(): number {
  try {
    const loadavg = os.loadavg();
    return loadavg[0] >= 1.0 ? loadavg[0] : 1.0;
  } catch {
    return 1.0;
  }
}

/**
 * Run a command with subprocess management
 *
 * Features:
 * - Timeout handling (wall-clock time adjusted by load average)
 * - Early termination on panic/failure patterns (if killOnFailure is set)
 * - Signal relay (SIGINT, SIGTERM)
 */
export async function xsystem(
  cmd: string[],
  options: XSystemOptions = {}
): Promise<number> {
  const { timeout, killOnFailure, startTime = Date.now() / 1000 } = options;

  // Calculate wall-clock timeout
  const wallTimeout = timeout ? timeout * getLoadAverage() + 1 : undefined;

  let proc: Subprocess<"pipe", "pipe", "inherit">;
  let killed = false;
  let cause: string | undefined;

  // Create subprocess
  proc = spawn({
    cmd,
    stdin: "pipe",
    stdout: "pipe",
    stderr: "inherit",
  });

  // Set up signal handlers
  const cleanup = () => {
    if (!killed) {
      killed = true;
      proc.kill();
    }
  };

  process.on("SIGINT", cleanup);
  process.on("SIGTERM", cleanup);

  // Set up timeout
  let timeoutHandle: Timer | undefined;
  if (wallTimeout) {
    timeoutHandle = setTimeout(() => {
      if (!killed) {
        killed = true;
        proc.kill("SIGINT");
        const elapsed = Math.floor(Date.now() / 1000 - startTime);
        const loadAvg = getLoadAverage().toFixed(2);
        console.log(
          `\nTIMEOUT after ${elapsed} seconds of wall-clock time - load average: ${loadAvg}`
        );
      }
    }, wallTimeout * 1000);
  }

  // Process output
  if (killOnFailure && proc.stdout) {
    let buffer = "";
    let boots = 0;

    const reader = proc.stdout.getReader();
    const decoder = new TextDecoder();

    try {
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;

        const chunk = decoder.decode(value, { stream: true });
        buffer += chunk;
        process.stdout.write(chunk);

        // Process complete lines
        let newlineIdx: number;
        while ((newlineIdx = buffer.indexOf("\n")) >= 0) {
          const line = buffer.slice(0, newlineIdx + 1);
          buffer = buffer.slice(newlineIdx + 1);

          if (cause) continue;

          // Check for failure patterns
          if (/Kernel PANIC|User process ABORT/i.test(line)) {
            cause = line.includes("PANIC") ? "kernel panic" : "user process abort";
            scheduleEarlyKill();
          } else if (/Pintos booting/.test(line)) {
            boots++;
            if (boots > 1) {
              cause = "triple fault";
              scheduleEarlyKill();
            }
          } else if (/FAILED/.test(line)) {
            cause = "test failure";
            scheduleEarlyKill();
          }
        }
      }

      // Print any remaining buffer
      if (buffer) {
        process.stdout.write(buffer);
      }
    } catch (err) {
      // Reader closed, subprocess ended
    }

    function scheduleEarlyKill() {
      setTimeout(() => {
        if (!killed) {
          killed = true;
          proc.kill("SIGINT");
          console.log(`Simulation terminated due to ${cause}.`);
        }
      }, 5000);
    }
  } else if (proc.stdout) {
    // Just pipe stdout through
    const reader = proc.stdout.getReader();
    const decoder = new TextDecoder();

    try {
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        process.stdout.write(decoder.decode(value, { stream: true }));
      }
    } catch {
      // Reader closed
    }
  }

  // Wait for process to exit
  const exitCode = await proc.exited;

  // Clean up
  if (timeoutHandle) clearTimeout(timeoutHandle);
  process.off("SIGINT", cleanup);
  process.off("SIGTERM", cleanup);

  // Special case: QEMU's isa-debug-exit uses 0x63 as clean exit
  if (exitCode === 0x63) {
    return 0;
  }

  return exitCode;
}

/**
 * Run a command and print it first
 */
export async function runCommand(cmd: string[], options: XSystemOptions = {}): Promise<void> {
  console.log(cmd.join(" "));
  const exitCode = await xsystem(cmd, options);
  if (exitCode !== 0) {
    throw new Error("command failed");
  }
}

/**
 * Check if stdin is a TTY
 */
export function isatty(): boolean {
  return process.stdin.isTTY === true;
}

/**
 * Find a program in PATH
 */
export function findInPath(program: string): string | null {
  const pathEnv = process.env.PATH || "";
  for (const dir of pathEnv.split(":")) {
    const fullPath = `${dir}/${program}`;
    try {
      fs.accessSync(fullPath, fs.constants.X_OK);
      return program;
    } catch {
      // Not found in this directory
    }
  }
  return null;
}
