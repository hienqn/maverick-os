# VFS (Virtual Filesystem Switch) Implementation Plan

## Overview

Add a VFS abstraction layer to Pintos that standardizes the file interface and allows pluggable filesystem implementations.

## Current Architecture

```
syscall.c --> filesys.c --> directory.c --> inode.c --> cache.c --> block.c
                        \-> file.c ---------/
```

**Problem:** Strong coupling - `file.c` and `directory.c` directly call `inode_*` functions with Pintos-specific assumptions.

## Target Architecture

```
syscall.c --> filesys.c --> vfs.c -----------------> cache.c --> block.c
                              |
                    +---------+---------+
                    v                   v
              pintos_fs.c          (future fs types)
              (wraps inode.c)
```

---

## New Files

| File | Purpose |
|------|---------|
| `src/filesys/vfs.h` | VFS structs and public API |
| `src/filesys/vfs.c` | Core VFS implementation |
| `src/filesys/pintos_fs.h` | Pintos FS type declarations |
| `src/filesys/pintos_fs.c` | Pintos FS adapter (wraps existing inode.c) |

## Files to Modify

| File | Changes |
|------|---------|
| `src/filesys/filesys.c` | Add VFS init, mount root, remove legacy wrappers |
| `src/filesys/filesys.h` | Export VFS types, remove legacy struct references |
| `src/filesys/Makefile` | Add new source files |
| `src/userprog/syscall.c` | **Full migration**: Use `vfs_*` API directly |
| `src/userprog/process.h` | Update CWD to `vfs_inode*`, fd_entry to use `vfs_file*`/`vfs_dir*` |
| `src/userprog/process.c` | Update fork/exec for VFS file descriptor handling |

## Files Unchanged

- `src/filesys/cache.c` - Already abstract
- `src/filesys/inode.c` - Kept as-is, wrapped by pintos_fs.c
- `src/filesys/file.c` - Deprecated, functionality moves to VFS
- `src/filesys/directory.c` - Deprecated, functionality moves to pintos_fs.c

---

## Core Data Structures

### vfs.h

```c
/* Filesystem type (one per fs implementation) */
struct vfs_fs_type {
    const char *name;                    /* "pintos", "fat32", etc. */
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

/* Generic inode */
struct vfs_inode {
    struct vfs_superblock *sb;
    uint32_t ino;                        /* Sector number for Pintos */
    off_t length;
    bool is_dir;
    bool removed;
    int open_cnt;
    int deny_write_cnt;
    struct lock lock;
    const struct vfs_inode_ops *i_ops;
    const struct vfs_file_ops *f_ops;
    void *private_data;                  /* FS-specific (inode_disk for Pintos) */
    struct list_elem elem;
};

/* Generic file handle */
struct vfs_file {
    struct vfs_inode *inode;
    off_t pos;
    bool deny_write;
    int ref_count;
    struct lock lock;
};

/* Generic directory handle */
struct vfs_dir {
    struct vfs_inode *inode;
    off_t pos;
};
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
    bool (*is_directory)(struct vfs_inode *inode);
    bool (*is_removed)(struct vfs_inode *inode);
};

struct vfs_file_ops {
    off_t (*read)(struct vfs_inode *inode, void *buf, off_t size, off_t offset);
    off_t (*write)(struct vfs_inode *inode, const void *buf, off_t size, off_t offset);
    void (*deny_write)(struct vfs_inode *inode);
    void (*allow_write)(struct vfs_inode *inode);
};
```

---

## Implementation Phases

### Phase 1: VFS Infrastructure (~200 lines)

**Files:** `vfs.h`, `vfs.c`

1. Define all structs in `vfs.h`
2. Implement in `vfs.c`:
   - `vfs_init()` - Initialize global state
   - `vfs_register_fs_type()` / `vfs_find_fs_type()`
   - `vfs_mount()` / `vfs_unmount()`
   - `vfs_inode_open()` / `vfs_inode_close()` / `vfs_inode_reopen()`

