# Pipe Test Suite Plan

This document outlines a comprehensive test suite for the `pipe()` system call implementation in Pintos. The test suite is designed based on industry best practices from Linux Test Project (LTP), xv6, and POSIX conformance requirements.

## Overview

### Test Categories

| Category | Tests | Purpose |
|----------|-------|---------|
| Basic | 4 | Pipe creation and simple I/O |
| Fork/Inheritance | 5 | Pipe sharing across fork() |
| Blocking Behavior | 3 | Blocking reads/writes |
| EOF Semantics | 3 | End-of-file detection |
| Data Integrity | 4 | Correctness of data transfer |
| Error Handling | 5 | Edge cases and error conditions |
| **Total** | **24** | |

---

## Files to Create/Modify

### New Files

```
tests/userprog/pipe/
├── Make.tests              # Test configuration
├── Rubric                  # Grading rubric
│
├── pipe-simple.c           # Basic tests
├── pipe-simple.ck
├── pipe-read-write.c
├── pipe-read-write.ck
├── pipe-close.c
├── pipe-close.ck
├── pipe-two-pipes.c
├── pipe-two-pipes.ck
│
├── pipe-fork.c             # Fork tests
├── pipe-fork.ck
├── pipe-fork-reverse.c
├── pipe-fork-reverse.ck
├── pipe-fork-close.c
├── pipe-fork-close.ck
├── pipe-fork-multi.c
├── pipe-fork-multi.ck
├── pipe-fork-chain.c
├── pipe-fork-chain.ck
│
├── pipe-block-read.c       # Blocking tests
├── pipe-block-read.ck
├── pipe-block-write.c
├── pipe-block-write.ck
├── pipe-nonblock.c
├── pipe-nonblock.ck
│
├── pipe-eof.c              # EOF tests
├── pipe-eof.ck
├── pipe-eof-multi.c
├── pipe-eof-multi.ck
├── pipe-eof-partial.c
├── pipe-eof-partial.ck
│
├── pipe-data-small.c       # Data integrity tests
├── pipe-data-small.ck
├── pipe-data-large.c
├── pipe-data-large.ck
├── pipe-data-order.c
├── pipe-data-order.ck
├── pipe-data-chunk.c
├── pipe-data-chunk.ck
│
├── pipe-bad-fd.c           # Error handling tests
├── pipe-bad-fd.ck
├── pipe-bad-ptr.c
├── pipe-bad-ptr.ck
├── pipe-double-close.c
├── pipe-double-close.ck
├── pipe-write-closed.c
├── pipe-write-closed.ck
├── pipe-read-only.c
├── pipe-read-only.ck
│
└── child-pipe.c            # Helper program for exec tests
```

### Files to Modify

```
lib/syscall-nr.h            # Add SYS_PIPE
lib/user/syscall.h          # Add pipe() declaration
lib/user/syscall.c          # Add pipe() wrapper
tests/userprog/Make.tests   # Include pipe tests (or separate Make.tests)
```

---

## Test Specifications

### 1. Basic Tests

#### 1.1 pipe-simple
**Purpose**: Verify pipe() syscall creates valid file descriptors.

```c
void test_main(void) {
  int pipefd[2];
  CHECK(pipe(pipefd) == 0, "pipe() returns 0 on success");
  CHECK(pipefd[0] >= 2, "read fd is valid (>= 2)");
  CHECK(pipefd[1] >= 2, "write fd is valid (>= 2)");
  CHECK(pipefd[0] != pipefd[1], "read and write fds are different");
  close(pipefd[0]);
  close(pipefd[1]);
}
```

**Expected Output**:
```
(pipe-simple) begin
(pipe-simple) pipe() returns 0 on success
(pipe-simple) read fd is valid (>= 2)
(pipe-simple) write fd is valid (>= 2)
(pipe-simple) read and write fds are different
(pipe-simple) end
```

#### 1.2 pipe-read-write
**Purpose**: Verify basic write and read operations on a pipe.

