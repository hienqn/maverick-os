/**
 * MLFQS (Multi-Level Feedback Queue Scheduler) test verification
 * Port of tests/threads/mlfqs.pm
 */

import {
  readTextFile,
  commonChecks,
  getCoreOutput,
  fail,
  pass,
  log,
  initTest,
} from "./tests";

/**
 * Calculate expected load average and recent CPU values
 */
export function mlfqsExpectedLoad(
  ready: number[],
  recentDelta: (number | undefined)[]
): { loadAvg: number[]; recentCpu: number[] } {
  const loadAvgArr: number[] = [0];
  const recentCpuArr: number[] = [0];
  let loadAvg = 0;
  let recentCpu = 0;

  for (let i = 0; i < ready.length; i++) {
    loadAvg = (59 / 60) * loadAvg + (1 / 60) * ready[i];
    loadAvgArr.push(loadAvg);

    if (recentDelta[i] !== undefined) {
      const twiceLoad = loadAvg * 2;
      const loadFactor = twiceLoad / (twiceLoad + 1);
      recentCpu = (recentCpu + recentDelta[i]!) * loadFactor;
      recentCpuArr.push(recentCpu);
    }
  }

  return { loadAvg: loadAvgArr, recentCpu: recentCpuArr };
}

/**
 * Simulate MLFQS scheduler for 750 ticks and return expected tick counts
 */
export function mlfqsExpectedTicks(nice: number[]): number[] {
  const threadCnt = nice.length;
  const recentCpu = new Array(threadCnt).fill(0);
  const slices = new Array(threadCnt).fill(0);
  const fifo = new Array(threadCnt).fill(0);
  let nextFifo = 1;
  let loadAvg = 0;

  for (let i = 1; i <= 750; i++) {
    if (i % 25 === 0) {
      // Update load average
      loadAvg = (59 / 60) * loadAvg + (1 / 60) * threadCnt;

      // Update recent_cpu
      const twiceLoad = loadAvg * 2;
      const loadFactor = twiceLoad / (twiceLoad + 1);
      for (let j = 0; j < threadCnt; j++) {
        recentCpu[j] = recentCpu[j] * loadFactor + nice[j];
      }
    }

    // Update priorities
    const priority: number[] = [];
    for (let j = 0; j < threadCnt; j++) {
      let p = Math.floor(recentCpu[j] / 4 + nice[j] * 2);
      p = Math.max(0, Math.min(63, p));
      priority.push(p);
    }

    // Choose thread to run (lowest priority number = highest priority)
    let max = 0;
    for (let j = 1; j < priority.length; j++) {
      if (
        priority[j] < priority[max] ||
        (priority[j] === priority[max] && fifo[j] < fifo[max])
      ) {
        max = j;
      }
    }
    fifo[max] = nextFifo++;

    // Run thread
    recentCpu[max] += 4;
    slices[max] += 4;
  }

  return slices;
}

/**
 * Compare actual vs expected values with tolerance
 */
export function mlfqsCompare(
  indepVar: string,
  format: string,
  actual: (number | undefined)[],
  expected: number[],
  maxdiff: number,
  tRange: [number, number, number],
  message: string
): boolean {
  const [tMin, tMax, tStep] = tRange;

  // Check if all values are within tolerance
  let ok = true;
  for (let t = tMin; t <= tMax; t += tStep) {
    const act = actual[t];
    const exp = expected[t];
    if (act === undefined || Math.abs(act - exp) > maxdiff + 0.01) {
      ok = false;
      break;
    }
  }

  if (ok) return true;

  // Output comparison table
  log(message);
  mlfqsRow(indepVar, "actual", "<->", "expected", "explanation");
  mlfqsRow("------", "--------", "---", "--------", "-".repeat(40));

  for (let t = tMin; t <= tMax; t += tStep) {
    const act = actual[t];
    const exp = expected[t];
    let actStr: string;
    let diff: string;
    let rationale: string;

    if (act === undefined) {
      actStr = "undef";
      diff = "";
      rationale = "Missing value.";
    } else {
      const delta = Math.abs(act - exp);
      if (delta > maxdiff + 0.01) {
        const excess = delta - maxdiff;
        if (act > exp) {
          diff = ">>>";
          rationale = `Too big, by ${formatNumber(format, excess)}.`;
        } else {
          diff = "<<<";
          rationale = `Too small, by ${formatNumber(format, excess)}.`;
        }
      } else {
        diff = " = ";
        rationale = "";
      }
      actStr = formatNumber(format, act);
    }

    const expStr = formatNumber(format, exp);
    mlfqsRow(String(t), actStr, diff, expStr, rationale);
  }

  return false;
}

function mlfqsRow(
  col1: string,
  col2: string,
  col3: string,
  col4: string,
  col5: string
): void {
  log(
    `${col1.toString().padStart(6)} ${col2.toString().padStart(8)} ${col3.padStart(3)} ${col4.padEnd(8)} ${col5}`
  );
}

