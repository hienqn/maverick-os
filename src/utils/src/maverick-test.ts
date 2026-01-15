#!/usr/bin/env bun
/**
 * maverick-test - Unified test runner for PintOS with structured JSON output
 *
 * Combines running a test and checking results into a single command.
 * Designed for AI agent consumption - provides machine-readable test results.
 *
 * Usage:
 *   maverick-test --test alarm-single
 *   maverick-test --test alarm-single --json
 */

import * as fs from "fs";
import * as path from "path";
import type { Architecture } from "./lib/types";
import type { StructuredTestResult, DiffEntry } from "./tests/types";
import { spawn, type Subprocess } from "bun";

// ============================================================================
// Types
// ============================================================================

interface MaverickTestResult {
  version: 1;
  test: string;
  arch: Architecture;
  verdict: "PASS" | "FAIL";
  executionTimeMs: number;
  output: string[];
  coreOutput: string[];
  errors: string[];
  diff?: DiffEntry[];
  panic?: {
    message: string;
    callStack?: string;
    backtrace?: string;
  };
}

interface CliArgs {
  test?: string;
  arch: Architecture;
  timeout: number;
  json: boolean;
}

// ============================================================================
// Constants
// ============================================================================

const DEFAULT_TIMEOUT = 60;

// ============================================================================
// CLI Argument Parsing
// ============================================================================

function parseArgs(argv: string[]): CliArgs {
  const args: CliArgs = {
    arch: "i386",
    timeout: DEFAULT_TIMEOUT,
    json: false,
  };

  let i = 2; // Skip 'bun' and script name
  while (i < argv.length) {
    const arg = argv[i];

    if (arg === "--test" || arg === "-t") {
      args.test = argv[++i];
    } else if (arg === "--arch" || arg === "-a") {
      args.arch = argv[++i] as Architecture;
    } else if (arg === "--timeout" || arg === "-T") {
      args.timeout = parseInt(argv[++i], 10);
    } else if (arg === "--json") {
      args.json = true;
    } else if (arg === "--help" || arg === "-h") {
      printHelp();
      process.exit(0);
    } else if (arg.startsWith("--")) {
      console.error(`Unknown option: ${arg}`);
      process.exit(1);
    } else if (!args.test) {
      // Positional argument - treat as test name
      args.test = arg;
    }

    i++;
  }

  return args;
}

function printHelp(): void {
  console.log(`maverick-test - Unified test runner for PintOS

Usage: maverick-test [OPTIONS] [TEST-NAME]

Options:
  --test, -t NAME       Test to run (e.g., "alarm-single")
  --arch, -a ARCH       Architecture: i386 (default) or riscv64
  --timeout, -T SECS    Execution timeout (default: ${DEFAULT_TIMEOUT})
  --json                Output structured JSON (default: text output)
  -h, --help            Show this help message

Examples:
  maverick-test alarm-single
  maverick-test --test priority-donate-one --json
  maverick-test --arch riscv64 alarm-multiple
`);
}

// ============================================================================
// Find Build Directory
// ============================================================================

interface BuildPaths {
  srcDir: string;
  buildDir: string;
  testDir: string;
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
  const searchPaths = [
    process.cwd(),
    path.dirname(process.cwd()),
    path.resolve(path.dirname(process.argv[1]), "../../.."),
    "/home/workspace/group0/src",
  ];

  let srcDir: string | null = null;
  for (const searchPath of searchPaths) {
    let candidate = searchPath;
    for (let i = 0; i < 5 && candidate !== "/"; i++) {
      if (fs.existsSync(path.join(candidate, "Make.config"))) {
        srcDir = candidate;
        break;
      }
      candidate = path.dirname(candidate);
    }
    if (srcDir) break;
  }

  if (!srcDir) {
    throw new Error(
      `Could not find PintOS src directory (looking for Make.config).`
    );
  }

  const buildDir = path.join(srcDir, component, "build", arch);
  const testDir = path.join(buildDir, "tests", component);

  // Verify build exists
  if (!fs.existsSync(path.join(buildDir, "kernel.o"))) {
    throw new Error(
      `Kernel not found. Run 'make ARCH=${arch}' in ${path.join(srcDir, component)}/ first.`
    );
  }

  return { srcDir, buildDir, testDir };
}

// ============================================================================
// Run Test
// ============================================================================