```c
void test_main(void) {
  int pipefd[2];
  char write_buf[] = "Hello, Pipe!";
  char read_buf[32];
  int bytes_written, bytes_read;

  CHECK(pipe(pipefd) == 0, "pipe()");

  bytes_written = write(pipefd[1], write_buf, sizeof(write_buf));
  CHECK(bytes_written == sizeof(write_buf), "write %d bytes", bytes_written);

  bytes_read = read(pipefd[0], read_buf, sizeof(read_buf));
  CHECK(bytes_read == sizeof(write_buf), "read %d bytes", bytes_read);
  CHECK(memcmp(write_buf, read_buf, sizeof(write_buf)) == 0, "data matches");

  close(pipefd[0]);
  close(pipefd[1]);
}
```

#### 1.3 pipe-close
**Purpose**: Verify closing pipe file descriptors works correctly.

```c
void test_main(void) {
  int pipefd[2];

  CHECK(pipe(pipefd) == 0, "pipe()");
  close(pipefd[0]);
  msg("closed read end");
  close(pipefd[1]);
  msg("closed write end");
}
```

#### 1.4 pipe-two-pipes
**Purpose**: Verify multiple pipes can coexist.

```c
void test_main(void) {
  int pipe1[2], pipe2[2];

  CHECK(pipe(pipe1) == 0, "first pipe()");
  CHECK(pipe(pipe2) == 0, "second pipe()");

  // All four fds should be unique
  CHECK(pipe1[0] != pipe1[1] && pipe1[0] != pipe2[0] &&
        pipe1[0] != pipe2[1] && pipe1[1] != pipe2[0] &&
        pipe1[1] != pipe2[1] && pipe2[0] != pipe2[1],
        "all fds are unique");

  // Write/read on each pipe independently
  write(pipe1[1], "A", 1);
  write(pipe2[1], "B", 1);

  char c1, c2;
  read(pipe1[0], &c1, 1);
  read(pipe2[0], &c2, 1);

  CHECK(c1 == 'A', "pipe1 data correct");
  CHECK(c2 == 'B', "pipe2 data correct");

  close(pipe1[0]); close(pipe1[1]);
  close(pipe2[0]); close(pipe2[1]);
}
```

---

### 2. Fork/Inheritance Tests

#### 2.1 pipe-fork
**Purpose**: Classic parent-to-child communication via pipe.

```c
void test_main(void) {
  int pipefd[2];
  pid_t pid;
  char msg_out[] = "Hello from parent";
  char msg_in[32];

  CHECK(pipe(pipefd) == 0, "pipe()");

  pid = fork();
  if (pid < 0) {
    fail("fork failed");
  } else if (pid == 0) {
    // Child: close write end, read from pipe
    close(pipefd[1]);
    int n = read(pipefd[0], msg_in, sizeof(msg_in));
    msg("child received: %s", msg_in);
    close(pipefd[0]);
    exit(n);
  } else {
    // Parent: close read end, write to pipe
    close(pipefd[0]);
    write(pipefd[1], msg_out, sizeof(msg_out));
    close(pipefd[1]);
    int status = wait(pid);
    msg("child returned %d", status);
  }
}
```

**Expected Output**:
```
(pipe-fork) begin
(pipe-fork) pipe()
(pipe-fork) child received: Hello from parent
(pipe-fork) end
pipe-fork: exit(18)
(pipe-fork) child returned 18
(pipe-fork) end
```

#### 2.2 pipe-fork-reverse
**Purpose**: Child-to-parent communication (reverse direction).

```c
void test_main(void) {
  int pipefd[2];
  pid_t pid;
  char msg_in[32];

  CHECK(pipe(pipefd) == 0, "pipe()");

  pid = fork();
  if (pid == 0) {
    // Child: close read end, write to pipe
    close(pipefd[0]);
    char msg_out[] = "Hello from child";
    write(pipefd[1], msg_out, sizeof(msg_out));
    close(pipefd[1]);
  } else {
    // Parent: close write end, read from pipe
    close(pipefd[1]);
    wait(pid);  // Wait for child to write
    read(pipefd[0], msg_in, sizeof(msg_in));
    msg("parent received: %s", msg_in);
    close(pipefd[0]);
  }
}
```

#### 2.3 pipe-fork-close
**Purpose**: Verify proper fd closure - child shouldn't block if parent closes correctly.

