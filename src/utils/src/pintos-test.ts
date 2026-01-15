#!/usr/bin/env bun
/**
 * Interactive test runner for PintOS
 * TypeScript port of pintos-test bash script
 *
 * Usage: pintos-test [test-name]
 *
 * Features:
 * - Fuzzy search for tests with fzf
 * - Colored output
 * - Debug mode support (PINTOS_DEBUG=1)
 */

import { existsSync, unlinkSync } from "fs";
import { spawn, spawnSync } from "child_process";
import { basename } from "path";

// Colors for terminal output
const isTerminal = process.stdout.isTTY;
const colors = {
  red: isTerminal ? "\x1b[31m" : "",
  green: isTerminal ? "\x1b[32m" : "",
  yellow: isTerminal ? "\x1b[33m" : "",
  dim: isTerminal ? "\x1b[2m" : "",
  bold: isTerminal ? "\x1b[1m" : "",
  reset: isTerminal ? "\x1b[0m" : "",
};

function fatal(message: string): never {
  console.error(`${colors.red}Error: ${message}${colors.reset}`);
  process.exit(1);
}

function info(message: string): void {
  console.log(`${colors.dim}${message}${colors.reset}`);
}

async function main() {
  // Check we're in the right directory
  const cwd = process.cwd();
  const currentDir = basename(cwd);

  let buildDir = cwd;
  if (currentDir !== "build") {
    if (existsSync("build")) {
      process.chdir("build");
      buildDir = process.cwd();
    } else {
      fatal(`directory 'build' was not found.
Make sure you have run 'make',
and that you are in one of: userprog, threads, filesys, vm`);
    }
  }

  // Get test name from args
  const args = process.argv.slice(2);
  if (args.length > 1) {
    fatal("too many arguments. Please provide at most one test name.");
  }

  const testQuery = args[0] || "";

  // Get list of all tests from Makefile
  info("Fetching test list...");
  const makeResult = spawnSync(
    "make",
    ["-f", "/dev/stdin", "-f", "Makefile", "print-TESTS"],
    {
      input: "print-% : ; $(info $($*)) @true",
      encoding: "utf-8",
    }
  );

  if (makeResult.status !== 0) {
    fatal("Failed to get test list from Makefile");
  }

  const tests = makeResult.stdout
    .trim()
    .split(/\s+/)
    .filter((t) => t.length > 0);

  if (tests.length === 0) {
    fatal("No tests found in Makefile");
  }

  // Use fzf for selection
  let selectedTest: string;

  // Check for exact or unique prefix match first
  const exactMatch = tests.find((t) => t === testQuery || t.endsWith(`/${testQuery}`));
  const prefixMatches = tests.filter(
    (t) => t.includes(testQuery)
  );

  if (exactMatch) {
    selectedTest = exactMatch;
  } else if (prefixMatches.length === 1) {
    selectedTest = prefixMatches[0];
  } else {
    // Use fzf for fuzzy selection
    const fzf = spawnSync("fzf", [
      "-q", testQuery,
      "--select-1",
      "--exit-0",
      "--header=Select a test to run",
      "--height=50%",
    ], {
      input: tests.join("\n"),
      encoding: "utf-8",
      stdio: ["pipe", "pipe", "inherit"],
    });

    if (fzf.status !== 0 || !fzf.stdout.trim()) {
      fatal("No test selected, or no matching test exists.");
    }

    selectedTest = fzf.stdout.trim();
  }

  console.log(`${colors.green}Running:${colors.reset} ${selectedTest}`);

  // Remove old output/result files
  for (const ext of ["output", "result"]) {
    const file = `${selectedTest}.${ext}`;
    if (existsSync(file)) {
      try {
        unlinkSync(file);
      } catch (e) {
        fatal(`Could not delete '${file}'`);
      }
    }
  }

  const debug = process.env.PINTOS_DEBUG;

  if (debug) {
    // Debug mode
    if (debug !== "2") {
      // Run test in background, start GDB in foreground
      info("Starting test with debugger...");
      const makeProc = spawn("make", [`${selectedTest}.output`], {
        stdio: ["ignore", "pipe", "pipe"],
      });

      let output = "";
      makeProc.stdout.on("data", (data) => (output += data));
      makeProc.stderr.on("data", (data) => (output += data));

      // Start GDB
      spawnSync("pintos-gdb", ["kernel.o"], { stdio: "inherit" });

      // Wait for make to finish and show output
      await new Promise<void>((resolve) => {
        makeProc.on("close", () => {
          console.log(output);
          resolve();
        });
      });
    } else {
      // Debug mode 2: just run test, let user connect GDB separately
      info("Starting test (waiting for debugger connection)...");
      info("Run 'pintos-gdb kernel.o' in another terminal");

      const makeProc = spawn("make", [`${selectedTest}.output`], {
        stdio: "inherit",
      });

      // Handle Ctrl+C
      process.on("SIGINT", () => {
        spawnSync("pkill", ["pintos"]);
        process.exit(1);
      });

      await new Promise<void>((resolve) => {
        makeProc.on("close", resolve);
      });
    }
  } else {
    // Normal mode: run test and show result
    const result = spawnSync("make", [`${selectedTest}.result`], {
      stdio: "inherit",
    });
    process.exit(result.status ?? 1);
  }
}

main().catch((e) => {
  fatal(e.message);
});
