/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                          FILESYSTEM MODULE                                ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  This is the top-level filesystem interface, providing high-level        ║
 * ║  operations that user programs interact with through system calls.       ║
 * ║                                                                          ║
 * ║  ARCHITECTURE OVERVIEW:                                                  ║
 * ║  ──────────────────────                                                  ║
 * ║                                                                          ║
 * ║    ┌─────────────────────────────────────────────────────────────────┐   ║
 * ║    │                      syscall.c (User Interface)                 │   ║
 * ║    └─────────────────────────────┬───────────────────────────────────┘   ║
 * ║                                  │                                       ║
 * ║                                  ▼                                       ║
 * ║    ┌─────────────────────────────────────────────────────────────────┐   ║
 * ║    │  filesys.c (THIS FILE) - Path resolution, high-level ops        │   ║
 * ║    │  ─────────────────────────────────────────────────────────────  │   ║
 * ║    │  • filesys_create/open/remove - File operations                 │   ║
 * ║    │  • filesys_mkdir/chdir - Directory operations                   │   ║
 * ║    │  • parse_path - Resolves "/foo/bar" to parent dir + name        │   ║
 * ║    └──────────────────────┬──────────────────┬───────────────────────┘   ║
 * ║                           │                  │                           ║
 * ║              ┌────────────┴──────┐    ┌──────┴────────────┐              ║
 * ║              ▼                   ▼    ▼                   ▼              ║
 * ║    ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐     ║
 * ║    │   directory.c   │    │     file.c      │    │   free-map.c   │     ║
 * ║    │  (dir entries)  │    │  (position,     │    │ (sector alloc) │     ║
 * ║    │                 │    │   ref counting) │    │                │     ║
 * ║    └────────┬────────┘    └────────┬────────┘    └────────────────┘     ║
 * ║             │                      │                                     ║
 * ║             └──────────┬───────────┘                                     ║
 * ║                        ▼                                                 ║
 * ║    ┌─────────────────────────────────────────────────────────────────┐   ║
 * ║    │                    inode.c (Block management)                   │   ║
 * ║    │  • Indexed allocation (direct/indirect/doubly-indirect)         │   ║
 * ║    │  • File extension, per-inode synchronization                    │   ║
 * ║    └─────────────────────────────┬───────────────────────────────────┘   ║
 * ║                                  ▼                                       ║
 * ║    ┌─────────────────────────────────────────────────────────────────┐   ║
 * ║    │                    cache.c (Buffer cache)                       │   ║
 * ║    │  • 64-entry LRU cache, write-back policy                        │   ║
 * ║    │  • Read-ahead prefetching, periodic flush                       │   ║
 * ║    └─────────────────────────────┬───────────────────────────────────┘   ║
 * ║                                  ▼                                       ║
 * ║    ┌─────────────────────────────────────────────────────────────────┐   ║
 * ║    │                    block.c (Disk driver)                        │   ║
 * ║    └─────────────────────────────────────────────────────────────────┘   ║
 * ║                                                                          ║
 * ║  FILESYS vs FILE MODULE:                                                 ║
 * ║  ───────────────────────                                                 ║
 * ║                                                                          ║
 * ║  filesys.c:                        file.c:                               ║
 * ║  • Works with PATHS (strings)      • Works with open FILE structs        ║
 * ║  • Handles path resolution         • Tracks file position (seek/tell)    ║
 * ║  • Creates/removes/opens files     • Manages reference counting          ║
 * ║  • Directory traversal             • Read/write at current position      ║
 * ║  • Stateless operations            • Stateful (per-open-file state)      ║
 * ║                                                                          ║
 * ║  PATH RESOLUTION:                                                        ║
 * ║  ────────────────                                                        ║
 * ║  parse_path("/foo/bar/baz.txt") returns:                                 ║
 * ║    • parent_dir → directory object for /foo/bar                          ║
 * ║    • final_name → "baz.txt"                                              ║
 * ║                                                                          ║
 * ║  Handles: absolute paths (/...), relative paths, ".", ".."              ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/wal.h"
#include "threads/thread.h"
#include "threads/malloc.h"

/* Partition that contains the file system. */
struct block* fs_device;

/* Maximum number of path components (e.g., /a/b/c/d has 4 components). */
#define MAX_PATH_COMPONENTS 64

static void do_format(void);

