# Implementation Plan: Hard Links and Symbolic Links for Pintos

## Summary

| Feature | Difficulty | Effort | New Syscalls |
|---------|------------|--------|--------------|
| Hard Links | Moderate | ~100-150 LOC | `link()` |
| Symbolic Links | Higher | ~300-400 LOC | `symlink()`, `readlink()` |

---

## Phase 1: Foundation - Inode Type System & Link Count

### 1.1 Modify `inode_disk` Structure
**File:** `src/filesys/inode.c` (lines 29-37)

```c
/* Add constants */
#define INODE_TYPE_FILE    0
#define INODE_TYPE_DIR     1
#define INODE_TYPE_SYMLINK 2

struct inode_disk {
  block_sector_t direct[DIRECT_BLOCK_COUNT];
  block_sector_t indirect;
  block_sector_t doubly_indirect;
  off_t length;
  uint32_t type;    /* Replaces is_dir: 0=file, 1=dir, 2=symlink */
  uint32_t nlink;   /* NEW: hard link count */
  unsigned magic;
  uint32_t unused[110];  /* Reduced by 1 */
};
```

### 1.2 Add Link Count Functions
**File:** `src/filesys/inode.c` and `inode.h`

```c
/* In inode.h - add declarations */
bool inode_is_symlink(struct inode* inode);
bool inode_is_file(struct inode* inode);
uint32_t inode_get_nlink(struct inode* inode);
void inode_inc_nlink(struct inode* inode);
void inode_dec_nlink(struct inode* inode);
```

### 1.3 Update Deletion Logic
**File:** `src/filesys/inode.c` - `inode_close()`

Only deallocate when `open_cnt == 0 && removed && nlink == 0`

---

## Phase 2: Hard Links

### 2.1 Add Syscall Number
**File:** `src/lib/syscall-nr.h`
```c
SYS_LINK,  /* Create hard link */
```

### 2.2 Add User-Space Wrapper
**Files:** `src/lib/user/syscall.h` and `syscall.c`
```c
bool link(const char* oldpath, const char* newpath);
```

### 2.3 Implement `filesys_link()`
**File:** `src/filesys/filesys.c`

Logic:
1. Parse both paths
2. Look up source inode
3. Verify not a directory (POSIX restriction)
4. Verify destination doesn't exist
5. Add directory entry pointing to same sector
6. Increment `nlink`

### 2.4 Update `dir_remove()`
**File:** `src/filesys/directory.c` (lines 245-275)

Change from:
- Call `inode_remove()` unconditionally

To:
- Decrement `nlink`
- Only call `inode_remove()` when `nlink == 0`

### 2.5 Add Syscall Handler
**File:** `src/userprog/syscall.c`
```c
case SYS_LINK:
  f->eax = filesys_link(oldpath, newpath);
```

---

## Phase 3: Symbolic Links

### 3.1 Add Syscall Numbers
**File:** `src/lib/syscall-nr.h`
```c
SYS_SYMLINK,   /* Create symbolic link */
SYS_READLINK,  /* Read symlink target */
```

### 3.2 Add User-Space Wrappers
**Files:** `src/lib/user/syscall.h` and `syscall.c`
```c
bool symlink(const char* target, const char* linkpath);
int readlink(const char* path, char* buf, size_t size);
```

### 3.3 Implement Symlink Inode Creation
**File:** `src/filesys/inode.c`
```c
bool inode_create_symlink(block_sector_t sector, const char* target);
```
- Create inode with `type = INODE_TYPE_SYMLINK`
- Store target path in data block

### 3.4 Implement `filesys_symlink()`
**File:** `src/filesys/filesys.c`

Logic:
1. Parse link path
2. Allocate new inode sector
3. Create symlink inode with target stored as content
4. Add directory entry

### 3.5 Implement `filesys_readlink()`
**File:** `src/filesys/filesys.c`

Logic:
1. Parse path WITHOUT following final symlink
2. Verify inode is symlink type
3. Read and return target path from inode data

### 3.6 Modify Path Resolution for Symlinks
**File:** `src/filesys/filesys.c` - `parse_path()`

Add:
```c
#define MAX_SYMLINK_DEPTH 20  /* Prevent infinite loops */
```

Changes:
1. Add depth counter to track symlink traversals
2. When encountering symlink during traversal:
   - Read target path from symlink content
   - Recursively resolve (increment depth)
   - Return ELOOP error if depth exceeded
3. Handle relative vs absolute symlink targets
4. Add `parse_path_no_follow()` for `readlink()`

### 3.7 Add Syscall Handlers
**File:** `src/userprog/syscall.c`

---

## Files to Modify (Summary)

| File | Changes |
|------|---------|
| `src/filesys/inode.h` | Add type constants, function declarations |
| `src/filesys/inode.c` | Modify `inode_disk`, add nlink functions, update deletion |
| `src/filesys/filesys.h` | Add `filesys_link`, `filesys_symlink`, `filesys_readlink` |
| `src/filesys/filesys.c` | Implement link functions, modify `parse_path()` |
| `src/filesys/directory.c` | Update `dir_remove()` for link count |
| `src/lib/syscall-nr.h` | Add `SYS_LINK`, `SYS_SYMLINK`, `SYS_READLINK` |
| `src/lib/user/syscall.h` | Add user-space function declarations |
| `src/lib/user/syscall.c` | Add user-space function implementations |
| `src/userprog/syscall.c` | Add syscall handlers |

---

## Implementation Order

1. **Phase 1** (Foundation) - Do this first
   - Modify `inode_disk` structure
   - Add type/nlink helper functions
   - Update `inode_is_dir()` to use new `type` field
   - Modify deletion logic

2. **Phase 2** (Hard Links) - Build on Phase 1
   - Simpler to implement
   - Validates the nlink mechanism works

3. **Phase 3** (Symbolic Links) - Build on Phase 2
   - More complex path resolution
   - Requires loop detection

---

## Key Edge Cases to Handle

### Hard Links
- Cannot link to directories
- Cannot link to removed inodes
- Cannot create link over existing file
- Both names must resolve to same inode number

### Symbolic Links
- Dangling symlinks (target doesn't exist) - allowed
- Symlink loops - return error (ELOOP)
- Relative symlinks - resolve relative to symlink's parent directory
- Symlinks to directories - should work transparently
- `readlink()` on non-symlink - return error
