/**
 * Type definitions for the PintOS test verification framework
 */

export interface TestResult {
  verdict: "PASS" | "FAIL";
  messages: string[];
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
 * Parsed .ck file structure
 */
export interface ParsedChecker {
  type: "expected" | "alarm" | "mlfqs" | "archive" | "custom";
  options: CheckOptions;
  expected?: string[]; // For expected output tests
  iterations?: number; // For alarm tests
  // Add more fields as needed for other test types
}

/**
 * File system entry for archive tests
 */
export type FSEntry =
  | { type: "file"; content: string | [string, number, number] }
  | { type: "directory" };

export type FSHierarchy = Map<string, FSEntry>;
