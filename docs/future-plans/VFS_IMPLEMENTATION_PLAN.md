# VFS (Virtual Filesystem Switch) Implementation Plan

## Overview

Add a VFS abstraction layer to Pintos that:
1. Standardizes the file interface and allows pluggable filesystem implementations
2. **Provides a unified device I/O subsystem** where devices are accessed as files (`/dev/*`)

This implements the Unix "everything is a file" philosophy.

## Current Architecture

```
syscall.c --> filesys.c --> directory.c --> inode.c --> cache.c --> block.c
                        \-> file.c ---------/
          \
           \-> FD_CONSOLE special case (input_getc/putbuf)  <-- Problem!
```

**Problems:**
1. Strong coupling - `file.c` and `directory.c` directly call `inode_*` functions
2. Console is a special case in syscalls, not a proper device

## Target Architecture

```
                         syscall.c
                             |
                             v
    +--------------------  vfs.c  --------------------+
    |                        |                        |
    v                        v                        v
pintos_fs.c              devfs.c                (future fs)
(disk files)          (/dev filesystem)
                             |
              +--------------+--------------+
              v              v              v
         chrdev.c       console_dev    serial_dev
      (char device       (tty)         (ttyS0)
        registry)
```

All I/O (files, directories, devices) goes through the unified VFS layer.

---

# Part 1: Core VFS Infrastructure

## New Files (VFS Core)

| File | Purpose |
|------|---------|
| `src/filesys/vfs.h` | VFS structs and public API |
| `src/filesys/vfs.c` | Core VFS implementation |
| `src/filesys/pintos_fs.h` | Pintos FS type declarations |
| `src/filesys/pintos_fs.c` | Pintos FS adapter (wraps existing inode.c) |

## Files to Modify (VFS Core)

| File | Changes |
|------|---------|
| `src/filesys/filesys.c` | Add VFS init, mount root |
| `src/filesys/filesys.h` | Export VFS types |
| `src/filesys/Makefile` | Add new source files |
| `src/userprog/syscall.c` | Use `vfs_*` API, remove FD_CONSOLE |
| `src/userprog/process.h` | Update fd_entry to use vfs_file/vfs_dir |
| `src/userprog/process.c` | Init fd 0/1/2 via device files |

---

## Core Data Structures

### vfs.h - Base Structures

```c
/* Filesystem type (one per fs implementation) */
struct vfs_fs_type {
    const char *name;                    /* "pintos", "devfs", etc. */
    struct vfs_superblock *(*mount)(struct block *device, void *data);
    void (*unmount)(struct vfs_superblock *sb);
    struct list_elem elem;
};

/* Superblock (one per mounted filesystem) */
struct vfs_superblock {
    struct vfs_fs_type *fs_type;
    struct block *device;
    const struct vfs_super_ops *s_ops;
    const struct vfs_inode_ops *i_ops;
    const struct vfs_file_ops *f_ops;
    struct vfs_inode *root;
    void *fs_private;
    struct list open_inodes;             /* Per-fs open inode list */
    struct lock open_inodes_lock;
};

/* Generic inode - WITH DEVICE SUPPORT */
struct vfs_inode {
    struct vfs_superblock *sb;
    uint32_t ino;                        /* Inode number */
    off_t length;
    int open_cnt;
    int deny_write_cnt;
    bool removed;
    struct lock lock;
    const struct vfs_inode_ops *i_ops;
    const struct vfs_file_ops *f_ops;
    void *private_data;                  /* FS-specific data */
    struct list_elem elem;

    /* Device support */
    uint16_t i_mode;                     /* S_IFREG, S_IFDIR, S_IFCHR, S_IFBLK */
    dev_t i_rdev;                        /* Device number (for device files) */
};

/* Generic file handle - WITH DEVICE SUPPORT */
struct vfs_file {
    struct vfs_inode *inode;
    off_t pos;
    bool deny_write;
    int ref_count;
    struct lock lock;

    /* Device support */
    struct cdev *cdev;                   /* Character device (if S_IFCHR) */
    void *cdev_ctx;                      /* Driver context from open() */
};

/* Generic directory handle */
struct vfs_dir {
    struct vfs_inode *inode;
    off_t pos;
};

/* File type constants */
#define S_IFMT   0xF000  /* File type mask */
#define S_IFREG  0x8000  /* Regular file */
#define S_IFDIR  0x4000  /* Directory */
#define S_IFCHR  0x2000  /* Character device */
#define S_IFBLK  0x6000  /* Block device */

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
```

