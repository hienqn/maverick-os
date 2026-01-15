/**
 * Alarm test verification
 * Port of tests/threads/alarm.pm
 */

import { readTextFile, commonChecks, fail, pass, log } from "./tests";

/**
 * Verify alarm test output
 *
 * The alarm test creates multiple threads that sleep for varying durations.
 * Expected wakeup order: (i+1) * (t+1) * 10 for i iterations, t=0..4
 * where i is the iteration number and t is the thread number.
 *
 * @param testName - Name of the test (used to find .output file)
 * @param iterations - Number of alarm iterations to verify
 */
export function checkAlarm(testName: string, iterations: number): never {
  const output = readTextFile(`${testName}.output`);
  commonChecks("run", output);

  // Calculate expected wakeup products
  const products: number[] = [];
  for (let i = 0; i < iterations; i++) {
    for (let t = 0; t < 5; t++) {
      products.push((i + 1) * (t + 1) * 10);
    }
  }
  products.sort((a, b) => a - b);

  // Verify output
  for (const line of output) {
    if (/out of order/i.test(line)) {
      fail(line);
    }

    const match = line.match(/product=(\d+)$/);
    if (!match) continue;

    const p = parseInt(match[1], 10);
    const q = products.shift();

    if (q === undefined) {
      fail("Too many wakeups.");
    }
    if (p !== q) {
      fail(`Out of order wakeups (${p} vs. ${q}).`);
    }
  }

  if (products.length !== 0) {
    fail(`${products.length} fewer wakeups than expected.`);
  }

  pass();
}