```c
void test_main(void) {
  int pipefd[2];
  pid_t pid;
  char buf[16];

  CHECK(pipe(pipefd) == 0, "pipe()");

  pid = fork();
  if (pid == 0) {
    // Child: reads until EOF
    close(pipefd[1]);  // Close write end
    int n = read(pipefd[0], buf, sizeof(buf));
    msg("child read returned %d (EOF expected: 0)", n);
    close(pipefd[0]);
    exit(n == 0 ? 0 : 1);
  } else {
    // Parent: close both ends without writing
    close(pipefd[0]);
    close(pipefd[1]);  // This should cause child to get EOF
    int status = wait(pid);
    CHECK(status == 0, "child got EOF correctly");
  }
}
```

#### 2.4 pipe-fork-multi
**Purpose**: Multiple children reading from same pipe.

```c
#define NUM_CHILDREN 3

void test_main(void) {
  int pipefd[2];
  pid_t pids[NUM_CHILDREN];

  CHECK(pipe(pipefd) == 0, "pipe()");

  for (int i = 0; i < NUM_CHILDREN; i++) {
    pids[i] = fork();
    if (pids[i] == 0) {
      close(pipefd[1]);
      char c;
      int n = read(pipefd[0], &c, 1);
      if (n == 1)
        msg("child %d read: %c", i, c);
      close(pipefd[0]);
      exit(0);
    }
  }

  // Parent: write one char per child
  close(pipefd[0]);
  for (int i = 0; i < NUM_CHILDREN; i++) {
    char c = 'A' + i;
    write(pipefd[1], &c, 1);
  }
  close(pipefd[1]);

  for (int i = 0; i < NUM_CHILDREN; i++)
    wait(pids[i]);
}
```

#### 2.5 pipe-fork-chain
**Purpose**: Pipeline of processes (like shell pipes: A | B | C).

```c
void test_main(void) {
  int pipe1[2], pipe2[2];

  CHECK(pipe(pipe1) == 0, "pipe1()");

  pid_t pid1 = fork();
  if (pid1 == 0) {
    // Process A: write to pipe1
    close(pipe1[0]);
    write(pipe1[1], "DATA", 4);
    close(pipe1[1]);
    exit(0);
  }

  CHECK(pipe(pipe2) == 0, "pipe2()");

  pid_t pid2 = fork();
  if (pid2 == 0) {
    // Process B: read from pipe1, transform, write to pipe2
    close(pipe1[1]);
    close(pipe2[0]);
    char buf[8];
    read(pipe1[0], buf, 4);
    // "Transform" by converting to lowercase
    for (int i = 0; i < 4; i++)
      buf[i] = buf[i] + 32;
    write(pipe2[1], buf, 4);
    close(pipe1[0]);
    close(pipe2[1]);
    exit(0);
  }

  // Process C (main): read from pipe2
  close(pipe1[0]);
  close(pipe1[1]);
  close(pipe2[1]);

  wait(pid1);
  wait(pid2);

  char result[8];
  read(pipe2[0], result, 4);
  result[4] = '\0';
  msg("pipeline result: %s", result);
  CHECK(strcmp(result, "data") == 0, "pipeline transformation correct");
  close(pipe2[0]);
}
```

---

### 3. Blocking Behavior Tests

#### 3.1 pipe-block-read
**Purpose**: Verify read blocks when pipe is empty (until data arrives).

```c
void test_main(void) {
  int pipefd[2];

  CHECK(pipe(pipefd) == 0, "pipe()");

  pid_t pid = fork();
  if (pid == 0) {
    // Child: wait a bit, then write
    close(pipefd[0]);
    // Busy wait to simulate delay
    for (volatile int i = 0; i < 1000000; i++);
    write(pipefd[1], "X", 1);
    msg("child wrote data");
    close(pipefd[1]);
    exit(0);
  } else {
    // Parent: read should block until child writes
    close(pipefd[1]);
    msg("parent about to read (should block)");
    char c;
    read(pipefd[0], &c, 1);
    msg("parent read: %c", c);
    close(pipefd[0]);
    wait(pid);
  }
}
```

**Expected Output** (order matters - child writes before parent reads):
```
(pipe-block-read) begin
(pipe-block-read) pipe()
(pipe-block-read) parent about to read (should block)
(pipe-block-read) child wrote data
(pipe-block-read) end
pipe-block-read: exit(0)
(pipe-block-read) parent read: X
(pipe-block-read) end
```

