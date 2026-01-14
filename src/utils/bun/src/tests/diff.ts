/**
 * Diff algorithm implementation (McIlroy-Hunt LCS-based)
 * Port of Algorithm::Diff for test output comparison
 */

export interface DiffEntry {
  type: "-" | "+" | " ";
  line: string;
}

/**
 * Compute the longest common subsequence indices
 */
function lcsIndices(a: string[], b: string[]): Map<number, number> {
  const result = new Map<number, number>();

  // Build position map for sequence B
  const bPositions = new Map<string, number[]>();
  for (let i = 0; i < b.length; i++) {
    const positions = bPositions.get(b[i]) || [];
    positions.unshift(i); // prepend for reverse order
    bPositions.set(b[i], positions);
  }

  // Find LCS using patience-style algorithm
  const thresh: number[] = [];
  const links: Array<[typeof links[0] | null, number, number]> = [];

  for (let i = 0; i < a.length; i++) {
    const positions = bPositions.get(a[i]);
    if (!positions) continue;

    let k = 0;
    for (const j of positions) {
      // Binary search for insertion point
      let lo = 0;
      let hi = thresh.length;
      while (lo < hi) {
        const mid = (lo + hi) >>> 1;
        if (thresh[mid] < j) {
          lo = mid + 1;
        } else {
          hi = mid;
        }
      }

      // Found exact match - skip
      if (lo < thresh.length && thresh[lo] === j) continue;

      // Update threshold and links
      if (lo === thresh.length) {
        thresh.push(j);
      } else {
        thresh[lo] = j;
      }
      links[lo] = [lo > 0 ? links[lo - 1] : null, i, j];
      k = lo;
    }
  }

  // Backtrack to build match vector
  if (thresh.length > 0) {
    let link: typeof links[0] | null = links[thresh.length - 1];
    while (link) {
      result.set(link[1], link[2]);
      link = link[0];
    }
  }

  return result;
}

/**
 * Generate a unified-style diff between two arrays of lines
 */
export function diff(expected: string[], actual: string[]): DiffEntry[] {
  const matches = lcsIndices(expected, actual);
  const result: DiffEntry[] = [];

  let ai = 0;
  let bi = 0;

  while (ai < expected.length || bi < actual.length) {
    const matchB = matches.get(ai);

    if (matchB !== undefined && matchB === bi) {
      // Lines match
      result.push({ type: " ", line: expected[ai] });
      ai++;
      bi++;
    } else if (matchB !== undefined && bi < matchB) {
      // Extra line in actual (insertion)
      result.push({ type: "+", line: actual[bi] });
      bi++;
    } else if (ai < expected.length && (matchB === undefined || matchB > bi)) {
      // Missing line from expected (deletion)
      result.push({ type: "-", line: expected[ai] });
      ai++;
    } else if (bi < actual.length) {
      // Extra line in actual
      result.push({ type: "+", line: actual[bi] });
      bi++;
    }
  }

  return result;
}

/**
 * Format diff output for display
 */
export function formatDiff(entries: DiffEntry[]): string {
  return entries.map((e) => `${e.type} ${e.line}`).join("\n");
}

/**
 * Check if two arrays are identical
 */
export function arraysEqual(a: string[], b: string[]): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) return false;
  }
  return true;
}
