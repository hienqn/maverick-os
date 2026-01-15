---
description: Quick test run and analysis with structured output
---

You are running and analyzing a PintOS test. Use the unified `maverick-test` tool for fast feedback.

## Step 1: Run the test

```bash
maverick-test --test {test-name} --json
```

This single command:
1. Finds the correct build directory
2. Runs the test in QEMU
3. Checks results against expected output
4. Returns structured JSON

## Step 2: Interpret the result

The JSON output contains:

| Field | Description |
|-------|-------------|
| `verdict` | "PASS" or "FAIL" |
| `output` | Full test output lines |
| `coreOutput` | Just the test-specific output (between "Executing..." and "Execution...complete") |
| `errors` | Array of failure reasons |
| `diff` | Line-by-line differences (if output mismatch) |
| `panic` | Panic message and call stack (if crashed) |

## Step 3: Diagnose failures

### For output mismatches (diff present)

Look at the `diff` array:
- `"-"` type = expected but missing
- `"+"` type = got but unexpected
- `" "` type = matched correctly

Example:
```json
"diff": [
  { "type": " ", "line": "(alarm-single) begin" },
  { "type": "-", "line": "(alarm-single) Thread 0 woke up." },
  { "type": "+", "line": "(alarm-single) Thread 1 woke up." }
]
```
This shows Thread 1 woke before Thread 0 (ordering bug).

### For panics

```bash
# Get symbolic backtrace
backtrace --json kernel.o {addresses from panic.callStack}
```

Then use `/debug-panic` for systematic panic analysis.

### For timeouts

```bash
# Debug with lock breakpoints
maverick-debug --test {test-name} \
  --break lock_acquire \
  --break sema_down \
  --eval "lock->holder" \
  --max-stops 20
```

Look for circular wait patterns.

## Quick Checks

```bash
# Just run and see pass/fail
maverick-test --test alarm-single

# Run with extended timeout
maverick-test --test priority-donate-chain --timeout 120 --json

# Run RISC-V architecture
maverick-test --test alarm-single --arch riscv64 --json
```

## Cross-references

- `/debug` - Full debugging workflow
- `/debug-panic` - Panic-specific analysis
- `/trace` - Follow execution paths
