/**
 * Type definitions for the PintOS test verification framework
 */

export interface TestResult {
  verdict: "PASS" | "FAIL";
  messages: string[];
}

/**
 * Structured test result for agent consumption
 */
export interface StructuredTestResult {
  version: 1;
  test: string;
  verdict: "PASS" | "FAIL";
  errors: string[];
  output?: string[];
  coreOutput?: string[];
  diff?: DiffEntry[];
  panic?: {
    message: string;
    callStack?: string;
    backtrace?: string;
  };
}

/**
 * Diff entry for structured output
 */
export interface DiffEntry {
  type: "-" | "+" | " ";
  line: string;
}

export interface CheckOptions {
  IGNORE_EXIT_CODES?: boolean;
  IGNORE_USER_FAULTS?: boolean;
}

export interface DiffHunk {
  type: "same" | "add" | "remove";
  lines: string[];
}

/**
 * Check rule for custom test logic
 */
export interface CheckRule {
  type: "contains" | "not_contains" | "regex" | "not_regex" | "equals";
  pattern: string;
  message?: string; // Custom failure message
}

/**
 * Parsed .ck file structure
 */
export interface ParsedChecker {
  type:
    | "expected"
    | "alarm"
    | "mlfqs"
    | "archive"
    | "process_death"
    | "halt"
    | "contains_pass"
    | "flexible_order"
    | "multi_check"
    | "custom";
  options: CheckOptions;
  expected?: string[]; // For expected output tests
  iterations?: number; // For alarm tests
  processName?: string; // For process_death tests
  checks?: CheckRule[]; // For multi_check tests
  threadCount?: number; // For flexible_order tests
  iterCount?: number; // For flexible_order tests
  // Add more fields as needed for other test types
}

/**
 * File system entry for archive tests
 */
export type FSEntry =
  | { type: "file"; content: string | [string, number, number] }
  | { type: "directory" };

export type FSHierarchy = Map<string, FSEntry>;