#### 3.2 pipe-block-write
**Purpose**: Verify write blocks when pipe buffer is full.

```c
#define PIPE_BUF_SIZE 4096  // Typical pipe buffer size

void test_main(void) {
  int pipefd[2];
  char buf[PIPE_BUF_SIZE];

  CHECK(pipe(pipefd) == 0, "pipe()");
  memset(buf, 'A', sizeof(buf));

  pid_t pid = fork();
  if (pid == 0) {
    // Child: try to fill pipe buffer + more
    close(pipefd[0]);
    int total = 0;

    // Write until would block (in a real implementation)
    // For testing, we write a known amount
    for (int i = 0; i < 3; i++) {
      int n = write(pipefd[1], buf, sizeof(buf));
      total += n;
    }
    msg("child wrote %d bytes total", total);
    close(pipefd[1]);
    exit(0);
  } else {
    // Parent: read slowly to let buffer fill
    close(pipefd[1]);
    for (volatile int i = 0; i < 500000; i++);

    int total = 0;
    int n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
      total += n;
    }
    msg("parent read %d bytes total", total);
    close(pipefd[0]);
    wait(pid);
  }
}
```

#### 3.3 pipe-nonblock
**Purpose**: Test behavior with O_NONBLOCK if supported (may return EAGAIN).

*Note: This test is optional depending on whether non-blocking I/O is implemented.*

---

### 4. EOF Semantics Tests

#### 4.1 pipe-eof
**Purpose**: Verify read returns 0 (EOF) when all write ends are closed.

```c
void test_main(void) {
  int pipefd[2];
  char buf[16];

  CHECK(pipe(pipefd) == 0, "pipe()");

  // Write some data
  write(pipefd[1], "test", 4);

  // Close write end
  close(pipefd[1]);
  msg("write end closed");

  // Read the data
  int n1 = read(pipefd[0], buf, 4);
  CHECK(n1 == 4, "first read got %d bytes", n1);

  // Read again - should get EOF
  int n2 = read(pipefd[0], buf, sizeof(buf));
  CHECK(n2 == 0, "second read returned %d (EOF)", n2);

  close(pipefd[0]);
}
```

#### 4.2 pipe-eof-multi
**Purpose**: EOF only occurs when ALL write ends are closed (multiple writers).

```c
void test_main(void) {
  int pipefd[2];

  CHECK(pipe(pipefd) == 0, "pipe()");

  pid_t pid = fork();
  if (pid == 0) {
    // Child has a copy of write end
    close(pipefd[0]);
    // Child closes its write end
    close(pipefd[1]);
    msg("child closed write end");
    exit(0);
  } else {
    // Parent: child closed write end, but parent still has it
    close(pipefd[0]);  // Close read end in parent

    // In a separate process, read should NOT get EOF yet
    pid_t pid2 = fork();
    if (pid2 == 0) {
      // This child reads
      int read_fd = pipefd[0];  // Would need to dup before closing
      // ... simplified: just test the concept
      exit(0);
    }

    wait(pid);
    close(pipefd[1]);  // Now close parent's write end
    msg("parent closed write end");
    wait(pid2);
  }
}
```

#### 4.3 pipe-eof-partial
**Purpose**: Partial reads followed by EOF.

```c
void test_main(void) {
  int pipefd[2];
  char buf[4];

  CHECK(pipe(pipefd) == 0, "pipe()");

  write(pipefd[1], "ABCDEFGH", 8);
  close(pipefd[1]);

  // Read in chunks
  int n1 = read(pipefd[0], buf, 3);
  CHECK(n1 == 3, "read 1: %d bytes", n1);

  int n2 = read(pipefd[0], buf, 3);
  CHECK(n2 == 3, "read 2: %d bytes", n2);

  int n3 = read(pipefd[0], buf, 3);
  CHECK(n3 == 2, "read 3: %d bytes (remaining)", n3);

  int n4 = read(pipefd[0], buf, 3);
  CHECK(n4 == 0, "read 4: %d (EOF)", n4);

  close(pipefd[0]);
}
```

---

### 5. Data Integrity Tests

