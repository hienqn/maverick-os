# Contributing to PintOS

Thank you for your interest in contributing to this PintOS implementation! This document provides guidelines for contributing to the project.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Workflow](#development-workflow)
- [Coding Standards](#coding-standards)
- [Commit Guidelines](#commit-guidelines)
- [Pull Request Process](#pull-request-process)
- [Testing](#testing)
- [Documentation](#documentation)

## Code of Conduct

Be respectful and constructive in all interactions. This is an educational project, and we welcome contributors of all experience levels.

## Getting Started

### Prerequisites

1. **Development Environment**
   - GCC cross-compiler for `i386-elf` target
   - QEMU or Bochs emulator
   - GNU Make
   - Perl 5.x

2. **Setup**
   ```bash
   git clone https://github.com/your-repo/pintos.git
   cd pintos
   ./setup-shell.sh
   ```

### Understanding the Codebase

Before contributing, familiarize yourself with:

- **[README.md](README.md)** - Project overview and architecture
- **[CHANGELOG.md](CHANGELOG.md)** - Version history
- **[docs/](docs/)** - Design documents and implementation guides

## Development Workflow

1. **Fork the repository** and clone your fork
2. **Create a feature branch** from `main`:
   ```bash
   git checkout -b feature/your-feature-name
   ```
3. **Make your changes** following the coding standards
4. **Test thoroughly** before submitting
5. **Commit with clear messages** following commit guidelines
6. **Push and create a Pull Request**

## Coding Standards

### Style Guide

This project follows the CS162 coding standards:

| Rule | Description |
|------|-------------|
| Indentation | 2 spaces (no tabs in C code) |
| Line Length | Maximum 80 characters |
| Braces | K&R style (opening brace on same line) |
| Naming | `snake_case` for functions and variables |
| Constants | `UPPER_SNAKE_CASE` for macros and constants |

### Example

```c
/* Brief description of the function.
   Detailed explanation if needed. */
static bool
example_function (struct thread *t, int value)
{
  if (t == NULL || value < 0)
    return false;

  t->some_field = value;
  return true;
}
```

### Documentation

- Add comments for non-obvious code
- Document all public functions with:
  - Brief description
  - Parameter explanations
  - Return value description
  - Any side effects or synchronization requirements

### Memory Safety

- Always check return values of memory allocation
- Free all allocated memory
- Avoid buffer overflows
- Validate user pointers before dereferencing

## Commit Guidelines

### Message Format

```
<type>: <short summary>

<optional body with details>

<optional footer>
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
feat: add copy-on-write support for fork

Implement lazy page copying for fork() system call.
Pages are marked read-only and shared between parent/child.
Actual copy is deferred until a write fault occurs.
```

```
fix: prevent priority inversion in lock_acquire

Re-sort the ready queue when donating priority to a
THREAD_READY recipient to ensure correct scheduling.
```

## Pull Request Process

### Before Submitting

1. **Run all tests** for affected components:
   ```bash
   cd src/threads && make check
   cd src/userprog && make check
   cd src/filesys && make check
   ```

2. **Verify your changes compile** without warnings:
   ```bash
   make clean && make 2>&1 | grep -i warning
   ```

3. **Update documentation** if needed

### PR Description

Your pull request should include:

- **Summary**: What does this PR do?
- **Motivation**: Why is this change needed?
- **Changes**: List of specific changes made
- **Testing**: How was this tested?
- **Related Issues**: Link any related issues

### Review Process

1. A maintainer will review your PR
2. Address any requested changes
3. Once approved, your PR will be merged

## Testing

### Running Tests

```bash
# Run all tests for a component
cd src/threads && make check

# Run a specific test
cd src/threads/build
make tests/threads/alarm-multiple.result

# View test output
cat tests/threads/alarm-multiple.output
```

### Writing Tests

If adding new functionality:

1. Create test file in appropriate `tests/` directory
2. Add test to the component's `Make.tests`
3. Create expected output `.ck` file
4. Verify test passes before submitting

## Documentation

### When to Update Docs

- Adding new features
- Changing existing behavior
- Fixing bugs that affect documented behavior
- Adding new system calls or APIs

### Where to Document

| Change | Location |
|--------|----------|
| New features | `README.md`, `CHANGELOG.md` |
| API changes | Header file comments |
| Design decisions | `docs/` directory |
| Bug fixes | `CHANGELOG.md` |

---

## Questions?

If you have questions about contributing, feel free to:

1. Open an issue with the `question` label
2. Review existing documentation in `docs/`

Thank you for contributing!
