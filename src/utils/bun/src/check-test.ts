#!/usr/bin/env bun
/**
 * PintOS Test Checker - TypeScript port of Perl test verification
 *
 * Usage: check-test.ts <checker.ck> <test-name> <result-file>
 *
 * This replaces the `perl -I$(SRCDIR) $< $* $@` pattern in Make.tests
 *
 * It supports two formats:
 * 1. .test.json - Native JSON format (preferred, faster)
 * 2. .ck - Perl checker files (fallback, parsed at runtime)
 *
 * The .test.json files can be generated from .ck files using:
 *   bun run src/generate-test-specs.ts
 */

import { existsSync, readFileSync } from "fs";
import { dirname, join } from "path";
import { parseCkFile, canHandle, type ParsedChecker } from "./tests/ck-parser";
import type { CheckOptions } from "./tests/types";
import {
  initTest,
  checkExpected,
  checkPrerequisites,
  fail,
  pass,
  getPrereqTests,
} from "./tests/tests";
import { checkAlarm } from "./tests/alarm";
import {
  checkMlfqsFair,
  checkMlfqsLoadAvg,
  checkMlfqsRecentCpu,
  checkMlfqsNice,
  checkMlfqsBlock,
} from "./tests/mlfqs";
import { checkArchive } from "./tests/archive";

/**
 * Test spec from .test.json file
 */
interface TestSpec {
  version: 1;
  source: string;
  type: string;
  options: {
    ignore_exit_codes?: boolean;
    ignore_user_faults?: boolean;
  };
  expected?: string[];
  expected_variants?: string[][];
  iterations?: number;
  nice?: number[];
  maxdiff?: number;
  ticks?: number;
  archive_tree?: any;
}

/**
 * Load a .test.json file and convert to ParsedChecker format
 */
function loadTestSpec(jsonPath: string): ParsedChecker | null {
  try {
    const content = readFileSync(jsonPath, "utf-8");
    const spec: TestSpec = JSON.parse(content);

    if (spec.version !== 1) {
      console.error(`Unsupported spec version: ${spec.version}`);
      return null;
    }

    const options: CheckOptions = {};
    if (spec.options.ignore_exit_codes) {
      options.IGNORE_EXIT_CODES = true;
    }
    if (spec.options.ignore_user_faults) {
      options.IGNORE_USER_FAULTS = true;
    }

    const parsed: ParsedChecker = {
      type: spec.type as ParsedChecker["type"],
      options,
    };

    // Copy type-specific data
    if (spec.expected_variants) {
      parsed.expected = spec.expected_variants;
    } else if (spec.expected) {
      parsed.expected = [spec.expected];
    }

    if (spec.iterations !== undefined) {
      parsed.iterations = spec.iterations;
    }
    if (spec.nice !== undefined) {
      parsed.nice = spec.nice;
    }
    if (spec.maxdiff !== undefined) {
      parsed.maxdiff = spec.maxdiff;
    }
    if (spec.ticks !== undefined) {
      parsed.ticks = spec.ticks;
    }
    if (spec.archive_tree !== undefined) {
      parsed.archiveTree = spec.archive_tree;
    }

    return parsed;
  } catch (err) {
    // Failed to load JSON, fall back to .ck parsing
    return null;
  }
}

function main() {
  const args = process.argv.slice(2);

  if (args.length < 2) {
    console.error("Usage: check-test.ts <checker.ck> <test-name> <result-file>");
    process.exit(1);
  }

  let ckFile: string;
  let testName: string;
  let resultFile: string;

  // Determine argument format (matches Perl invocation pattern)
  if (args.length === 2) {
    // Called as: check-test.ts test-name result-file
    testName = args[0];
    resultFile = args[1];
    ckFile = `${testName}.ck`;
  } else {
    // Called as: check-test.ts checker.ck test-name result-file
    ckFile = args[0];
    testName = args[1];
    resultFile = args[2];
  }

  // Find source directory (for module paths)
  let srcDir = process.cwd();
  while (srcDir !== "/" && !existsSync(join(srcDir, "tests", "tests.pm"))) {
    srcDir = dirname(srcDir);
  }

  // Initialize test context
  initTest(testName, srcDir);

  // Check prerequisites for -persistence tests
  checkPrerequisites();

  // Try to load .test.json first (faster, no Perl parsing)
  const jsonFile = ckFile.replace(/\.ck$/, ".test.json");
  let parsed: ParsedChecker | null = null;

  if (existsSync(jsonFile)) {
    parsed = loadTestSpec(jsonFile);
  }

  // Fall back to parsing .ck file
  if (parsed === null) {
    if (!existsSync(ckFile)) {
      fail(`Checker file not found: ${ckFile}`);
    }
    parsed = parseCkFile(ckFile);
  }

  // Handle based on test type
  switch (parsed.type) {
    case "expected":
      if (parsed.expected && parsed.expected.length > 0) {
        // Convert array of line arrays to array of strings
        const expectedVariants = parsed.expected.map((lines) => lines.join("\n"));
        checkExpected(expectedVariants, parsed.options);
      } else {
        fail("Could not parse expected output from .ck file");
      }
      break;

    case "alarm":
      if (parsed.iterations !== undefined) {
        checkAlarm(testName, parsed.iterations);
      } else {
        fail("Could not parse alarm iterations from .ck file");
      }
      break;

    case "mlfqs_fair":
      if (parsed.nice && parsed.maxdiff !== undefined) {
        checkMlfqsFair(testName, parsed.nice, parsed.maxdiff);
      } else {
        fail("Could not parse MLFQS fair parameters from .ck file");
      }
      break;

    case "mlfqs_nice":
      if (parsed.nice && parsed.maxdiff !== undefined) {
        checkMlfqsNice(testName, parsed.nice, parsed.maxdiff);
      } else {
        fail("Could not parse MLFQS nice parameters from .ck file");
      }
      break;

    case "mlfqs_load_avg":
      checkMlfqsLoadAvg(testName);
      break;

    case "mlfqs_recent":
      checkMlfqsRecentCpu(
        testName,
        parsed.ticks ?? 100,
        parsed.maxdiff ?? 6
      );
      break;

    case "mlfqs_block":
      checkMlfqsBlock(testName);
      break;

    case "archive":
      if (parsed.archiveTree) {
        const prereqTests = getPrereqTests();
        checkArchive(testName, parsed.archiveTree, prereqTests);
      } else {
        fail("Could not parse archive tree from .ck file");
      }
      break;

    case "custom":
      // Custom tests need Perl execution
      fallbackToPerl(ckFile, testName, resultFile, srcDir);
      break;

    default:
      fail(`Unknown test type: ${parsed.type}`);
  }
}

/**
 * Fall back to Perl for tests we can't handle yet
 */
function fallbackToPerl(
  ckFile: string,
  testName: string,
  resultFile: string,
  srcDir: string
): never {
  const { spawnSync } = require("child_process");

  const result = spawnSync(
    "perl",
    ["-I" + srcDir, ckFile, testName, resultFile],
    {
      stdio: "inherit",
      cwd: process.cwd(),
    }
  );

  process.exit(result.status ?? 1);
}

main();