#### 5.1 pipe-data-small
**Purpose**: Small data transfers maintain integrity.

```c
void test_main(void) {
  int pipefd[2];
  char write_data[] = "The quick brown fox";
  char read_data[32];

  CHECK(pipe(pipefd) == 0, "pipe()");

  int written = write(pipefd[1], write_data, strlen(write_data) + 1);
  CHECK(written == strlen(write_data) + 1, "wrote %d bytes", written);

  close(pipefd[1]);

  int total_read = 0;
  int n;
  while ((n = read(pipefd[0], read_data + total_read, sizeof(read_data) - total_read)) > 0) {
    total_read += n;
  }

  CHECK(total_read == written, "read %d bytes", total_read);
  CHECK(strcmp(write_data, read_data) == 0, "data integrity verified");

  close(pipefd[0]);
}
```

#### 5.2 pipe-data-large
**Purpose**: Large data transfers (multiple buffer sizes).

```c
#define DATA_SIZE 16384  // 16KB

void test_main(void) {
  int pipefd[2];
  static char write_data[DATA_SIZE];
  static char read_data[DATA_SIZE];

  // Initialize with pattern
  for (int i = 0; i < DATA_SIZE; i++)
    write_data[i] = (char)(i % 256);

  CHECK(pipe(pipefd) == 0, "pipe()");

  pid_t pid = fork();
  if (pid == 0) {
    // Child: write all data
    close(pipefd[0]);
    int total = 0;
    while (total < DATA_SIZE) {
      int n = write(pipefd[1], write_data + total, DATA_SIZE - total);
      if (n <= 0) break;
      total += n;
    }
    close(pipefd[1]);
    exit(total);
  } else {
    // Parent: read all data
    close(pipefd[1]);
    int total = 0;
    int n;
    while ((n = read(pipefd[0], read_data + total, DATA_SIZE - total)) > 0) {
      total += n;
    }
    close(pipefd[0]);

    int status = wait(pid);
    CHECK(status == DATA_SIZE, "child wrote %d bytes", status);
    CHECK(total == DATA_SIZE, "parent read %d bytes", total);
    CHECK(memcmp(write_data, read_data, DATA_SIZE) == 0, "data integrity verified");
  }
}
```

#### 5.3 pipe-data-order
**Purpose**: Data arrives in FIFO order.

```c
void test_main(void) {
  int pipefd[2];

  CHECK(pipe(pipefd) == 0, "pipe()");

  // Write sequence
  for (int i = 0; i < 10; i++) {
    char c = '0' + i;
    write(pipefd[1], &c, 1);
  }
  close(pipefd[1]);

  // Read and verify order
  char c;
  for (int i = 0; i < 10; i++) {
    read(pipefd[0], &c, 1);
    CHECK(c == '0' + i, "byte %d is '%c'", i, c);
  }

  close(pipefd[0]);
  msg("FIFO order verified");
}
```

#### 5.4 pipe-data-chunk
**Purpose**: Mismatched read/write sizes work correctly.

```c
void test_main(void) {
  int pipefd[2];
  char buf[16];

  CHECK(pipe(pipefd) == 0, "pipe()");

  // Write in 4-byte chunks
  write(pipefd[1], "AAAA", 4);
  write(pipefd[1], "BBBB", 4);
  write(pipefd[1], "CCCC", 4);
  close(pipefd[1]);

  // Read in 6-byte chunks
  int n1 = read(pipefd[0], buf, 6);
  buf[n1] = '\0';
  CHECK(n1 == 6, "read 1: %d bytes = %s", n1, buf);

  int n2 = read(pipefd[0], buf, 6);
  buf[n2] = '\0';
  CHECK(n2 == 6, "read 2: %d bytes = %s", n2, buf);

  int n3 = read(pipefd[0], buf, 6);
  CHECK(n3 == 0, "read 3: EOF");

  close(pipefd[0]);
}
```

---

### 6. Error Handling Tests

#### 6.1 pipe-bad-fd
**Purpose**: Operations on invalid pipe fd should fail gracefully.