/* Returns the starting directory for path resolution.
   If path is absolute (starts with '/'), returns root directory.
   Otherwise, returns the current thread's working directory.
   Returns NULL if the directory cannot be opened or if the CWD has been removed. */
struct dir* get_start_dir(const char* path) {
  if (path == NULL || path[0] == '\0')
    return NULL;

  if (path[0] == '/') {
    /* Absolute path - start from root */
    return dir_open_root();
  } else {
    /* Relative path - start from current working directory */
#ifdef USERPROG
    struct thread* cur = thread_current();
    if (cur->cwd != NULL) {
      /* Check if the CWD has been removed - if so, fail path resolution */
      struct inode* cwd_inode = dir_get_inode(cur->cwd);
      if (inode_is_removed(cwd_inode)) {
        return NULL;
      }
      return dir_reopen(cur->cwd);
    }
#endif
    /* Fall back to root if no cwd is set */
    return dir_open_root();
  }
}

/* Parses a path string and returns the parent directory and final component.
   
   Input: path string like "/foo/bar/file.txt" or "subdir/file.txt"
   Output: 
   - parent_dir: opened directory containing the final component (caller must close)
   - final_name: buffer to store the final component name (must be at least NAME_MAX + 1 bytes)
   
   Returns true on success, false on failure.
   
   Handles:
   - Absolute paths (starting with /)
   - Relative paths (using current thread's cwd)
   - "." (current directory)
   - ".." (parent directory)
   - Verifies intermediate components are directories
   
   Special cases:
   - "/" returns root directory with empty final_name
   - Empty path returns false
   - Path with only "." or ".." is handled correctly */