### Operation Tables

```c
struct vfs_super_ops {
    struct vfs_inode *(*alloc_inode)(struct vfs_superblock *sb);
    void (*destroy_inode)(struct vfs_inode *inode);
    int (*read_inode)(struct vfs_inode *inode);
    int (*write_inode)(struct vfs_inode *inode);
    void (*delete_inode)(struct vfs_inode *inode);
    void (*sync_fs)(struct vfs_superblock *sb);
};

struct vfs_inode_ops {
    struct vfs_inode *(*lookup)(struct vfs_inode *dir, const char *name);
    int (*create)(struct vfs_inode *dir, const char *name, off_t size);
    int (*mkdir)(struct vfs_inode *dir, const char *name);
    int (*unlink)(struct vfs_inode *dir, const char *name);
    int (*rmdir)(struct vfs_inode *dir, const char *name);
    bool (*readdir)(struct vfs_inode *dir, off_t *pos, char *name, size_t size);
    off_t (*get_length)(struct vfs_inode *inode);
};

struct vfs_file_ops {
    off_t (*read)(struct vfs_inode *inode, void *buf, off_t size, off_t offset);
    off_t (*write)(struct vfs_inode *inode, const void *buf, off_t size, off_t offset);
    void (*deny_write)(struct vfs_inode *inode);
    void (*allow_write)(struct vfs_inode *inode);
};
```

---

# Part 2: Unified Device I/O Subsystem

## New Files (Device Layer)

| File | Purpose |
|------|---------|
| `src/devices/chrdev.h` | Character device structures and API |
| `src/devices/chrdev.c` | Character device registration/lookup |
| `src/devices/console_dev.c` | Console as character device |
| `src/devices/mem_dev.c` | /dev/null, /dev/zero |
| `src/devices/serial_dev.c` | Serial port as character device |
| `src/filesys/devfs.h` | devfs declarations |
| `src/filesys/devfs.c` | In-memory device filesystem |

## Device Number Type

```c
/* src/devices/chrdev.h */

typedef uint16_t dev_t;

#define MAJOR(dev)    ((unsigned)((dev) >> 8))
#define MINOR(dev)    ((unsigned)((dev) & 0xff))
#define MKDEV(ma,mi)  ((dev_t)(((ma) << 8) | (mi)))

/* Reserved major numbers (Linux-compatible) */
#define MEM_MAJOR     1   /* /dev/null (3), /dev/zero (5) */
#define TTY_MAJOR     4   /* /dev/console, /dev/tty* */
#define SERIAL_MAJOR  4   /* /dev/ttyS0 (minor 64+) */
```

## Character Device Operations

```c
/* src/devices/chrdev.h */

struct cdev_file_ops {
    /* Open device - allocate per-open context */
    int (*open)(struct cdev *cdev, unsigned minor, void **ctx);

    /* Close device - free per-open context */
    void (*close)(struct cdev *cdev, void *ctx);

    /* Read from device */
    off_t (*read)(struct cdev *cdev, void *ctx, void *buf, off_t size);

    /* Write to device */
    off_t (*write)(struct cdev *cdev, void *ctx, const void *buf, off_t size);

    /* Device-specific control */
    int (*ioctl)(struct cdev *cdev, void *ctx, unsigned cmd, unsigned long arg);
};

struct cdev {
    unsigned major;              /* Major device number */
    unsigned minor_base;         /* First minor number */
    unsigned minor_count;        /* Number of minors */
    const char *name;            /* Device name */
    const struct cdev_file_ops *ops;
    void *private_data;
    struct list_elem elem;
};

/* API */
void chrdev_init(void);
int chrdev_register(struct cdev *cdev);
void chrdev_unregister(struct cdev *cdev);
struct cdev *chrdev_find(unsigned major);
```

## devfs Structure

```c
/* src/filesys/devfs.h */

struct devfs_entry {
    char name[NAME_MAX + 1];     /* Entry name */
    uint16_t mode;               /* S_IFCHR or S_IFBLK */
    dev_t rdev;                  /* Device number */
    struct devfs_entry *parent;
    struct list children;        /* If directory */
    struct list_elem elem;
};

struct devfs_sb_info {
    struct devfs_entry *root;
    struct lock entries_lock;
    uint32_t next_ino;
};

/* API */
void devfs_init(void);
int devfs_mknod(const char *name, uint16_t mode, dev_t dev);
```

