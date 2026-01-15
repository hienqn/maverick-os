/**
 * Core test verification framework for PintOS
 * TypeScript port of tests/tests.pm
 */

import { readFileSync, writeFileSync, existsSync } from "fs";
import { execSync } from "child_process";
import { diff, formatDiff, arraysEqual, type DiffEntry } from "./diff";
import type { TestResult, CheckOptions, CheckRule, StructuredTestResult } from "./types";

// ANSI color codes for terminal output
const isTerminal = process.stdout.isTTY;
const colors = {
  green: isTerminal ? "\x1b[32m" : "",
  red: isTerminal ? "\x1b[31m" : "",
  yellow: isTerminal ? "\x1b[33m" : "",
  dim: isTerminal ? "\x1b[2m" : "",
  bold: isTerminal ? "\x1b[1m" : "",
  reset: isTerminal ? "\x1b[0m" : "",
};

// Global state (set by main entry point)
let testName: string;
let srcDir: string;
let messages: string[] = [];
let prereqTests: string[] = [];

// Structured mode state
let structuredMode = false;
let structuredResult: StructuredTestResult | null = null;
let capturedOutput: string[] = [];
let capturedCoreOutput: string[] = [];
let capturedDiff: DiffEntry[] = [];
let capturedPanic: StructuredTestResult["panic"] = undefined;

/**
 * Custom error for structured mode - thrown instead of process.exit()
 */
export class TestResultError extends Error {
  constructor(public result: StructuredTestResult) {
    super(`Test ${result.verdict}: ${result.errors.join(", ")}`);
    this.name = "TestResultError";
  }
}

/**
 * Enable structured mode - throws instead of exiting, captures rich data
 */
export function enableStructuredMode(): void {
  structuredMode = true;
  structuredResult = null;
  capturedOutput = [];
  capturedCoreOutput = [];
  capturedDiff = [];
  capturedPanic = undefined;
}

/**
 * Disable structured mode - returns to normal exit behavior
 */
export function disableStructuredMode(): void {
  structuredMode = false;
}

/**
 * Capture output for structured results
 */
export function setCapturedOutput(output: string[], coreOutput?: string[]): void {
  if (structuredMode) {
    capturedOutput = output;
    if (coreOutput) {
      capturedCoreOutput = coreOutput;
    }
  }
}

/**
 * Capture diff for structured results
 */
export function setCapturedDiff(diffEntries: DiffEntry[]): void {
  if (structuredMode) {
    capturedDiff = diffEntries;
  }
}

/**
 * Initialize the test context
 */
export function initTest(test: string, src: string): void {
  testName = test;
  srcDir = src;
  messages = [];
}

/**
 * Read a text file and return lines (without newlines)
 */
export function readTextFile(filename: string): string[] {
  try {
    const content = readFileSync(filename, "utf-8");
    return content.split("\n").map((line) => line.replace(/\r$/, ""));
  } catch (e) {
    throw new Error(`${filename}: open: ${(e as Error).message}`);
  }
}

/**
 * Record a failure and exit
 */
export function fail(...msgs: string[]): never {
  finish("FAIL", ...msgs);
}

/**
 * Record success and exit
 */
export function pass(...msgs: string[]): never {
  finish("PASS", ...msgs);
}

/**
 * Write result file and exit (or throw in structured mode)
 */
function finish(verdict: "PASS" | "FAIL", ...msgs: string[]): never {
  const allMessages = [...messages, ...msgs];

  if (structuredMode) {
    // Build structured result and throw
    const result: StructuredTestResult = {
      version: 1,
      test: testName,
      verdict,
      errors: allMessages,
      output: capturedOutput.length > 0 ? capturedOutput : undefined,
      coreOutput: capturedCoreOutput.length > 0 ? capturedCoreOutput : undefined,
      diff: capturedDiff.length > 0 ? capturedDiff : undefined,
      panic: capturedPanic,
    };
    throw new TestResultError(result);
  }

  // Normal mode - write result file and exit
  const resultFile = `${testName}.result`;

  // Write verdict first, then each message on its own line
  // Must match Perl's format exactly: "PASS\n" or "FAIL\nmessage1\nmessage2\n"
  let content = verdict + "\n";
  if (allMessages.length > 0) {
    content += allMessages.join("\n") + "\n";
  }
  writeFileSync(resultFile, content);

  if (verdict === "PASS") {
    console.log(`${colors.green}✓ pass${colors.reset} ${colors.dim}${testName}${colors.reset}`);
  } else {
    console.log(`${colors.red}${colors.bold}✗ FAIL${colors.reset} ${testName}`);
    allMessages.forEach((m) => console.log(`  ${colors.dim}${m}${colors.reset}`));
  }

  process.exit(0);
}