bool parse_path(const char* path, struct dir** parent_dir, char* final_name) {
  *parent_dir = NULL;
  final_name[0] = '\0';

  if (path == NULL || path[0] == '\0')
    return false;

  /* Make a copy of path since we'll modify it */
  size_t path_len = strlen(path);
  char* path_copy = malloc(path_len + 1);
  if (path_copy == NULL)
    return false;
  strlcpy(path_copy, path, path_len + 1);

  /* Get starting directory */
  struct dir* cur_dir = get_start_dir(path);
  if (cur_dir == NULL) {
    free(path_copy);
    return false;
  }

  /* Handle root directory special case */
  if (strcmp(path, "/") == 0) {
    *parent_dir = cur_dir;
    final_name[0] = '\0';
    free(path_copy);
    return true;
  }

  /* First pass: count components and collect them */
  char* components[MAX_PATH_COMPONENTS];
  int num_components = 0;

  char* token;
  char* save_ptr;
  char* parse_start = path_copy;

  /* Skip leading slashes */
  while (*parse_start == '/')
    parse_start++;

  for (token = strtok_r(parse_start, "/", &save_ptr); token != NULL;
       token = strtok_r(NULL, "/", &save_ptr)) {
    if (num_components >= MAX_PATH_COMPONENTS) {
      dir_close(cur_dir);
      free(path_copy);
      return false;
    }
    components[num_components++] = token;
  }

  /* Handle empty path after removing slashes (e.g., "///") */
  if (num_components == 0) {
    *parent_dir = cur_dir;
    final_name[0] = '\0';
    free(path_copy);
    return true;
  }

  /* Traverse all but the last component */
  for (int i = 0; i < num_components - 1; i++) {
    char* comp = components[i];

    /* Handle "." - stay in current directory */
    if (strcmp(comp, ".") == 0) {
      continue;
    }

    /* Handle ".." - go to parent directory */
    if (strcmp(comp, "..") == 0) {
      struct inode* parent_inode = NULL;
      if (dir_lookup(cur_dir, "..", &parent_inode)) {
        struct dir* parent = dir_open(parent_inode);
        if (parent == NULL) {
          dir_close(cur_dir);
          free(path_copy);
          return false;
        }
        dir_close(cur_dir);
        cur_dir = parent;
      }
      /* If ".." lookup fails (e.g., at root), stay in current directory */
      continue;
    }

    /* Regular directory component - look it up */
    struct inode* inode = NULL;
    if (!dir_lookup(cur_dir, comp, &inode)) {
      /* Directory component not found */
      dir_close(cur_dir);
      free(path_copy);
      return false;
    }

    /* Verify it's a directory */
    if (!inode_is_dir(inode)) {
      inode_close(inode);
      dir_close(cur_dir);
      free(path_copy);
      return false;
    }

    /* Open the next directory */
    struct dir* next_dir = dir_open(inode);
    if (next_dir == NULL) {
      dir_close(cur_dir);
      free(path_copy);
      return false;
    }

    dir_close(cur_dir);
    cur_dir = next_dir;
  }

  /* Handle the final component */
  char* final_comp = components[num_components - 1];

  /* Check length */
  if (strlen(final_comp) > NAME_MAX) {
    dir_close(cur_dir);
    free(path_copy);
    return false;
  }

  /* Handle special cases for final component */
  if (strcmp(final_comp, ".") == 0) {
    /* "." as final component - return current dir with "." as name */
    strlcpy(final_name, ".", NAME_MAX + 1);
    *parent_dir = cur_dir;
    free(path_copy);
    return true;
  }

  if (strcmp(final_comp, "..") == 0) {
    /* ".." as final component - return parent dir with ".." as name */
    strlcpy(final_name, "..", NAME_MAX + 1);
    *parent_dir = cur_dir;
    free(path_copy);
    return true;
  }

  /* Regular final component */
  strlcpy(final_name, final_comp, NAME_MAX + 1);
  *parent_dir = cur_dir;
  free(path_copy);
  return true;
}

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  cache_init();
  free_map_init();  /* Initialize free map first (marks WAL sectors as reserved) */
  wal_init(format); /* Initialize WAL subsystem (may trigger recovery if crash detected) */
  inode_init();

  if (format)
    do_format();

  free_map_open();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) {
  cache_print_stats();
  cache_shutdown();
  wal_shutdown(); /* Flush WAL and write clean shutdown marker */
  free_map_close();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char* name, off_t initial_size) {
  struct dir* parent_dir = NULL;
  char final_name[NAME_MAX + 1];

  /* Parse the path to get parent directory and file name */
  if (!parse_path(name, &parent_dir, final_name))
    return false;

  /* Cannot create file with empty name (e.g., "/" or path ending in "/") */
  if (final_name[0] == '\0') {
    dir_close(parent_dir);
    return false;
  }

  block_sector_t inode_sector = 0;
  bool success = (free_map_allocate(1, &inode_sector) && inode_create(inode_sector, initial_size) &&
                  dir_add(parent_dir, final_name, inode_sector));

  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);

  dir_close(parent_dir);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* name) {
  struct dir* parent_dir = NULL;
  char final_name[NAME_MAX + 1];
  struct inode* inode = NULL;

  /* Parse the path to get parent directory and file name */
  if (!parse_path(name, &parent_dir, final_name)) {
    return NULL;
  }

  /* Handle root directory or path ending with "/" */
  if (final_name[0] == '\0') {
    /* Return the directory itself as a file */
    inode = dir_get_inode(parent_dir);
    inode = inode_reopen(inode); /* Get a new reference */
    dir_close(parent_dir);
    return file_open(inode);
  }

  /* Look up the file in the parent directory */
  if (!dir_lookup(parent_dir, final_name, &inode)) {
    dir_close(parent_dir);
    return NULL;
  }

  dir_close(parent_dir);
  return file_open(inode);
}

/* Deletes the file or directory named NAME.
   Returns true if successful, false on failure.
   Fails if:
   - No file/directory named NAME exists
   - NAME is the root directory
   - NAME is a non-empty directory */
bool filesys_remove(const char* name) {
  struct dir* parent_dir = NULL;
  char final_name[NAME_MAX + 1];

  /* Parse the path to get parent directory and file name */
  if (!parse_path(name, &parent_dir, final_name))
    return false;

  /* Cannot remove with empty name (root directory or path ending in "/") */
  if (final_name[0] == '\0') {
    dir_close(parent_dir);
    return false;
  }

  /* Cannot remove "." or ".." */
  if (strcmp(final_name, ".") == 0 || strcmp(final_name, "..") == 0) {
    dir_close(parent_dir);
    return false;
  }

  /* Look up the entry to check if it's a directory */
  struct inode* inode = NULL;
  if (!dir_lookup(parent_dir, final_name, &inode)) {
    dir_close(parent_dir);
    return false;
  }

  /* If it's a directory, check that it's empty */
  if (inode_is_dir(inode)) {
    struct dir* target_dir = dir_open(inode);
    if (target_dir == NULL) {
      dir_close(parent_dir);
      return false;
    }

    /* Check if directory is empty (only . and .. entries) */
    if (!dir_is_empty(target_dir)) {
      dir_close(target_dir);
      dir_close(parent_dir);
      return false;
    }
    dir_close(target_dir);
  } else {
    inode_close(inode);
  }

  /* Remove the directory entry */
  bool success = dir_remove(parent_dir, final_name);
  dir_close(parent_dir);

  return success;
}