---

# Part 3: Implementation Phases

## Phase 1: VFS Infrastructure (~200 lines)

**Files:** `vfs.h`, `vfs.c`

1. Define all structs in `vfs.h` (including device fields)
2. Implement in `vfs.c`:
   - `vfs_init()` - Initialize global state
   - `vfs_register_fs_type()` / `vfs_find_fs_type()`
   - `vfs_mount()` / `vfs_unmount()`
   - `vfs_inode_open()` / `vfs_inode_close()` / `vfs_inode_reopen()`

## Phase 2: Path Resolution (~150 lines)

**Files:** `vfs.c`

1. Implement `vfs_get_start_dir()` - Get root or cwd
2. Implement `vfs_parse_path()` - Tokenize and traverse
3. Handle "." and ".." during traversal
4. Support both absolute and relative paths
5. **Mount point crossing** - resolve `/dev/...` to devfs

## Phase 3: Pintos FS Adapter (~400 lines)

**Files:** `pintos_fs.h`, `pintos_fs.c`

1. Define `struct pintos_inode_info` to hold `inode_disk`
2. Implement `pintos_super_ops`:
   - `alloc_inode` / `destroy_inode`
   - `read_inode` / `write_inode` / `delete_inode`
3. Implement `pintos_inode_ops`:
   - `lookup` - Search dir entries
   - `create` / `mkdir` / `unlink` / `rmdir`
   - `readdir`
4. Implement `pintos_file_ops`:
   - `read` / `write` - Wrap existing byte_to_sector logic
   - `deny_write` / `allow_write`
5. Register with `vfs_register_fs_type()`

## Phase 4: Character Device Layer (~200 lines)

**Files:** `chrdev.h`, `chrdev.c`

```c
/* chrdev.c */
static struct list chrdev_list;
static struct lock chrdev_lock;

void chrdev_init(void) {
    list_init(&chrdev_list);
    lock_init(&chrdev_lock);
}

int chrdev_register(struct cdev *cdev) {
    lock_acquire(&chrdev_lock);
    /* Check for major conflict, add to list */
    list_push_back(&chrdev_list, &cdev->elem);
    lock_release(&chrdev_lock);
    return 0;
}

struct cdev *chrdev_find(unsigned major) {
    lock_acquire(&chrdev_lock);
    /* Search list for matching major */
    lock_release(&chrdev_lock);
    return result;
}
```

## Phase 5: Console Device Driver (~150 lines)

**Files:** `console_dev.c`

Wraps existing `input_getc()` and `putbuf()`:

```c
static off_t console_read(struct cdev *cdev, void *ctx, void *buf, off_t size) {
    struct console_ctx *cctx = ctx;
    if (cctx->mode != CONSOLE_READ)
        return -1;

    char *cbuf = buf;
    for (off_t i = 0; i < size; i++)
        cbuf[i] = input_getc();
    return size;
}

static off_t console_write(struct cdev *cdev, void *ctx, const void *buf, off_t size) {
    struct console_ctx *cctx = ctx;
    if (cctx->mode != CONSOLE_WRITE)
        return -1;

    putbuf(buf, size);
    return size;
}

static struct cdev console_cdev = {
    .major = TTY_MAJOR,
    .minor_base = 0,
    .minor_count = 3,  /* 0=stdin, 1=stdout, 2=stderr */
    .name = "console",
    .ops = &console_ops,
};
```

## Phase 6: Memory Devices (~80 lines)

**Files:** `mem_dev.c`

```c
/* /dev/null - discard writes, EOF on read */
static off_t null_read(...) { return 0; }
static off_t null_write(...) { return size; }

/* /dev/zero - return zeros on read */
static off_t zero_read(...) { memset(buf, 0, size); return size; }

static struct cdev mem_cdev = {
    .major = MEM_MAJOR,
    .minor_base = 0,
    .minor_count = 16,
    .name = "mem",
    .ops = &mem_ops,
};
```

## Phase 7: devfs Filesystem (~300 lines)

**Files:** `devfs.h`, `devfs.c`

In-memory filesystem mounted at `/dev/`:

```c
void devfs_init(void) {
    vfs_register_fs_type(&devfs_fs_type);
    vfs_mount("devfs", "/dev", NULL, NULL);

    /* Create standard device nodes */
    devfs_mknod("null",    S_IFCHR, MKDEV(MEM_MAJOR, 3));
    devfs_mknod("zero",    S_IFCHR, MKDEV(MEM_MAJOR, 5));
    devfs_mknod("console", S_IFCHR, MKDEV(TTY_MAJOR, 0));
    devfs_mknod("stdin",   S_IFCHR, MKDEV(TTY_MAJOR, 0));
    devfs_mknod("stdout",  S_IFCHR, MKDEV(TTY_MAJOR, 1));
    devfs_mknod("stderr",  S_IFCHR, MKDEV(TTY_MAJOR, 2));
    devfs_mknod("ttyS0",   S_IFCHR, MKDEV(SERIAL_MAJOR, 64));
}
```

## Phase 8: VFS Device Integration (~100 lines)

**Files:** `vfs.c`

Add device dispatch to VFS operations:

```c
/* vfs_open() - handle device files */
struct vfs_file *vfs_open(const char *path) {
    struct vfs_inode *inode = vfs_resolve_path(path);
    struct vfs_file *file = malloc(sizeof(struct vfs_file));
    file->inode = inode;
    file->cdev = NULL;
    file->cdev_ctx = NULL;

    /* Device file? */
    if (S_ISCHR(inode->i_mode)) {
        struct cdev *cdev = chrdev_find(MAJOR(inode->i_rdev));
        if (cdev && cdev->ops->open) {
            cdev->ops->open(cdev, MINOR(inode->i_rdev), &file->cdev_ctx);
        }
        file->cdev = cdev;
    }
    return file;
}

/* vfs_file_read() - dispatch to device */
off_t vfs_file_read(struct vfs_file *file, void *buf, off_t size) {
    if (file->cdev != NULL)
        return file->cdev->ops->read(file->cdev, file->cdev_ctx, buf, size);
    return file->inode->f_ops->read(file->inode, buf, size, file->pos);
}

/* vfs_file_write() - dispatch to device */
off_t vfs_file_write(struct vfs_file *file, const void *buf, off_t size) {
    if (file->cdev != NULL)
        return file->cdev->ops->write(file->cdev, file->cdev_ctx, buf, size);
    return file->inode->f_ops->write(file->inode, buf, size, file->pos);
}
```

## Phase 9: Syscall Migration (~150 lines)

**Files:** `syscall.c`, `process.h`, `process.c`

Remove `FD_CONSOLE` special case:

```c
/* Before */
enum fd_type { FD_NONE, FD_FILE, FD_DIR, FD_CONSOLE };

/* After - FD_CONSOLE removed! */
enum fd_type { FD_NONE, FD_FILE, FD_DIR };

struct fd_entry {
    enum fd_type type;
    union {
        struct vfs_file *vfs_file;
        struct vfs_dir *vfs_dir;
    };
};
```

Initialize stdin/stdout/stderr via device files:

```c
/* process.c - setup_fd_table() */
static bool setup_fd_table(struct process *pcb) {
    pcb->fd_table[STDIN_FILENO].type = FD_FILE;
    pcb->fd_table[STDIN_FILENO].vfs_file = vfs_open("/dev/stdin");

    pcb->fd_table[STDOUT_FILENO].type = FD_FILE;
    pcb->fd_table[STDOUT_FILENO].vfs_file = vfs_open("/dev/stdout");

    pcb->fd_table[STDERR_FILENO].type = FD_FILE;
    pcb->fd_table[STDERR_FILENO].vfs_file = vfs_open("/dev/stderr");

    return true;
}
```

## Phase 10: Mount Table (~100 lines)

**Files:** `vfs.c`, `vfs.h`

1. Implement mount point resolution in `vfs_parse_path()`
2. Longest-prefix matching for path -> mount resolution
3. Support `/dev/...` crossing to devfs mount

---

# Part 4: Concurrency Strategy

## Lock Hierarchy (6 levels)

```
Level 1: vfs_global_lock
    +-- Protects: registered_fs_types list, mount_table list
    +-- Acquired: fs type registration, mount/unmount

Level 2: chrdev_lock
    +-- Protects: chrdev_list (registered character devices)
    +-- Acquired: chrdev_register(), chrdev_find()

Level 3: superblock->open_inodes_lock (per-mounted-fs)
    +-- Protects: superblock's open_inodes list
    +-- Acquired: vfs_inode_open(), vfs_inode_close()

Level 4: vfs_inode->lock (per-inode)
    +-- Protects: open_cnt, deny_write_cnt, removed, length
    +-- Acquired: inode metadata operations

Level 5: vfs_file->lock (per-file-handle)
    +-- Protects: ref_count
    +-- Acquired: file_dup(), file_close()

Level 6: cache locks (existing)
    +-- Already implemented in cache.c
```

