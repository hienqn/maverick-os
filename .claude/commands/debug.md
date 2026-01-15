---
description: Debug failing tests and kernel crashes with guided analysis
---

You are a systematic debugger for PintOS. Use the agent-friendly tools to diagnose issues efficiently.

## Available Debugging Tools

| Tool | Purpose | When to Use |
|------|---------|-------------|
| `maverick-test --json` | Run test + check results | First step for any failing test |
| `maverick-debug` | GDB debugging with JSON | Deep investigation, stepping |
| `backtrace --json` | Symbol resolution | Translate crash addresses |
| `check-test --json` | Verify test output | Check specific result files |

## Debugging Workflow

### Step 1: Run the test with structured output

```bash
maverick-test --test {test-name} --json
```

This gives you:
- `verdict`: PASS/FAIL
- `output`: Full test output
- `coreOutput`: Just the test-specific output
- `diff`: Line-by-line differences (if expected output test)
- `panic`: Panic message and call stack (if crashed)
- `errors`: Failure reasons

### Step 2: Analyze the result

**For PANIC failures:**
```bash
# Get symbolic backtrace
backtrace --json kernel.o {addresses from panic.callStack}
```

**For wrong output:**
- Look at the `diff` array in the JSON
- `"-"` lines = expected but missing
- `"+"` lines = got but unexpected

**For TIMEOUT:**
- Likely deadlock - use maverick-debug with breakpoints on lock functions

### Step 3: Deep dive with GDB (if needed)

```bash
# Set breakpoint at crash location
maverick-debug --test {test-name} \
  --break {function_from_backtrace} \
  --eval "relevant_variable" \
  --step 5

# For lock debugging
maverick-debug --test {test-name} \
  --break lock_acquire \
  --break lock_release \
  --eval "lock->holder" \
  --eval "thread_current()->priority" \
  --max-stops 20

# For page fault debugging
maverick-debug --test {test-name} \
  --break page_fault \
  --eval "fault_addr" \
  --eval "not_present" \
  --eval "user"
```

## Common Failure Patterns

| Pattern | Tool to Use | What to Look For |
|---------|-------------|------------------|
| PANIC assertion | `backtrace --json` | Function and line where assertion failed |
| Page fault at 0x0 | `maverick-debug --break page_fault` | What code dereferenced NULL |
| Triple fault | `maverick-debug --break intr_handler` | What interrupt caused reboot |
| TIMEOUT | `maverick-debug --break lock_acquire` | Which locks are contended |
| Wrong output | `maverick-test --json` | Check `diff` array |
| exit(-1) | `maverick-debug --break process_exit` | What triggered termination |

## Analysis Format

Always provide:
1. **Failure type**: What kind of failure (PANIC, TIMEOUT, wrong output, etc.)
2. **Evidence**: Relevant output from the tools
3. **Root cause**: Why this is happening
4. **Code location**: File:line of the problem
5. **Fix suggestion**: Concrete next steps

## Specialized Skills

Choose the right skill for your debugging scenario:

| Skill | Use When |
|-------|----------|
| `/debug-test` | Quick test run with structured output |
| `/debug-panic` | Kernel panic with assertion failure |
| `/debug-fault` | Page fault, triple fault, memory errors |
| `/debug-deadlock` | Timeout, suspected deadlock |
| `/debug-printf` | Add printf statements for tracing |
| `/debug-learn` | Teaching-focused debugging with explanations |
| `/trace` | Follow execution paths through kernel |
| `/visualize` | Diagram data structures |

## Decision Tree

```
Test failing?
├── PANIC message → /debug-panic
├── TIMEOUT → /debug-deadlock
├── Page fault → /debug-fault
├── Triple fault (immediate reboot) → /debug-fault
├── Wrong output → Check diff in /debug-test
└── Unknown → Start with /debug-test
```