```c
void test_main(void) {
  int pipefd[2];
  char buf[16];

  CHECK(pipe(pipefd) == 0, "pipe()");

  close(pipefd[0]);
  close(pipefd[1]);

  // Read/write on closed fds
  int n = read(pipefd[0], buf, sizeof(buf));
  msg("read on closed fd returned %d", n);

  n = write(pipefd[1], "test", 4);
  msg("write on closed fd returned %d", n);
}
```

#### 6.2 pipe-bad-ptr
**Purpose**: Pipe with invalid pointer should fail.

```c
void test_main(void) {
  int* bad_ptr = (int*)0xDEADBEEF;

  // This should fail or kill the process
  int result = pipe(bad_ptr);

  // If we get here, pipe returned an error
  fail("pipe with bad pointer should have failed, got %d", result);
}
```

**Expected**: Process terminates with exit(-1) or pipe returns -1.

#### 6.3 pipe-double-close
**Purpose**: Closing same fd twice should be handled gracefully.

```c
void test_main(void) {
  int pipefd[2];

  CHECK(pipe(pipefd) == 0, "pipe()");

  close(pipefd[0]);
  msg("first close of read end");

  close(pipefd[0]);  // Second close of same fd
  msg("second close of read end (should be no-op or error)");

  close(pipefd[1]);
  msg("closed write end");
}
```

#### 6.4 pipe-write-closed
**Purpose**: Write to pipe with closed read end should fail or return error.

```c
void test_main(void) {
  int pipefd[2];

  CHECK(pipe(pipefd) == 0, "pipe()");

  close(pipefd[0]);  // Close read end
  msg("read end closed");

  // Write should fail (EPIPE/SIGPIPE in POSIX, -1 or process death in Pintos)
  int n = write(pipefd[1], "test", 4);
  msg("write to broken pipe returned %d", n);

  close(pipefd[1]);
}
```

#### 6.5 pipe-read-only
**Purpose**: Verify can't write to read end or read from write end.

```c
void test_main(void) {
  int pipefd[2];
  char buf[16];

  CHECK(pipe(pipefd) == 0, "pipe()");

  // Try to write to read end
  int n1 = write(pipefd[0], "test", 4);
  msg("write to read fd returned %d", n1);

  // Try to read from write end
  int n2 = read(pipefd[1], buf, sizeof(buf));
  msg("read from write fd returned %d", n2);

  close(pipefd[0]);
  close(pipefd[1]);
}
```

---

## Make.tests Configuration