## Lock Ordering Rules

```
ALWAYS acquire in this order (never reverse):

vfs_global_lock
    -> chrdev_lock
        -> superblock->open_inodes_lock
            -> vfs_inode->lock
                -> vfs_file->lock
                    -> cache internal locks
```

---

# Part 5: Initialization Order

```c
/* threads/init.c */
void main(void) {
    // ... early init ...

    /* Phase 4: Character device subsystem */
    chrdev_init();

    /* Phases 5-6: Register device drivers */
    mem_dev_init();       /* /dev/null, /dev/zero */
    console_dev_init();   /* /dev/console, stdin, stdout, stderr */
    serial_dev_init();    /* /dev/ttyS0 */

    /* Phases 1-3: VFS and pintos_fs */
    filesys_init(format);

    /* Phase 7: Mount devfs */
    devfs_init();

    // ... rest of init ...
}
```

---

# Part 6: Summary

## All New Files

| File | Lines | Purpose |
|------|-------|---------|
| `src/filesys/vfs.h` | ~120 | VFS structures and API |
| `src/filesys/vfs.c` | ~400 | VFS implementation |
| `src/filesys/pintos_fs.h` | ~30 | Pintos FS declarations |
| `src/filesys/pintos_fs.c` | ~400 | Pintos FS adapter |
| `src/filesys/devfs.h` | ~50 | devfs declarations |
| `src/filesys/devfs.c` | ~250 | devfs implementation |
| `src/devices/chrdev.h` | ~60 | Character device API |
| `src/devices/chrdev.c` | ~140 | Character device registry |
| `src/devices/console_dev.c` | ~100 | Console character device |
| `src/devices/mem_dev.c` | ~80 | /dev/null, /dev/zero |
| `src/devices/serial_dev.c` | ~80 | /dev/ttyS0 |

## Estimated Effort

| Phase | Lines | Complexity |
|-------|-------|------------|
| Phase 1: VFS Infrastructure | ~200 | Medium |
| Phase 2: Path Resolution | ~150 | Medium |
| Phase 3: Pintos Adapter | ~400 | High |
| Phase 4: Character Device Layer | ~200 | Medium |
| Phase 5: Console Device | ~150 | Low |
| Phase 6: Memory Devices | ~80 | Low |
| Phase 7: devfs | ~300 | High |
| Phase 8: VFS Device Integration | ~100 | Medium |
| Phase 9: Syscall Migration | ~150 | Medium |
| Phase 10: Mount Table | ~100 | Medium |
| **Total** | **~1830** | |

---

# Part 7: Testing Strategy

## Unit Tests

1. `chrdev_register()` / `chrdev_find()` - device registration
2. Console read/write via `/dev/console`
3. `/dev/null` - returns EOF on read, accepts all writes
4. `/dev/zero` - returns zeros on read

## Integration Tests

1. All existing `tests/userprog/*` must pass
2. All existing `tests/filesys/*` must pass
3. `open("/dev/null")` and write to it
4. `open("/dev/zero")` and read zeros

## Manual Testing

```c
/* User program test */
int fd = open("/dev/null");
write(fd, "hello", 5);  /* Should succeed, discard data */
close(fd);

fd = open("/dev/zero");
char buf[10];
read(fd, buf, 10);  /* Should return 10 zeros */
close(fd);
```

---

# Part 8: Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Device number type | `uint16_t` | Simple, sufficient for educational OS |
| devfs storage | In-memory | No persistence needed for /dev |
| Console minor scheme | 0=stdin, 1=stdout, 2=stderr | Matches Unix tradition |
| VFS + device integration | Unified vfs_file | Single abstraction for files and devices |
| cdev ops signature | Separate from vfs_file_ops | Devices don't need seek/tell |
| FD_CONSOLE removal | Yes | Clean unified model |

---

# Part 9: Critical Paths

1. `vfs_parse_path()` must correctly cross mount points (/ to /dev)
2. `chrdev_find()` must be fast (called on every device open)
3. Console minor number dispatch (0 vs 1/2) for read/write permissions
4. Reference counting across VFS inode/file and device contexts
5. WAL integration via cache layer should work transparently
