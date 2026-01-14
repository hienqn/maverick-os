/**
 * Parser for PintOS .ck (checker) files
 *
 * .ck files are Perl scripts, but most follow predictable patterns:
 * 1. Simple expected output: check_expected([<<'EOF']); ... EOF
 * 2. Alarm tests: check_alarm(N);
 * 3. MLFQS tests: check_mlfqs_fair([...], N) or check_mlfqs_load_avg, etc.
 * 4. Archive tests: check_archive({...});
 *
 * This parser extracts the relevant information without executing Perl.
 * It also handles Perl variable interpolation in heredocs.
 */

import { readFileSync } from "fs";
import type { CheckOptions } from "./types";

/**
 * Parsed Perl variables from the .ck file
 */
interface PerlVariables {
  scalars: Map<string, string>; // $var = value
  arrays: Map<string, string[]>; // @arr = (v1, v2, ...)
}

export interface ParsedChecker {
  type:
    | "expected"
    | "alarm"
    | "mlfqs_fair"
    | "mlfqs_load_avg"
    | "mlfqs_recent"
    | "mlfqs_nice"
    | "mlfqs_block"
    | "archive"
    | "custom";
  options: CheckOptions;
  expected?: string[][]; // For expected output tests (multiple acceptable outputs)
  iterations?: number; // For alarm tests
  nice?: number[]; // For MLFQS fair/nice tests
  maxdiff?: number; // For MLFQS tests
  ticks?: number; // For MLFQS recent tests
  archiveTree?: any; // For archive tests (parsed Perl data structure)
}

/**
 * Parse Perl variable assignments from .ck file content
 * Handles: $var = value; and @arr = (v1, v2, ...);
 */