### Phase 2: Path Resolution (~150 lines)

**Files:** `vfs.c`

1. Implement `vfs_get_start_dir()` - Get root or cwd
2. Implement `vfs_parse_path()` - Tokenize and traverse
3. Handle "." and ".." during traversal
4. Support both absolute and relative paths

### Phase 3: Pintos FS Adapter (~400 lines)

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
5. Implement `pintos_mount()` / `pintos_unmount()`
6. Register with `vfs_register_fs_type()`

### Phase 4: High-Level VFS API (~150 lines)

**Files:** `vfs.c`

1. Implement file operations:
   - `vfs_create()` / `vfs_open()` / `vfs_remove()`
   - `vfs_file_read()` / `vfs_file_write()`
   - `vfs_file_seek()` / `vfs_file_tell()` / `vfs_file_length()`
   - `vfs_file_close()`
2. Implement directory operations:
   - `vfs_mkdir()` / `vfs_rmdir()`
   - `vfs_dir_open()` / `vfs_dir_close()` / `vfs_readdir()`
   - `vfs_chdir()`

### Phase 5: Integration (~100 lines)

**Files:** `filesys.c`, `filesys.h`, `Makefile`

1. Modify `filesys_init()`:
   ```c
   vfs_init();
   pintos_fs_init();
   // ... existing format/free_map code ...
   vfs_mount("pintos", "/", fs_device, NULL);
   ```
2. Update `filesys.h` to export VFS types
3. Update Makefile to include new sources

### Phase 6: Full Syscall Migration (~200 lines)

**Files:** `syscall.c`, `process.h`, `process.c`

1. Update `fd_entry` struct:
   ```c
   struct fd_entry {
       enum fd_type type;
       union {
           struct vfs_file *file;   /* Was: struct file* */
           struct vfs_dir *dir;     /* Was: struct dir* */
       };
   };
   ```
2. Update CWD handling:
   ```c
   struct vfs_inode *cwd_inode;  /* Was: struct dir *cwd */
   ```
3. Migrate all syscall handlers to use VFS API:
   - `SYS_OPEN` -> `vfs_open()`
   - `SYS_CREATE` -> `vfs_create()`
   - `SYS_READ` -> `vfs_file_read()`
   - `SYS_WRITE` -> `vfs_file_write()`
   - `SYS_MKDIR` -> `vfs_mkdir()`
   - `SYS_CHDIR` -> `vfs_chdir()`
   - etc.
4. Update fork/exec to copy VFS file references
5. Remove includes of deprecated `file.h`, `directory.h`

### Phase 7: Mount Table Implementation (~100 lines)

**Files:** `vfs.c`, `vfs.h`

1. Implement mount point resolution in `vfs_parse_path()`:
   ```c
   struct vfs_mount *vfs_find_mount(const char *path);
   ```
2. Support mounting at arbitrary paths (e.g., `/mnt/usb`)
3. Longest-prefix matching for path -> mount resolution
4. Add `vfs_mount()` / `vfs_unmount()` with proper cleanup