/**
 * Add a message to the output
 */
export function log(msg: string): void {
  messages.push(msg);
}

/**
 * Check for prerequisite tests (for -persistence tests)
 */
export function checkPrerequisites(): void {
  prereqTests = [];
  const match = testName.match(/^(.*)-persistence$/);
  if (match) {
    const prereqTest = match[1];
    prereqTests.push(prereqTest);
    try {
      const result = readTextFile(`${prereqTest}.result`);
      if (result[0] !== "PASS") {
        fail(`Prerequisite test ${prereqTest} failed.`);
      }
    } catch {
      fail(`Prerequisite test ${prereqTest} result not found.`);
    }
  }
}

/**
 * Get the list of prerequisite tests
 */
export function getPrereqTests(): string[] {
  return prereqTests;
}

/**
 * Main verification function - check output against expected
 */
export function checkExpected(
  expected: string | string[],
  options: CheckOptions = {}
): void {
  const output = readTextFile(`${testName}.output`);
  commonChecks("run", output);
  compareOutput("run", output, expected, options);
}

/**
 * Common checks for all test output
 */
export function commonChecks(run: string, output: string[]): void {
  if (output.length === 0) {
    fail(`${capitalize(run)} produced no output at all`);
  }

  checkForPanic(run, output);
  checkForKeyword(run, "FAIL", output);
  checkForTripleFault(run, output);
  checkForKeyword(run, "TIMEOUT", output);

  if (!output.some((line) => /Pintos booting with.*kB RAM\.\.\./.test(line))) {
    fail(
      `${capitalize(run)} didn't start up properly: no "Pintos booting" message`
    );
  }
  if (!output.some((line) => /Boot complete/.test(line))) {
    fail(
      `${capitalize(run)} didn't start up properly: no "Boot complete" message`
    );
  }
  if (!output.some((line) => /Timer: \d+ ticks/.test(line))) {
    fail(
      `${capitalize(run)} didn't shut down properly: no "Timer: # ticks" message`
    );
  }
  if (!output.some((line) => /Powering off/.test(line))) {
    fail(
      `${capitalize(run)} didn't shut down properly: no "Powering off" message`
    );
  }
}

/**
 * Check for kernel panic
 */
function checkForPanic(run: string, output: string[]): void {
  const panicLine = output.find((line) => /PANIC/.test(line));
  if (!panicLine) return;

  const panicStart = panicLine.indexOf("PANIC");
  const panicMessage = panicLine.substring(panicStart);
  log(`Kernel panic in ${run}: ${panicMessage}`);

  let callStack: string | undefined;
  let backtraceResult: string | undefined;

  // Try to get and translate backtrace
  const stackLine = output.find((line) => /Call stack:/.test(line));
  if (stackLine) {
    const match = stackLine.match(/Call stack:((?:\s+0x[0-9a-f]+)+)/);
    if (match) {
      const addrs = match[1];
      callStack = addrs.trim();
      log(`Call stack:${addrs}`);

      // Try to translate the backtrace
      try {
        const trace = execSync(`backtrace kernel.o ${addrs}`, {
          encoding: "utf-8",
        });
        backtraceResult = trace.trim();
        log("Translation of call stack:");
        log(trace);
      } catch {
        // Backtrace translation failed, continue without it
      }
    }
  }

  // Capture panic for structured mode
  if (structuredMode) {
    capturedPanic = {
      message: panicMessage,
      callStack,
      backtrace: backtraceResult,
    };
  }

  // Add helpful hints for common panics
  if (/sec_no \< d->capacity/.test(panicLine)) {
    log(
      "\nThis assertion commonly fails when accessing a file via an inode that"
    );
    log("has been closed and freed. Freeing an inode clears all its sector");
    log("indexes to 0xcccccccc, which is not a valid sector number for disks");
    log("smaller than about 1.6 TB.");
  }

  fail();
}

/**
 * Check for specific keyword in output
 */