function parsePerlVariables(content: string): PerlVariables {
  const scalars = new Map<string, string>();
  const arrays = new Map<string, string[]>();

  // Parse scalar assignments: $var = value;
  // Handles: $init = 3115322833;
  const scalarRegex = /\$(\w+)\s*=\s*([^;]+);/g;
  let match;
  while ((match = scalarRegex.exec(content)) !== null) {
    const varName = match[1];
    const value = match[2].trim();
    // Only store simple numeric or string values
    if (/^[\d.]+$/.test(value) || /^['"].*['"]$/.test(value)) {
      scalars.set(varName, value.replace(/^['"]|['"]$/g, ""));
    }
  }

  // Parse array assignments: @arr = (v1, v2, ...);
  // Handles: @shuffle = (1691062564, 1973575879, ...);
  const arrayRegex = /@(\w+)\s*=\s*\(([^)]+)\)/g;
  while ((match = arrayRegex.exec(content)) !== null) {
    const arrName = match[1];
    const values = match[2]
      .split(",")
      .map((v) => v.trim().replace(/^['"]|['"]$/g, ""));
    arrays.set(arrName, values);
  }

  return { scalars, arrays };
}

/**
 * Interpolate Perl variables in a string
 * Handles: $var, $arr[0], $arr[1], etc.
 */
function interpolateVariables(text: string, vars: PerlVariables): string {
  let result = text;

  // Replace array access: $arr[index]
  result = result.replace(/\$(\w+)\[(\d+)\]/g, (match, arrName, index) => {
    const arr = vars.arrays.get(arrName);
    if (arr && parseInt(index, 10) < arr.length) {
      return arr[parseInt(index, 10)];
    }
    return match; // Keep original if not found
  });

  // Replace scalar variables: $var
  result = result.replace(/\$(\w+)(?!\[)/g, (match, varName) => {
    const value = vars.scalars.get(varName);
    return value !== undefined ? value : match;
  });

  return result;
}

/**
 * Parse a .ck file and extract test configuration
 */
export function parseCkFile(ckPath: string): ParsedChecker {
  const content = readFileSync(ckPath, "utf-8");

  // Check for alarm test
  const alarmMatch = content.match(/check_alarm\s*\(\s*(\d+)\s*\)/);
  if (alarmMatch) {
    return {
      type: "alarm",
      options: {},
      iterations: parseInt(alarmMatch[1], 10),
    };
  }

  // Check for MLFQS fair test: check_mlfqs_fair ([0, 0], 50)
  const mlfqsFairMatch = content.match(
    /check_mlfqs_fair\s*\(\s*\[([^\]]+)\]\s*,\s*(\d+)\s*\)/
  );
  if (mlfqsFairMatch) {
    const nice = mlfqsFairMatch[1].split(",").map((s) => parseInt(s.trim(), 10));
    const maxdiff = parseInt(mlfqsFairMatch[2], 10);
    return { type: "mlfqs_fair", options: {}, nice, maxdiff };
  }

  // Check for MLFQS nice test (same format as fair)
  const mlfqsNiceMatch = content.match(
    /check_mlfqs_nice\s*\(\s*\[([^\]]+)\]\s*,\s*(\d+)\s*\)/
  );
  if (mlfqsNiceMatch) {
    const nice = mlfqsNiceMatch[1].split(",").map((s) => parseInt(s.trim(), 10));
    const maxdiff = parseInt(mlfqsNiceMatch[2], 10);
    return { type: "mlfqs_nice", options: {}, nice, maxdiff };
  }

  // Check for MLFQS load average test (custom inline logic)
  if (content.includes("mlfqs_compare") && content.includes("load average")) {
    return { type: "mlfqs_load_avg", options: {} };
  }

  // Check for MLFQS recent CPU test
  if (content.includes("mlfqs_compare") && content.includes("recent_cpu")) {
    // Try to extract parameters
    const ticksMatch = content.match(/recent_delta.*fill\((\d+)\)/);
    const maxdiffMatch = content.match(/mlfqs_compare.*?,\s*(\d+(?:\.\d+)?)\s*,/);
    return {
      type: "mlfqs_recent",
      options: {},
      ticks: ticksMatch ? parseInt(ticksMatch[1], 10) : 100,
      maxdiff: maxdiffMatch ? parseFloat(maxdiffMatch[1]) : 6,
    };
  }

  // Check for MLFQS block test
  if (content.includes("mlfqs-block")) {
    return { type: "mlfqs_block", options: {} };
  }

  // Check for archive tests
  if (content.includes("check_archive")) {
    const tree = parseArchiveTree(content);
    return { type: "archive", options: {}, archiveTree: tree };
  }

  // Parse check_expected with options and heredoc
  const options: CheckOptions = {};

  // Check for IGNORE_EXIT_CODES
  if (content.includes("IGNORE_EXIT_CODES")) {
    options.IGNORE_EXIT_CODES = true;
  }

  // Check for IGNORE_USER_FAULTS
  if (content.includes("IGNORE_USER_FAULTS")) {
    options.IGNORE_USER_FAULTS = true;
  }

  // Parse Perl variables for interpolation in heredocs
  const vars = parsePerlVariables(content);

  // Extract heredoc content (supports multiple acceptable outputs)
  const expectedList = extractHeredocs(content, vars);
  if (expectedList !== null && expectedList.length > 0) {
    return {
      type: "expected",
      options,
      expected: expectedList,
    };
  }

  // Fallback to custom handling
  return { type: "custom", options };
}

/**
 * Extract all heredoc contents from check_expected call
 * Returns array of string arrays (multiple acceptable outputs)
 *
 * Handles: check_expected ([<<'EOF', <<'EOF']);
 * Where both heredocs may use the same delimiter name
 *
 * Perl heredoc interpolation rules:
 * - <<EOF or <<"EOF" - interpolates variables ($var, $arr[0])
 * - <<'EOF' - literal, no interpolation
 */
function extractHeredocs(content: string, vars: PerlVariables): string[][] | null {
  // Check if this is a check_expected call
  if (!content.includes("check_expected")) return null;

  const results: string[][] = [];

  // Find the check_expected line to count how many heredocs we need
  const checkExpectedMatch = content.match(/check_expected\s*\(\s*\[?([^\]]+)\]?\s*\)/);
  if (!checkExpectedMatch) return null;

  // Count heredoc markers and check their quoting style
  const callPart = checkExpectedMatch[1];
  const heredocMarkers = callPart.match(/<<(['"]?)(\w+)\1/g) || [];
  const heredocCount = heredocMarkers.length;

  if (heredocCount === 0) return null;

  // Determine which heredocs need interpolation
  // <<'EOF' = no interpolation, <<EOF or <<"EOF" = interpolation
  const needsInterpolation: boolean[] = heredocMarkers.map((marker) => {
    return !marker.includes("'"); // Single quotes disable interpolation
  });

  // Find the first newline after check_expected - heredoc content starts there
  const checkExpectedEnd = content.indexOf("\n", checkExpectedMatch.index!);
  if (checkExpectedEnd === -1) return null;

  // Now parse heredocs sequentially from the content after the check_expected line
  let pos = checkExpectedEnd + 1;

  for (let i = 0; i < heredocCount; i++) {
    // Find the next delimiter line (a line containing only the delimiter, typically EOF)
    // The delimiter is on its own line
    const remaining = content.slice(pos);

    // Find end of this heredoc by looking for a line that's just the delimiter
    // In Perl heredocs, the terminator must be on its own line
    const lines = remaining.split("\n");
    let heredocLines: string[] = [];
    let foundEnd = false;

    for (let j = 0; j < lines.length; j++) {
      const line = lines[j];
      // Check if this line is a heredoc terminator (just letters, possibly with trailing whitespace)
      if (/^\w+\s*$/.test(line) && line.trim() === "EOF") {
        foundEnd = true;
        pos += heredocLines.join("\n").length + 1 + line.length + 1; // +1 for newlines
        break;
      }
      heredocLines.push(line);
    }

    if (!foundEnd && i === heredocCount - 1) {
      // Last heredoc might not have been found correctly
      // Look for 'pass;' as end marker instead
      const passIdx = heredocLines.findIndex((l) => l.includes("pass;"));
      if (passIdx > 0) {
        // The line before 'pass;' should be the EOF marker
        heredocLines = heredocLines.slice(0, passIdx - 1);
      }
    }

    if (heredocLines.length > 0) {
      // Remove trailing empty line if present
      if (heredocLines[heredocLines.length - 1] === "") {
        heredocLines.pop();
      }

      // Apply variable interpolation if this heredoc uses interpolating syntax
      if (needsInterpolation[i]) {
        heredocLines = heredocLines.map((line) => interpolateVariables(line, vars));
      }

      results.push(heredocLines);
    }
  }

  return results.length > 0 ? results : null;
}

/**
 * Extract single heredoc (backwards compatibility)
 */
function extractHeredoc(content: string): string[] | null {
  const vars = parsePerlVariables(content);
  const heredocs = extractHeredocs(content, vars);
  return heredocs && heredocs.length > 0 ? heredocs[0] : null;
}

/**
 * Parse archive tree structure from .ck file
 * Handles patterns like:
 *   $tree->{a}{b}{c} = [''];
 *   for my $a (0...3) { ... $tree->{$a}{$b}{$c}{$d} = [''] }
 */
function parseArchiveTree(content: string): any {
  // Simple case: direct assignments like $tree->{'filename'} = ['content']
  // or nested loops creating directory structures

  // Check for loop-based tree generation
  const loopMatch = content.match(
    /for\s+my\s+\$(\w+)\s+\((\d+)\.\.\.?(\d+)\)/g
  );

  if (loopMatch && loopMatch.length >= 2) {
    // This is a nested loop structure - parse the bounds
    const bounds: Array<[string, number, number]> = [];
    const loopRegex = /for\s+my\s+\$(\w+)\s+\((\d+)\.\.\.?(\d+)\)/g;
    let match;
    while ((match = loopRegex.exec(content)) !== null) {
      bounds.push([match[1], parseInt(match[2], 10), parseInt(match[3], 10)]);
    }

    // Generate the tree structure
    return generateTreeFromLoops(bounds);
  }

  // Simple direct assignment case
  const simpleMatch = content.match(
    /\$\w+->\{['"]?([^}'"]+)['"]?\}\s*=\s*\[([^\]]*)\]/g
  );
  if (simpleMatch) {
    const tree: any = {};
    for (const m of simpleMatch) {
      const parts = m.match(/\{['"]?([^}'"]+)['"]?\}/g);
      const valueMatch = m.match(/=\s*\[([^\]]*)\]/);
      if (parts && valueMatch) {
        let current = tree;
        for (let i = 0; i < parts.length - 1; i++) {
          const key = parts[i].replace(/[{}'"`]/g, "");
          if (!current[key]) current[key] = {};
          current = current[key];
        }
        const lastKey = parts[parts.length - 1].replace(/[{}'"`]/g, "");
        const value = valueMatch[1].trim();
        current[lastKey] = value === "''" || value === '""' ? [""] : [value.replace(/['"]/g, "")];
      }
    }
    return tree;
  }

  // Can't parse - return empty
  return {};
}

/**
 * Generate tree structure from loop bounds
 * e.g., [(a, 0, 3), (b, 0, 2), (c, 0, 2), (d, 0, 3)] creates a 4-level directory tree
 */
function generateTreeFromLoops(bounds: Array<[string, number, number]>): any {
  if (bounds.length === 0) return [""];

  const [varName, start, end] = bounds[0];
  const remaining = bounds.slice(1);
  const tree: any = {};

  for (let i = start; i <= end; i++) {
    if (remaining.length === 0) {
      tree[String(i)] = [""];
    } else {
      tree[String(i)] = generateTreeFromLoops(remaining);
    }
  }

  return tree;
}

/**
 * Check if this .ck file can be handled by the TypeScript framework
 */
export function canHandle(parsed: ParsedChecker): boolean {
  return parsed.type !== "custom";
}