---

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Open inode list | Per-superblock | Allows multiple mounted filesystems |
| Operation dispatch | Function pointers | Standard VFS pattern, minimal overhead |
| Backward compatibility | **Full migration** | Update syscall.c to use VFS directly, cleaner long-term |
| Path resolution | In VFS layer | Generic across all fs types |
| Mount table | **Full support** | Multiple mount points (/, /mnt/*, etc.) |
| Scope | **Complete implementation** | Full VFS + Pintos adapter (~1000+ lines) |

---

## Testing Strategy

1. **Unit tests:** New tests for VFS operations
2. **Regression tests:** All existing `filesys/base/*` and `filesys/extended/*` tests must pass
3. **WAL integration:** Verify WAL still works through VFS layer

---

## Estimated Effort

| Phase | Lines | Complexity |
|-------|-------|------------|
| Phase 1: VFS Infrastructure | ~200 | Medium |
| Phase 2: Path Resolution | ~150 | Medium |
| Phase 3: Pintos Adapter | ~400 | High |
| Phase 4: High-Level API | ~150 | Medium |
| Phase 5: Integration | ~100 | Low |
| Phase 6: Syscall Migration | ~200 | Medium |
| Phase 7: Mount Table | ~100 | Medium |
| **Total** | **~1300** | |

---

## Concurrency Strategy

### Current Locking (in inode.c)

```
Level 1: open_inodes_lock (global)
    +-- Protects: list traversal, insertion, removal

Level 2: inode->lock (per-inode)
    +-- Protects: open_cnt, deny_write_cnt, removed, data (inode_disk)

Lock ordering: open_inodes_lock -> inode->lock (never reverse)
```

### VFS Lock Hierarchy (5 levels)

```
Level 1: vfs_global_lock
    +-- Protects: registered_fs_types list, mount_table list
    +-- Acquired: fs type registration, mount/unmount operations
    +-- Granularity: Coarse, rarely held

Level 2: superblock->open_inodes_lock (per-mounted-fs)
    +-- Protects: superblock's open_inodes list
    +-- Acquired: vfs_inode_open(), vfs_inode_close()
    +-- Mirrors current open_inodes_lock but per-fs

Level 3: vfs_inode->lock (per-inode)
    +-- Protects: open_cnt, deny_write_cnt, removed, length, private_data
    +-- Acquired: inode metadata operations, file extension
    +-- Mirrors current inode->lock

Level 4: vfs_file->lock (per-file-handle)
    +-- Protects: ref_count only
    +-- Acquired: file_dup(), file_close()
    +-- Lightweight, rarely contended

Level 5: cache locks (existing)
    +-- Already implemented in cache.c
    +-- No changes needed
```

### Lock Ordering Rules

```
ALWAYS acquire in this order (never reverse):

vfs_global_lock
    -> superblock->open_inodes_lock
        -> vfs_inode->lock
            -> vfs_file->lock
                -> cache internal locks
```

### Concurrency Scenarios

| Operation | Locks Acquired | Order |
|-----------|---------------|-------|
| `vfs_mount()` | vfs_global_lock | 1 |
| `vfs_inode_open()` | sb->open_inodes_lock -> inode->lock | 2->3 |
| `vfs_inode_close()` | sb->open_inodes_lock -> inode->lock | 2->3 |
| `vfs_file_read()` | (none - reads inode->length atomically) | - |
| `vfs_file_write()` | inode->lock (for extension check) | 3 |
| `vfs_file_dup()` | file->lock | 4 |
| `vfs_parse_path()` | Multiple inode->locks (released before next) | 3 (serial) |

### Path Resolution Concurrency

**Challenge:** Traversing `/a/b/c` requires looking up multiple directories. Another thread could remove `/a/b` mid-traversal.

**Strategy: Reference counting with early validation**

```c
bool vfs_parse_path(const char *path, ...) {
    struct vfs_inode *cur = vfs_get_start_dir(path);  // +1 ref

    for each component:
        // Hold reference to cur while looking up next
        struct vfs_inode *next = cur->i_ops->lookup(cur, name);  // +1 ref

        if (next == NULL || next->removed) {
            vfs_inode_close(cur);   // -1 ref
            vfs_inode_close(next);  // -1 ref if not NULL
            return false;
        }

        vfs_inode_close(cur);  // -1 ref (safe, next holds ref)
        cur = next;

    *result = cur;  // Transfer ownership to caller
    return true;
}
```

**Key invariant:** Never release a directory's reference until the next directory is opened and validated.

---

## Critical Paths

1. `pintos_fs.c` must correctly wrap `byte_to_sector()` and `inode_extend()` logic
2. `vfs_parse_path()` must handle edge cases: "/", ".", "..", trailing slashes
3. Reference counting in `vfs_inode_open/close` must match existing semantics
4. WAL integration via cache layer should work transparently