function checkForKeyword(run: string, keyword: string, output: string[]): void {
  const kwLine = output.find((line) => line.includes(keyword));
  if (!kwLine) return;

  // Strip test name prefix for brevity
  const cleanLine = kwLine.replace(/^\([^)]+\)\s+/, "");
  log(`${run}: ${cleanLine}`);
  fail();
}

/**
 * Check for triple fault (spontaneous reboots)
 */
function checkForTripleFault(run: string, output: string[]): void {
  const reboots = output.filter((line) => /Pintos booting/.test(line)).length - 1;
  if (reboots <= 0) return;

  log(`${capitalize(run)} spontaneously rebooted ${reboots} times.`);
  log("This is most often caused by unhandled page faults.");
  log("Read the Triple Faults section in the Debugging chapter");
  log("of the Pintos manual for more information.");
  fail();
}

/**
 * Extract core output between "Executing" and "Execution complete"
 */
export function getCoreOutput(run: string, output: string[]): string[] {
  let start = -1;
  let end = -1;
  let process = "";

  for (let i = 0; i < output.length; i++) {
    const match = output[i].match(/^Executing '(\S+).*':$/);
    if (match) {
      start = i + 1;
      process = match[1];
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

  if (start < 0) {
    fail(`${capitalize(run)} didn't start a thread or process`);
  }
  if (end < 0) {
    fail(`${capitalize(run)} started '${process}' but it never finished`);
  }

  return output.slice(start, end + 1);
}

/**
 * Compare actual output against expected
 */
function compareOutput(
  run: string,
  fullOutput: string[],
  expected: string | string[],
  options: CheckOptions
): void {
  let output = getCoreOutput(run, fullOutput);

  // Capture output for structured mode
  if (structuredMode) {
    capturedOutput = fullOutput;
    capturedCoreOutput = output;
  }

  if (output.length === 0) {
    fail(`${capitalize(run)} didn't produce any output`);
  }

  // Apply filters based on options
  if (options.IGNORE_EXIT_CODES) {
    output = output.filter((line) => !/^[a-zA-Z0-9-_]+: exit\(-?\d+\)$/.test(line));
  }

  if (options.IGNORE_USER_FAULTS) {
    output = output.filter(
      (line) =>
        !/^Page fault at.*in user context\.$/.test(line) &&
        !/: dying due to interrupt 0x0e \(.*\)\.$/.test(line) &&
        !/^Interrupt 0x0e \(.*\) at eip=/.test(line) &&
        !/ cr2=.* error=/.test(line) &&
        !/ eax=.* ebx=.* ecx=.* edx=/.test(line) &&
        !/ esi=.* edi=.* esp=.* ebp=/.test(line) &&
        !/ cs=.* ds=.* es=.* ss=/.test(line)
    );
  }

  // Handle multiple acceptable outputs
  const expectedVariants = Array.isArray(expected) ? expected : [expected];
  let msg = "";
  let lastDiffEntries: DiffEntry[] = [];

  for (const exp of expectedVariants) {
    const expectedLines = exp.split("\n").filter((line) => line !== "");

    msg += "Acceptable output:\n";
    msg += expectedLines.map((line) => `  ${line}`).join("\n") + "\n";

    // Check for exact match
    if (arraysEqual(output, expectedLines)) {
      pass(); // Success!
    }

    // Generate diff
    const diffEntries = diff(expectedLines, output);
    lastDiffEntries = diffEntries;
    msg += "Differences in `diff -u' format:\n";
    msg += formatDiff(diffEntries) + "\n";
  }

  // Capture diff for structured mode (use last variant's diff)
  if (structuredMode) {
    capturedDiff = lastDiffEntries;
  }

  // No match found
  if (options.IGNORE_EXIT_CODES) {
    msg += "\n(Process exit codes are excluded for matching purposes.)\n";
  }
  if (options.IGNORE_USER_FAULTS) {
    msg += "\n(User fault messages are excluded for matching purposes.)\n";
  }

  fail(`Test output failed to match any acceptable form.\n\n${msg}`);
}

function capitalize(s: string): string {
  return s.charAt(0).toUpperCase() + s.slice(1);
}

/**
 * Check for process death - verifies process started, exited with -1, and didn't complete
 */
export function checkProcessDeath(processName: string): void {
  const output = readTextFile(`${testName}.output`);
  commonChecks("run", output);
  const coreOutput = getCoreOutput("run", output);

  // Check for begin message
  if (coreOutput[0] !== `(${processName}) begin`) {
    fail(`First line of output is not '(${processName}) begin' message.`);
  }

  // Check for exit(-1)
  if (!coreOutput.some((line) => line === `${processName}: exit(-1)`)) {
    fail(`Output missing '${processName}: exit(-1)' message.`);
  }

  // Check that end message is NOT present
  if (coreOutput.some((line) => line.includes(`(${processName}) end`))) {
    fail(`Output contains '(${processName}) end' message.`);
  }

  pass();
}

/**
 * Check for halt test - verifies halt stopped before completion
 */
export function checkHalt(): void {
  const output = readTextFile(`${testName}.output`);
  commonChecks("run", output);

  // Check for begin message
  if (!output.some((line) => line === "(halt) begin")) {
    fail("missing 'begin' message");
  }

  // Check that complete message is NOT present
  if (output.some((line) => line === "Execution of 'halt' complete.")) {
    fail("found 'complete' message--halt didn't really halt");
  }

  // Check that fail message is NOT present
  if (output.some((line) => line === "(halt) fail")) {
    fail("found 'fail' message--halt didn't really halt");
  }

  pass();
}

/**
 * Check for PASS in output (for simple self-validating tests)
 */
export function checkContainsPass(processName: string): void {
  const output = readTextFile(`${testName}.output`);
  commonChecks("run", output);
  const coreOutput = getCoreOutput("run", output);

  if (!coreOutput.some((line) => line === `(${processName}) PASS`)) {
    fail(`missing PASS in output`);
  }

  pass();
}

/**
 * Check for flexible ordering (priority-fifo style)
 * All iterations must have same order, but any permutation of 0..threadCount-1 is valid
 */
export function checkFlexibleOrder(
  processName: string,
  threadCount: number,
  iterCount: number
): void {
  const output = readTextFile(`${testName}.output`);
  commonChecks("run", output);
  const coreOutput = getCoreOutput("run", output);

  const iterations = coreOutput.filter((line) => line.includes("iteration:"));

  if (iterations.length === 0) {
    fail("No iterations found in output.");
  }

  // Extract numbers from first iteration
  const firstMatch = iterations[0].match(/(\d+)/g);
  if (!firstMatch || firstMatch.length !== threadCount) {
    fail(`First iteration does not list exactly ${threadCount} threads.`);
  }

  const numbering = firstMatch.map(Number);

  // Verify it's a permutation of 0..threadCount-1
  const sorted = [...numbering].sort((a, b) => a - b);
  for (let i = 0; i < sorted.length; i++) {
    if (sorted[i] !== i) {
      fail(`First iteration does not list all threads 0...${threadCount - 1}`);
    }
  }

  // Verify all iterations are identical
  for (let i = 1; i < iterations.length; i++) {
    if (iterations[i] !== iterations[0]) {
      fail(`Iteration ${i} differs from iteration 0`);
    }
  }

  // Verify iteration count
  if (iterations.length !== iterCount) {
    fail(`${iterCount} iterations expected but ${iterations.length} found`);
  }

  pass();
}

/**
 * Multi-check: run multiple check rules against output
 */
export function checkMultiRule(checks: CheckRule[]): void {
  const output = readTextFile(`${testName}.output`);
  commonChecks("run", output);
  const coreOutput = getCoreOutput("run", output);
  const allOutput = output.join("\n");
  const coreStr = coreOutput.join("\n");

  for (const check of checks) {
    const target = coreStr;

    switch (check.type) {
      case "contains":
        if (!target.includes(check.pattern)) {
          fail(check.message || `Output does not contain: ${check.pattern}`);
        }
        break;

      case "not_contains":
        if (target.includes(check.pattern)) {
          fail(check.message || `Output should not contain: ${check.pattern}`);
        }
        break;

      case "regex":
        if (!new RegExp(check.pattern).test(target)) {
          fail(check.message || `Output does not match pattern: ${check.pattern}`);
        }
        break;

      case "not_regex":
        if (new RegExp(check.pattern).test(target)) {
          fail(check.message || `Output should not match pattern: ${check.pattern}`);
        }
        break;

      case "equals":
        if (!coreOutput.some((line) => line === check.pattern)) {
          fail(check.message || `Output does not have exact line: ${check.pattern}`);
        }
        break;
    }
  }

  pass();
}