async function runTest(
  test: string,
  arch: Architecture,
  buildPaths: BuildPaths,
  timeout: number
): Promise<{ output: string[]; exitCode: number }> {
  // Extract test name from path
  const testName = test.includes("/") ? path.basename(test) : test;

  // Run pintos with the test
  const pintosPath = path.join(buildPaths.srcDir, "utils", "bin", "pintos");
  const pintosArgs = [
    "--qemu",
    `--timeout=${timeout}`,
    "-k", // kill on failure
    "--",
    `run ${testName}`,
  ];

  const proc = spawn({
    cmd: [pintosPath, ...pintosArgs],
    cwd: buildPaths.buildDir,
    stdout: "pipe",
    stderr: "pipe",
  });

  // Capture output
  const output: string[] = [];
  const decoder = new TextDecoder();

  if (proc.stdout) {
    const reader = proc.stdout.getReader();
    try {
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        const text = decoder.decode(value, { stream: true });
        output.push(...text.split("\n").filter((l) => l.length > 0));
      }
    } catch {
      // Stream closed
    }
  }

  const exitCode = await proc.exited;
  return { output, exitCode };
}

// ============================================================================
// Check Results
// ============================================================================

function extractCoreOutput(output: string[]): string[] {
  let start = -1;
  let end = -1;

  for (let i = 0; i < output.length; i++) {
    const match = output[i].match(/^Executing '(\S+).*':$/);
    if (match) {
      start = i + 1;
      break;
    }
  }

  if (start >= 0) {
    for (let i = start; i < output.length; i++) {
      if (/^Execution of '.*' complete\.$/.test(output[i])) {
        end = i - 1;
        break;
      }
    }
  }

  if (start < 0 || end < 0) {
    return [];
  }

  return output.slice(start, end + 1);
}

function checkForPanic(output: string[]): MaverickTestResult["panic"] | undefined {
  const panicLine = output.find((line) => /PANIC/.test(line));
  if (!panicLine) return undefined;

  const panicStart = panicLine.indexOf("PANIC");
  const message = panicLine.substring(panicStart);

  let callStack: string | undefined;
  const stackLine = output.find((line) => /Call stack:/.test(line));
  if (stackLine) {
    const match = stackLine.match(/Call stack:((?:\s+0x[0-9a-f]+)+)/);
    if (match) {
      callStack = match[1].trim();
    }
  }

  return { message, callStack };
}

function checkOutput(
  output: string[],
  testName: string,
  buildPaths: BuildPaths
): { verdict: "PASS" | "FAIL"; errors: string[]; diff?: DiffEntry[] } {
  const errors: string[] = [];

  // Check for basic issues
  if (output.length === 0) {
    return { verdict: "FAIL", errors: ["No output produced"] };
  }

  // Check for boot
  if (!output.some((line) => /Pintos booting/.test(line))) {
    return { verdict: "FAIL", errors: ["Kernel did not boot"] };
  }

  // Check for panic
  if (output.some((line) => /PANIC/.test(line))) {
    const panic = checkForPanic(output);
    return { verdict: "FAIL", errors: [`Kernel panic: ${panic?.message || "unknown"}`] };
  }

  // Check for FAIL keyword
  if (output.some((line) => line.includes("FAIL"))) {
    return { verdict: "FAIL", errors: ["Test reported FAIL"] };
  }

  // Check for proper shutdown
  if (!output.some((line) => /Powering off/.test(line))) {
    return { verdict: "FAIL", errors: ["Kernel did not shut down properly"] };
  }

  // Try to load expected output from .test.json
  const testJsonPath = findTestSpec(testName, buildPaths);
  if (testJsonPath && fs.existsSync(testJsonPath)) {
    try {
      const spec = JSON.parse(fs.readFileSync(testJsonPath, "utf-8"));
      if (spec.expected && spec.type === "expected") {
        const coreOutput = extractCoreOutput(output);
        const expected = spec.expected;

        // Check for match
        if (arraysEqual(coreOutput, expected)) {
          return { verdict: "PASS", errors: [] };
        }

        // Generate diff
        const diff = generateDiff(expected, coreOutput);
        return {
          verdict: "FAIL",
          errors: ["Output does not match expected"],
          diff,
        };
      }
    } catch {
      // Failed to parse spec, continue with basic checks
    }
  }

  // If we got here with no FAIL/PANIC, assume pass
  return { verdict: "PASS", errors: [] };
}