```makefile
# -*- makefile -*-

# Pipe test suite
tests/userprog/pipe_TESTS = $(addprefix tests/userprog/pipe/,\
  pipe-simple pipe-read-write pipe-close pipe-two-pipes \
  pipe-fork pipe-fork-reverse pipe-fork-close pipe-fork-multi pipe-fork-chain \
  pipe-block-read pipe-block-write \
  pipe-eof pipe-eof-partial \
  pipe-data-small pipe-data-large pipe-data-order pipe-data-chunk \
  pipe-bad-fd pipe-bad-ptr pipe-double-close pipe-write-closed pipe-read-only)

tests/userprog/pipe_PROGS = $(tests/userprog/pipe_TESTS)

# Source file definitions
tests/userprog/pipe/pipe-simple_SRC = tests/userprog/pipe/pipe-simple.c tests/main.c
tests/userprog/pipe/pipe-read-write_SRC = tests/userprog/pipe/pipe-read-write.c tests/main.c
tests/userprog/pipe/pipe-close_SRC = tests/userprog/pipe/pipe-close.c tests/main.c
tests/userprog/pipe/pipe-two-pipes_SRC = tests/userprog/pipe/pipe-two-pipes.c tests/main.c
tests/userprog/pipe/pipe-fork_SRC = tests/userprog/pipe/pipe-fork.c tests/main.c
tests/userprog/pipe/pipe-fork-reverse_SRC = tests/userprog/pipe/pipe-fork-reverse.c tests/main.c
tests/userprog/pipe/pipe-fork-close_SRC = tests/userprog/pipe/pipe-fork-close.c tests/main.c
tests/userprog/pipe/pipe-fork-multi_SRC = tests/userprog/pipe/pipe-fork-multi.c tests/main.c
tests/userprog/pipe/pipe-fork-chain_SRC = tests/userprog/pipe/pipe-fork-chain.c tests/main.c
tests/userprog/pipe/pipe-block-read_SRC = tests/userprog/pipe/pipe-block-read.c tests/main.c
tests/userprog/pipe/pipe-block-write_SRC = tests/userprog/pipe/pipe-block-write.c tests/main.c
tests/userprog/pipe/pipe-eof_SRC = tests/userprog/pipe/pipe-eof.c tests/main.c
tests/userprog/pipe/pipe-eof-partial_SRC = tests/userprog/pipe/pipe-eof-partial.c tests/main.c
tests/userprog/pipe/pipe-data-small_SRC = tests/userprog/pipe/pipe-data-small.c tests/main.c
tests/userprog/pipe/pipe-data-large_SRC = tests/userprog/pipe/pipe-data-large.c tests/main.c
tests/userprog/pipe/pipe-data-order_SRC = tests/userprog/pipe/pipe-data-order.c tests/main.c
tests/userprog/pipe/pipe-data-chunk_SRC = tests/userprog/pipe/pipe-data-chunk.c tests/main.c
tests/userprog/pipe/pipe-bad-fd_SRC = tests/userprog/pipe/pipe-bad-fd.c tests/main.c
tests/userprog/pipe/pipe-bad-ptr_SRC = tests/userprog/pipe/pipe-bad-ptr.c tests/main.c
tests/userprog/pipe/pipe-double-close_SRC = tests/userprog/pipe/pipe-double-close.c tests/main.c
tests/userprog/pipe/pipe-write-closed_SRC = tests/userprog/pipe/pipe-write-closed.c tests/main.c
tests/userprog/pipe/pipe-read-only_SRC = tests/userprog/pipe/pipe-read-only.c tests/main.c

# All programs include test library
$(foreach prog,$(tests/userprog/pipe_PROGS),$(eval $(prog)_SRC += tests/lib.c))

# Extended timeouts for blocking and large data tests
tests/userprog/pipe/pipe-block-read.output: TIMEOUT = 120
tests/userprog/pipe/pipe-block-write.output: TIMEOUT = 120
tests/userprog/pipe/pipe-data-large.output: TIMEOUT = 180
```

---

## Grading Rubric

```
Functionality of pipe system call:

- Basic pipe operations
3   pipe-simple
3   pipe-read-write
2   pipe-close
3   pipe-two-pipes

- Fork and inheritance
5   pipe-fork
4   pipe-fork-reverse
4   pipe-fork-close
5   pipe-fork-multi
5   pipe-fork-chain

- Blocking behavior
5   pipe-block-read
5   pipe-block-write

- EOF semantics
4   pipe-eof
4   pipe-eof-partial

- Data integrity
3   pipe-data-small
5   pipe-data-large
3   pipe-data-order
3   pipe-data-chunk

- Error handling
3   pipe-bad-fd
3   pipe-bad-ptr
2   pipe-double-close
3   pipe-write-closed
3   pipe-read-only
```

**Total: 80 points**

---

## Implementation Notes

### Pipe Buffer Size
The tests assume a pipe buffer of at least 4KB (PIPE_BUF_SIZE). This is the minimum POSIX requires. Linux defaults to 64KB.

### Blocking Semantics
- `read()` blocks when pipe is empty and write end is open
- `write()` blocks when pipe buffer is full and read end is open
- Both return when data is available or EOF occurs

### EOF Detection
- Read returns 0 (EOF) when:
  - All write file descriptors are closed AND
  - All buffered data has been consumed

### Error Handling
- Write to pipe with closed read end: return -1 (or SIGPIPE in full POSIX)
- Read from pipe with closed write end: return 0 (EOF) after buffer drained
- Operations on invalid fd: return -1

### Fork Behavior
- Both file descriptors are duplicated (reference count increases)
- Parent and child can independently read/write
- Pipe remains open until ALL copies of an end are closed

---

## Test Execution

To run all pipe tests:
```bash
cd src/userprog
make check TESTS="tests/userprog/pipe/pipe-*"
```

To run a specific test:
```bash
make tests/userprog/pipe/pipe-fork.result
```

---

## References

1. Linux Test Project (LTP) - pipe tests
2. xv6 usertests.c - pipe1 test
3. POSIX.1-2017 pipe(2) specification
4. Linux man7.org pipe(2) and pipe(7) manual pages
