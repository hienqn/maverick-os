/**
 * PintOS Grading Script
 *
 * Generates grade reports from test results.
 *
 * Usage: make-grade <src_dir> <results_file> <grading_file>
 *
 * - src_dir: Path to PintOS source directory
 * - results_file: File containing "pass test-name" or "FAIL test-name" lines
 * - grading_file: File specifying rubric weights (e.g., "20%\ttests/threads/Rubric.alarm")
 */

import { readFileSync } from "fs";

function main() {
  const args = process.argv.slice(2);
  if (args.length !== 3) {
    console.error("Usage: make-grade <src_dir> <results_file> <grading_file>");
    process.exit(1);
  }

  const [srcDir, resultsFile, gradingFile] = args;

  // Read pass/fail verdicts from results file
  const verdicts = new Map<string, boolean>();
  const verdictCounts = new Map<string, number>();

  const resultsContent = readFileSync(resultsFile, "utf-8");
  for (const line of resultsContent.split("\n")) {
    if (!line.trim()) continue;
    const match = line.match(/^(pass|FAIL) (.*)$/);
    if (!match) {
      console.error(`Invalid results line: ${line}`);
      process.exit(1);
    }
    verdicts.set(match[2], match[1] === "pass");
  }

  const failures: string[] = [];
  const overall: string[] = [];
  const rubrics: string[] = [];
  const summary: string[] = [];
  let pctActual = 0;
  let pctPossible = 0;

  // Read grading file
  const gradingContent = readFileSync(gradingFile, "utf-8");
  for (const line of gradingContent.split("\n")) {
    // Strip comments and skip empty lines
    const stripped = line.replace(/#.*/, "").trim();
    if (!stripped) continue;

    const gradingMatch = stripped.match(/^(\d+(?:\.\d+)?)%\t(.*)$/);
    if (!gradingMatch) {
      console.error(`Invalid grading line: ${line}`);
      process.exit(1);
    }

    const maxPct = parseFloat(gradingMatch[1]);
    const rubricSuffix = gradingMatch[2];
    const dir = rubricSuffix.replace(/\/[^/]*$/, "");
    const rubricFile = `${srcDir}/${rubricSuffix}`;

    let rubricContent: string;
    try {
      rubricContent = readFileSync(rubricFile, "utf-8");
    } catch (e) {
      console.error(`${rubricFile}: open failed`);
      process.exit(1);
    }

    const rubricLines = rubricContent.split("\n");

    // First line is title
    let title = rubricLines[0]?.trim() ?? "";
    if (!title.endsWith(":")) {
      console.error(`Rubric ${rubricFile} title must end with ':'`);
      process.exit(1);
    }
    title = title.slice(0, -1) + ` (${rubricSuffix}):`;
    rubrics.push(title);

    let score = 0;
    let possible = 0;
    let cnt = 0;
    let passed = 0;

    for (let i = 1; i < rubricLines.length; i++) {
      const rubricLine = rubricLines[i];
      if (rubricLine === undefined) continue;

      // Handle comment lines (starting with -)
      if (rubricLine.startsWith("-")) {
        rubrics.push(`\t${rubricLine}`);
        continue;
      }

      // Handle empty lines
      if (!rubricLine.trim()) {
        rubrics.push("");
        continue;
      }

      // Parse test entry: "points\ttest-name"
      const testMatch = rubricLine.match(/^(\d+)\t(.*)$/);
      if (!testMatch) {
        console.error(`Invalid rubric line in ${rubricFile}: ${rubricLine}`);
        process.exit(1);
      }

      const poss = parseInt(testMatch[1], 10);
      const name = testMatch[2];
      const test = `${dir}/${name}`;

      let points = 0;
      if (!verdicts.has(test)) {
        overall.push(`warning: ${test} not tested, assuming failure`);
      } else if (verdicts.get(test)) {
        points = poss;
        passed++;
      }

      if (!points) {
        failures.push(test);
      }

      verdictCounts.set(test, (verdictCounts.get(test) ?? 0) + 1);

      const marker = points ? "    " : "**  ";
      rubrics.push(
        `\t${marker}${points.toString().padStart(2)}/${poss.toString().padStart(2)} ${test}`
      );

      score += points;
      possible += poss;
      cnt++;
    }

    rubrics.push("");
    rubrics.push("\t- Section summary.");
    rubrics.push(`\t    ${passed.toString().padStart(3)}/${cnt.toString().padStart(3)} tests passed`);
    rubrics.push(
      `\t    ${score.toString().padStart(3)}/${possible.toString().padStart(3)} points subtotal`
    );
    rubrics.push("");

    const pct = (score / possible) * maxPct;
    summary.push(
      `${rubricSuffix.padEnd(45)} ${score.toString().padStart(3)}/${possible.toString().padStart(3)} ${pct.toFixed(1).padStart(5)}%/${maxPct.toFixed(1).padStart(5)}%`
    );

    pctActual += pct;
    pctPossible += maxPct;
  }

  // Build summary header
  const sumLine = "--------------------------------------------- --- --- ------ ------";
  summary.unshift(
    "SUMMARY BY TEST SET",
    "",
    `${"Test Set".padEnd(45)} ${"Pts".padStart(3)} ${"Max".padStart(3)} ${"% Ttl".padStart(6)} ${"% Max".padStart(6)}`,
    sumLine
  );
  summary.push(
    sumLine,
    `${"Total".padEnd(45)} ${"".padStart(3)} ${"".padStart(3)} ${pctActual.toFixed(1).padStart(5)}%/${pctPossible.toFixed(1).padStart(5)}%`
  );

  // Build rubrics header
  rubrics.unshift("SUMMARY OF INDIVIDUAL TESTS", "");

  // Check for tests that weren't counted or counted multiple times
  for (const [name, _verdict] of verdicts) {
    const count = verdictCounts.get(name);
    if (count === undefined || count !== 1) {
      if (!count) {
        overall.push(`warning: test ${name} doesn't count for grading`);
      } else {
        overall.push(`warning: test ${name} counted ${count} times in grading`);
      }
    }
  }

  overall.push(`TOTAL TESTING SCORE: ${pctActual.toFixed(1)}%`);
  if (pctActual.toFixed(1) === pctPossible.toFixed(1)) {
    overall.push("ALL TESTED PASSED -- PERFECT SCORE");
  }

  const divider = ["", "- ".repeat(38), ""];

  // Print output
  for (const line of [...overall, ...divider, ...summary, ...divider, ...rubrics]) {
    console.log(line);
  }

  // Print failure details
  for (const test of failures) {
    for (const line of divider) {
      console.log(line);
    }
    console.log(`DETAILS OF ${test} FAILURE:\n`);

    // Try to read .result file
    try {
      const resultContent = readFileSync(`${test}.result`, "utf-8");
      const lines = resultContent.split("\n");
      // Skip first line, print rest
      for (let i = 1; i < lines.length; i++) {
        console.log(lines[i]);
      }
    } catch {
      // File doesn't exist, ignore
    }

    // Try to read .output file
    try {
      const outputContent = readFileSync(`${test}.output`, "utf-8");
      console.log(`\nOUTPUT FROM ${test}:\n`);

      let panics = 0;
      let boots = 0;
      for (const line of outputContent.split("\n")) {
        if (line.includes("PANIC") && ++panics > 2) {
          console.log("[...details of additional panic(s) omitted...]");
          break;
        }
        console.log(line);
        if (line.includes("Pintos booting") && ++boots > 1) {
          console.log("[...details of reboot(s) omitted...]");
          break;
        }
      }
    } catch {
      // File doesn't exist, ignore
    }
  }
}

main();