function formatNumber(format: string, value: number): string {
  if (format === "%d") {
    return String(Math.round(value));
  } else if (format === "%.2f") {
    return value.toFixed(2);
  }
  return String(value);
}

/**
 * Check MLFQS fair scheduling test
 */
export function checkMlfqsFair(
  testName: string,
  nice: number[],
  maxdiff: number
): never {
  const output = readTextFile(`${testName}.output`);
  commonChecks("run", output);
  const coreOutput = getCoreOutput("run", output);

  // Extract actual tick counts
  const actual: (number | undefined)[] = [];
  for (const line of coreOutput) {
    const match = line.match(/Thread (\d+) received (\d+) ticks\./);
    if (match) {
      const id = parseInt(match[1], 10);
      const count = parseInt(match[2], 10);
      actual[id] = count;
    }
  }

  // Calculate expected values
  const expected = mlfqsExpectedTicks(nice);

  // Compare
  const ok = mlfqsCompare(
    "thread",
    "%d",
    actual,
    expected,
    maxdiff,
    [0, nice.length - 1, 1],
    `Some tick counts were missing or differed from those expected by more than ${maxdiff}.`
  );

  if (!ok) {
    fail();
  }
  pass();
}

/**
 * Check MLFQS load average test
 */
export function checkMlfqsLoadAvg(testName: string): never {
  const output = readTextFile(`${testName}.output`);
  commonChecks("run", output);
  const coreOutput = getCoreOutput("run", output);

  // Extract actual values
  const actual: (number | undefined)[] = [];
  for (const line of coreOutput) {
    const match = line.match(/After (\d+) seconds, load average=(\d+\.\d+)\./);
    if (match) {
      const t = parseInt(match[1], 10);
      const loadAvg = parseFloat(match[2]);
      actual[t] = loadAvg;
    }
  }

  // Calculate expected values
  let loadAvg = 0;
  const expected: number[] = [];
  for (let t = 0; t < 180; t++) {
    // ready = t for t<60, 120-t for 60<=t<120, 0 for t>=120
    const ready = t < 60 ? t : t < 120 ? 120 - t : 0;
    loadAvg = (59 / 60) * loadAvg + (1 / 60) * ready;
    expected[t] = loadAvg;
  }

  // Compare
  const ok = mlfqsCompare(
    "time",
    "%.2f",
    actual,
    expected,
    2.5,
    [2, 178, 2],
    "Some load average values were missing or differed from those expected by more than 2.5."
  );

  if (!ok) {
    fail();
  }
  pass();
}

/**
 * Check MLFQS recent CPU test
 */
export function checkMlfqsRecentCpu(
  testName: string,
  ticks: number,
  maxdiff: number
): never {
  const output = readTextFile(`${testName}.output`);
  commonChecks("run", output);
  const coreOutput = getCoreOutput("run", output);

  // Extract actual values
  const actual: (number | undefined)[] = [];
  for (const line of coreOutput) {
    const match = line.match(/After (\d+) seconds, recent_cpu=(\d+\.\d+)\./);
    if (match) {
      const t = parseInt(match[1], 10);
      const recentCpu = parseFloat(match[2]);
      actual[t] = recentCpu;
    }
  }

  // Calculate expected values
  const ready = new Array(180).fill(1);
  const recentDelta = new Array(180).fill(ticks);
  const { recentCpu: expected } = mlfqsExpectedLoad(ready, recentDelta);

  // Compare
  const ok = mlfqsCompare(
    "time",
    "%.2f",
    actual,
    expected.slice(1), // Skip index 0
    maxdiff,
    [2, 178, 2],
    `Some recent_cpu values were missing or differed from those expected by more than ${maxdiff}.`
  );

  if (!ok) {
    fail();
  }
  pass();
}

/**
 * Check MLFQS nice test
 */
export function checkMlfqsNice(
  testName: string,
  nice: number[],
  maxdiff: number
): never {
  checkMlfqsFair(testName, nice, maxdiff);
}

/**
 * Check MLFQS block test
 */
export function checkMlfqsBlock(testName: string): never {
  const output = readTextFile(`${testName}.output`);
  commonChecks("run", output);
  const coreOutput = getCoreOutput("run", output);

  // Look for the expected blocking/unblocking pattern
  const expected = [
    "(mlfqs-block) Main thread acquiring lock.",
    "(mlfqs-block) Main thread spinning 500 times.",
    "(mlfqs-block) Block thread spinning 750 times.",
    "(mlfqs-block) Main thread releasing lock.",
    "(mlfqs-block) Block thread acquired lock.",
  ];

  let expIdx = 0;
  for (const line of coreOutput) {
    if (expIdx < expected.length && line === expected[expIdx]) {
      expIdx++;
    }
  }

  if (expIdx !== expected.length) {
    log("Expected output sequence not found.");
    log("Expected:");
    expected.forEach((e) => log(`  ${e}`));
    fail();
  }

  pass();
}