/* Changes the current working directory to DIR_PATH.
   Returns true if successful, false on failure. */
bool filesys_chdir(const char* dir_path) {
  struct dir* parent_dir = NULL;
  char final_name[NAME_MAX + 1];

  /* Parse the path */
  if (!parse_path(dir_path, &parent_dir, final_name))
    return false;

  struct inode* inode = NULL;

  /* Handle root directory or path ending with "/" */
  if (final_name[0] == '\0') {
    /* The path refers to the parent_dir itself (e.g., "/" or "/foo/") */
    inode = inode_reopen(dir_get_inode(parent_dir));
    dir_close(parent_dir);
  } else if (strcmp(final_name, ".") == 0) {
    /* Stay in current directory */
    inode = inode_reopen(dir_get_inode(parent_dir));
    dir_close(parent_dir);
  } else if (strcmp(final_name, "..") == 0) {
    /* Go to parent of parent_dir */
    if (!dir_lookup(parent_dir, "..", &inode)) {
      dir_close(parent_dir);
      return false;
    }
    dir_close(parent_dir);
  } else {
    /* Look up the directory in parent */
    if (!dir_lookup(parent_dir, final_name, &inode)) {
      dir_close(parent_dir);
      return false;
    }
    dir_close(parent_dir);
  }

  /* Verify it's a directory */
  if (!inode_is_dir(inode)) {
    inode_close(inode);
    return false;
  }

  /* Open the new directory */
  struct dir* new_dir = dir_open(inode);
  if (new_dir == NULL) {
    return false;
  }

  /* Close old CWD and set new one */
#ifdef USERPROG
  struct thread* cur = thread_current();
  if (cur->cwd != NULL) {
    dir_close(cur->cwd);
  }
  cur->cwd = new_dir;
#else
  dir_close(new_dir);
#endif

  return true;
}

/* Creates a new directory at DIR_PATH.
   Returns true if successful, false on failure. */
bool filesys_mkdir(const char* dir_path) {
  struct dir* parent_dir = NULL;
  char final_name[NAME_MAX + 1];

  /* Parse the path to get parent directory and new directory name */
  if (!parse_path(dir_path, &parent_dir, final_name))
    return false;

  /* Cannot create directory with empty name */
  if (final_name[0] == '\0') {
    dir_close(parent_dir);
    return false;
  }

  /* Cannot create . or .. */
  if (strcmp(final_name, ".") == 0 || strcmp(final_name, "..") == 0) {
    dir_close(parent_dir);
    return false;
  }

  /* Check if name already exists */
  struct inode* existing = NULL;
  if (dir_lookup(parent_dir, final_name, &existing)) {
    inode_close(existing);
    dir_close(parent_dir);
    return false;
  }

  /* Allocate a sector for the new directory's inode */
  block_sector_t new_sector = 0;
  if (!free_map_allocate(1, &new_sector)) {
    dir_close(parent_dir);
    return false;
  }

  /* Get the parent directory's sector for the .. entry */
  block_sector_t parent_sector = inode_get_inumber(dir_get_inode(parent_dir));

  /* Create the new directory with . and .. entries */
  if (!dir_create_with_parent(new_sector, parent_sector, 16)) {
    free_map_release(new_sector, 1);
    dir_close(parent_dir);
    return false;
  }

  /* Add the new directory entry to the parent */
  if (!dir_add(parent_dir, final_name, new_sector)) {
    /* Failed to add entry - need to clean up the directory we created */
    struct inode* new_inode = inode_open(new_sector);
    if (new_inode != NULL) {
      inode_remove(new_inode);
      inode_close(new_inode);
    }
    dir_close(parent_dir);
    return false;
  }

  dir_close(parent_dir);
  return true;
}

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();

  /* Initialize WAL metadata for fresh filesystem */
  wal_init_metadata();

  /* Create root directory with . and .. entries.
     Root's .. points to itself since it has no parent. */
  if (!dir_create_with_parent(ROOT_DIR_SECTOR, ROOT_DIR_SECTOR, 16))
    PANIC("root directory creation failed");

  free_map_close();
  printf("done.\n");
}