function findTestSpec(testName: string, buildPaths: BuildPaths): string | null {
  // Try common locations
  const locations = [
    path.join(buildPaths.testDir, `${testName}.test.json`),
    path.join(buildPaths.buildDir, "tests", "threads", `${testName}.test.json`),
    path.join(buildPaths.buildDir, "tests", "userprog", `${testName}.test.json`),
    path.join(buildPaths.buildDir, "tests", "vm", `${testName}.test.json`),
    path.join(buildPaths.buildDir, "tests", "filesys", `${testName}.test.json`),
  ];

  for (const loc of locations) {
    if (fs.existsSync(loc)) {
      return loc;
    }
  }

  return null;
}

function arraysEqual(a: string[], b: string[]): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) return false;
  }
  return true;
}

function generateDiff(expected: string[], actual: string[]): DiffEntry[] {
  const diff: DiffEntry[] = [];
  const maxLen = Math.max(expected.length, actual.length);

  for (let i = 0; i < maxLen; i++) {
    const exp = i < expected.length ? expected[i] : undefined;
    const act = i < actual.length ? actual[i] : undefined;

    if (exp === act) {
      diff.push({ type: " ", line: exp! });
    } else {
      if (exp !== undefined) {
        diff.push({ type: "-", line: exp });
      }
      if (act !== undefined) {
        diff.push({ type: "+", line: act });
      }
    }
  }

  return diff;
}

// ============================================================================
// Main Entry Point
// ============================================================================

async function main(): Promise<void> {
  const args = parseArgs(process.argv);

  if (!args.test) {
    console.error("Error: Test name is required");
    printHelp();
    process.exit(1);
  }

  const startTime = Date.now();

  // Find build paths
  let buildPaths: BuildPaths;
  try {
    buildPaths = findBuildPaths(args.test, args.arch);
  } catch (err) {
    if (args.json) {
      const result: MaverickTestResult = {
        version: 1,
        test: args.test,
        arch: args.arch,
        verdict: "FAIL",
        executionTimeMs: Date.now() - startTime,
        output: [],
        coreOutput: [],
        errors: [(err as Error).message],
      };
      console.log(JSON.stringify(result, null, 2));
    } else {
      console.error(`Error: ${(err as Error).message}`);
    }
    process.exit(1);
  }

  // Run the test
  let output: string[] = [];
  let exitCode = 1;

  try {
    const result = await runTest(args.test, args.arch, buildPaths, args.timeout);
    output = result.output;
    exitCode = result.exitCode;
  } catch (err) {
    if (args.json) {
      const result: MaverickTestResult = {
        version: 1,
        test: args.test,
        arch: args.arch,
        verdict: "FAIL",
        executionTimeMs: Date.now() - startTime,
        output: [],
        coreOutput: [],
        errors: [`Failed to run test: ${(err as Error).message}`],
      };
      console.log(JSON.stringify(result, null, 2));
    } else {
      console.error(`Error running test: ${(err as Error).message}`);
    }
    process.exit(1);
  }

  // Check results
  const coreOutput = extractCoreOutput(output);
  const panic = checkForPanic(output);
  const checkResult = checkOutput(output, args.test, buildPaths);

  const executionTimeMs = Date.now() - startTime;

  if (args.json) {
    const result: MaverickTestResult = {
      version: 1,
      test: args.test,
      arch: args.arch,
      verdict: checkResult.verdict,
      executionTimeMs,
      output,
      coreOutput,
      errors: checkResult.errors,
      diff: checkResult.diff,
      panic,
    };
    console.log(JSON.stringify(result, null, 2));
  } else {
    // Text output
    if (checkResult.verdict === "PASS") {
      console.log(`\x1b[32m✓ PASS\x1b[0m ${args.test} (${executionTimeMs}ms)`);
    } else {
      console.log(`\x1b[31m✗ FAIL\x1b[0m ${args.test} (${executionTimeMs}ms)`);
      for (const err of checkResult.errors) {
        console.log(`  ${err}`);
      }
      if (panic) {
        console.log(`  Panic: ${panic.message}`);
      }
    }
  }

  process.exit(checkResult.verdict === "PASS" ? 0 : 1);
}

main().catch((err) => {
  console.error(`Fatal error: ${err.message}`);
  process.exit(1);
});
