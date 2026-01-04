---
sidebar_position: 4
---

# Contributing

Guidelines for contributing to PintOS.

## Development Workflow

1. **Fork** the repository
2. **Create a branch** for your feature: `git checkout -b feat/my-feature`
3. **Make changes** following the code style
4. **Test** your changes: `make check`
5. **Commit** with a descriptive message
6. **Push** and create a **Pull Request**

## Code Style

### Formatting

- **Indentation**: 2 spaces (no tabs)
- **Line length**: 80 characters max
- **Braces**: K&R style

```c
/* Good */
if (condition) {
  do_something();
} else {
  do_other();
}

/* Bad */
if (condition)
{
    do_something();
}
```

### Naming

- **Functions**: `snake_case` - `thread_create()`, `lock_acquire()`
- **Types**: `snake_case` with `_t` suffix for typedefs - `tid_t`, `pid_t`
- **Macros**: `UPPER_SNAKE_CASE` - `PGSIZE`, `ASSERT`
- **Constants**: `UPPER_SNAKE_CASE` - `PRI_MAX`, `PHYS_BASE`

### Comments

```c
/* Single-line comments use this style. */

/* Multi-line comments look like this.
   They wrap naturally at 80 characters
   and maintain consistent indentation. */

/* Function documentation:
   Brief description of what it does.

   ARGS: param1 - description
         param2 - description

   RETURNS: what it returns

   NOTE: any important caveats */
void example_function(int param1, char *param2) {
  /* Implementation */
}
```

## Commit Messages

Use conventional commits format:

```
<type>(<scope>): <subject>

<body>

<footer>
```

### Types

| Type | Description |
|------|-------------|
| `feat` | New feature |
| `fix` | Bug fix |
| `docs` | Documentation only |
| `refactor` | Code change that neither fixes a bug nor adds a feature |
| `test` | Adding or updating tests |
| `perf` | Performance improvement |

### Examples

```
feat(vm): implement demand paging for executable segments

- Add supplemental page table entry for file-backed pages
- Load pages lazily on first access via page fault
- Support partial page reads for final segment page

Closes #42
```

```
fix(synch): prevent priority inversion in lock_acquire

Priority donation was not being propagated through nested
lock chains. Now follows waiting_lock pointers to donate
through the entire chain.
```

## Testing

### Before Submitting

Run the test suite for affected components:

```bash
# Thread changes
cd src/threads && make check

# User program changes
cd src/userprog && make check

# VM changes
cd src/vm && make check

# Filesystem changes
cd src/filesys && make check
```

### Adding Tests

1. Create test file in `src/tests/<component>/`
2. Add expected output in `.ck` file
3. Register in `Make.tests`

## Pull Request Checklist

- [ ] Code follows style guidelines
- [ ] All tests pass
- [ ] New functionality has tests
- [ ] Documentation updated if needed
- [ ] Commit messages are descriptive
- [ ] No unrelated changes included

## Memory Safety

- Always check `malloc()`/`palloc_get_page()` return values
- Free allocated memory on all exit paths
- Use `ASSERT()` to catch programming errors
- Avoid unbounded recursion (4KB stack limit)

## Getting Help

- Read existing code for patterns
- Check the [Deep Dives](/docs/deep-dives/context-switch-assembly) for detailed explanations
- Open an issue for questions
