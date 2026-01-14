#!/usr/bin/env bun
/**
 * Generate .test.json files from .ck (Perl checker) files
 *
 * This script parses all .ck files in the tests directory and generates
 * corresponding .test.json files with a clean, native format.
 *
 * Usage:
 *   bun run src/generate-test-specs.ts [tests-dir]
 *
 * The generated JSON files can be used by check-test instead of parsing
 * the Perl .ck files at runtime.
 */

import { readdirSync, statSync, readFileSync, writeFileSync, existsSync } from "fs";
import { join, dirname } from "path";
import { parseCkFile, type ParsedChecker } from "./tests/ck-parser";

interface TestSpec {
  // Metadata
  version: 1;
  source: string; // Original .ck file path

  // Test type
  type:
    | "expected"
    | "alarm"
    | "mlfqs_fair"
    | "mlfqs_load_avg"
    | "mlfqs_recent"
    | "mlfqs_nice"
    | "mlfqs_block"
    | "archive";

  // Options
  options: {
    ignore_exit_codes?: boolean;
    ignore_user_faults?: boolean;
  };

  // Type-specific data
  expected?: string[]; // For expected output tests (first/primary variant)
  expected_variants?: string[][]; // Multiple acceptable outputs
  iterations?: number; // For alarm tests
  nice?: number[]; // For MLFQS fair/nice tests
  maxdiff?: number; // For MLFQS tests
  ticks?: number; // For MLFQS recent tests
  archive_tree?: any; // For archive tests
}

function findCkFiles(dir: string): string[] {
  const results: string[] = [];

  function walk(currentDir: string) {
    const entries = readdirSync(currentDir);
    for (const entry of entries) {
      const fullPath = join(currentDir, entry);
      const stat = statSync(fullPath);
      if (stat.isDirectory()) {
        walk(fullPath);
      } else if (entry.endsWith(".ck")) {
        results.push(fullPath);
      }
    }
  }

  walk(dir);
  return results;
}

function convertToSpec(ckPath: string, parsed: ParsedChecker): TestSpec | null {
  // Skip custom type - these need manual handling or Perl fallback
  if (parsed.type === "custom") {
    return null;
  }

  const spec: TestSpec = {
    version: 1,
    source: ckPath,
    type: parsed.type,
    options: {},
  };

  // Copy options
  if (parsed.options.IGNORE_EXIT_CODES) {
    spec.options.ignore_exit_codes = true;
  }
  if (parsed.options.IGNORE_USER_FAULTS) {
    spec.options.ignore_user_faults = true;
  }

  // Copy type-specific data
  switch (parsed.type) {
    case "expected":
      if (parsed.expected && parsed.expected.length > 0) {
        // Primary expected output (first variant)
        spec.expected = parsed.expected[0];
        // If multiple variants, include all
        if (parsed.expected.length > 1) {
          spec.expected_variants = parsed.expected;
        }
      }
      break;

    case "alarm":
      spec.iterations = parsed.iterations;
      break;

    case "mlfqs_fair":
    case "mlfqs_nice":
      spec.nice = parsed.nice;
      spec.maxdiff = parsed.maxdiff;
      break;

    case "mlfqs_recent":
      spec.ticks = parsed.ticks;
      spec.maxdiff = parsed.maxdiff;
      break;

    case "mlfqs_load_avg":
    case "mlfqs_block":
      // These don't have additional parameters
      break;

    case "archive":
      spec.archive_tree = parsed.archiveTree;
      break;
  }

  return spec;
}

function main() {
  const testsDir = process.argv[2] || join(dirname(dirname(dirname(__dirname))), "tests");

  if (!existsSync(testsDir)) {
    console.error(`Tests directory not found: ${testsDir}`);
    process.exit(1);
  }

  console.log(`Scanning for .ck files in: ${testsDir}`);
  const ckFiles = findCkFiles(testsDir);
  console.log(`Found ${ckFiles.length} .ck files`);

  let converted = 0;
  let skipped = 0;
  let failed = 0;

  for (const ckPath of ckFiles) {
    try {
      const parsed = parseCkFile(ckPath);
      const spec = convertToSpec(ckPath, parsed);

      if (spec === null) {
        // Custom type - skip
        skipped++;
        console.log(`  SKIP (custom): ${ckPath}`);
        continue;
      }

      // Write .test.json alongside .ck file
      const jsonPath = ckPath.replace(/\.ck$/, ".test.json");
      writeFileSync(jsonPath, JSON.stringify(spec, null, 2) + "\n");
      converted++;
      console.log(`  OK: ${jsonPath}`);
    } catch (err) {
      failed++;
      console.error(`  FAIL: ${ckPath}: ${err}`);
    }
  }

  console.log(`\nSummary:`);
  console.log(`  Converted: ${converted}`);
  console.log(`  Skipped (custom): ${skipped}`);
  console.log(`  Failed: ${failed}`);
}

main();
