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

/* Maximum symlink depth to prevent infinite loops (ELOOP). */
#define MAX_SYMLINK_DEPTH 20

static void do_format(void);

/* Forward declaration for recursive path parsing. */
static bool parse_path_recursive(const char* path, struct dir** parent_dir, char* final_name,
                                 int symlink_depth);

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
   - Symbolic links in intermediate components (followed automatically)
   - Verifies intermediate components are directories (after symlink resolution)

   Special cases:
   - "/" returns root directory with empty final_name
   - Empty path returns false
   - Path with only "." or ".." is handled correctly
   - Symlink loops detected and return false (max depth exceeded) */
bool parse_path(const char* path, struct dir** parent_dir, char* final_name) {
  return parse_path_recursive(path, parent_dir, final_name, 0);
}

/* Internal recursive path parser that tracks symlink depth.
   See parse_path() for parameter descriptions.
   symlink_depth tracks how many symlinks we've followed to detect loops. */
static bool parse_path_recursive(const char* path, struct dir** parent_dir, char* final_name,
                                 int symlink_depth) {
  *parent_dir = NULL;
  final_name[0] = '\0';

  if (path == NULL || path[0] == '\0')
    return false;

  /* Check for symlink loop (too many levels of symlinks) */
  if (symlink_depth > MAX_SYMLINK_DEPTH)
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

    /* Regular component - look it up */
    struct inode* inode = NULL;
    if (!dir_lookup(cur_dir, comp, &inode)) {
      /* Component not found */
      dir_close(cur_dir);
      free(path_copy);
      return false;
    }

    /* Check if it's a symbolic link */
    if (inode_is_symlink(inode)) {
      /* Read symlink target */
      off_t target_len = inode_length(inode);
      char* target = malloc(target_len + 1);
      if (target == NULL) {
        inode_close(inode);
        dir_close(cur_dir);
        free(path_copy);
        return false;
      }

      off_t bytes_read = inode_read_at(inode, target, target_len, 0);
      inode_close(inode);

      if (bytes_read != target_len) {
        free(target);
        dir_close(cur_dir);
        free(path_copy);
        return false;
      }
      target[target_len] = '\0';

      /* Build the new path: target + remaining components */
      /* Calculate size needed for new path */
      size_t new_path_len = target_len + 1; /* target + null or slash */
      for (int j = i + 1; j < num_components; j++) {
        new_path_len += strlen(components[j]) + 1; /* component + slash */
      }

      char* new_path = malloc(new_path_len + 1);
      if (new_path == NULL) {
        free(target);
        dir_close(cur_dir);
        free(path_copy);
        return false;
      }

      /* Build new path */
      strlcpy(new_path, target, new_path_len + 1);
      for (int j = i + 1; j < num_components; j++) {
        strlcat(new_path, "/", new_path_len + 1);
        strlcat(new_path, components[j], new_path_len + 1);
      }

      free(target);
      free(path_copy);

      /* If target is relative, we need to resolve from cur_dir.
         If target is absolute, we start from root.
         For relative paths, we need to handle this specially. */
      bool result;
      if (new_path[0] == '/') {
        /* Absolute symlink target - close cur_dir, resolve from root */
        dir_close(cur_dir);
        result = parse_path_recursive(new_path, parent_dir, final_name, symlink_depth + 1);
      } else {
        /* Relative symlink target - resolve from cur_dir */
        /* We need to construct an absolute path from cur_dir + new_path */
        /* For simplicity, we can use the cur_dir and prepend a marker */
        /* Actually, we need to save cur_dir and use it as the starting point */

        /* The trick: temporarily change to cur_dir context for relative resolution.
           Since get_start_dir() uses cwd for relative paths, we need to handle this.
           For now, let's construct the path by getting cur_dir's path...

           Simpler approach: Since we're already at cur_dir, and the symlink target
           is relative, we can resolve it by creating a path that uses cur_dir.

           Actually, the cleanest way is to manually handle this:
           - Close cur_dir for now
           - The relative path should be resolved from the symlink's parent directory
           - But we ARE in the symlink's parent directory (cur_dir)

           Let's handle relative symlinks by doing the resolution here inline. */

        /* For relative symlinks, we prepend components from cur_dir's position.
           But this is complex. A simpler approach: just fail for now or
           handle it with a special internal function.

           Actually, since new_path is relative and starts from cur_dir,
           we can recursively parse it starting from cur_dir instead of cwd/root. */

        /* Use cur_dir as base - but parse_path_recursive uses get_start_dir which
           looks at cwd. We need a different approach.

           Let's do the simple thing: recursively call ourselves but interpret
           the relative path starting from cur_dir by iterating manually. */

        /* Save cur_dir and continue parsing new_path from it */
        struct dir* base_dir = cur_dir;

        /* Parse new_path components */
        char* new_path_copy = malloc(strlen(new_path) + 1);
        if (new_path_copy == NULL) {
          free(new_path);
          dir_close(base_dir);
          return false;
        }
        strlcpy(new_path_copy, new_path, strlen(new_path) + 1);
        free(new_path);

        /* Parse into new components */
        char* new_components[MAX_PATH_COMPONENTS];
        int new_num_components = 0;
        char* new_save_ptr;

        for (token = strtok_r(new_path_copy, "/", &new_save_ptr); token != NULL;
             token = strtok_r(NULL, "/", &new_save_ptr)) {
          if (new_num_components >= MAX_PATH_COMPONENTS) {
            dir_close(base_dir);
            free(new_path_copy);
            return false;
          }
          new_components[new_num_components++] = token;
        }

        if (new_num_components == 0) {
          /* Symlink points to empty/current directory */
          *parent_dir = base_dir;
          final_name[0] = '\0';
          free(new_path_copy);
          return true;
        }

        /* Continue traversal with new components from base_dir */
        cur_dir = base_dir;

        /* Recursively process - but this is getting complex.
           Let's use a cleaner approach: just recursively call with
           an absolute path constructed from cur_dir.

           For now, let's just recursively traverse the new components. */
        for (int k = 0; k < new_num_components - 1; k++) {
          char* new_comp = new_components[k];

          if (strcmp(new_comp, ".") == 0) {
            continue;
          }

          if (strcmp(new_comp, "..") == 0) {
            struct inode* parent_inode = NULL;
            if (dir_lookup(cur_dir, "..", &parent_inode)) {
              struct dir* parent = dir_open(parent_inode);
              if (parent == NULL) {
                dir_close(cur_dir);
                free(new_path_copy);
                return false;
              }
              dir_close(cur_dir);
              cur_dir = parent;
            }
            continue;
          }

          struct inode* new_inode = NULL;
          if (!dir_lookup(cur_dir, new_comp, &new_inode)) {
            dir_close(cur_dir);
            free(new_path_copy);
            return false;
          }

          /* Check for nested symlink */
          if (inode_is_symlink(new_inode)) {
            /* Need to handle nested symlink - recursive call */
            /* Build remaining path and recurse */
            size_t remaining_len = 1;
            for (int m = k; m < new_num_components; m++) {
              remaining_len += strlen(new_components[m]) + 1;
            }
            char* remaining = malloc(remaining_len + 1);
            if (remaining == NULL) {
              inode_close(new_inode);
              dir_close(cur_dir);
              free(new_path_copy);
              return false;
            }
            remaining[0] = '\0';
            for (int m = k; m < new_num_components; m++) {
              if (m > k)
                strlcat(remaining, "/", remaining_len + 1);
              strlcat(remaining, new_components[m], remaining_len + 1);
            }

            inode_close(new_inode);
            free(new_path_copy);

            /* Recurse - cur_dir is the base for this relative path */
            /* We need to construct full path... this is getting messy.
               Let's just fail gracefully for deeply nested relative symlinks
               or use a simpler recursive approach. */

            /* Actually, let's just directly recurse with the remaining path */
            bool nested_result =
                parse_path_recursive(remaining, parent_dir, final_name, symlink_depth + 1);
            free(remaining);
            dir_close(cur_dir);
            return nested_result;
          }

          if (!inode_is_dir(new_inode)) {
            inode_close(new_inode);
            dir_close(cur_dir);
            free(new_path_copy);
            return false;
          }

          struct dir* next_dir = dir_open(new_inode);
          if (next_dir == NULL) {
            dir_close(cur_dir);
            free(new_path_copy);
            return false;
          }

          dir_close(cur_dir);
          cur_dir = next_dir;
        }

        /* Handle final component of new path */
        char* new_final = new_components[new_num_components - 1];
        if (strlen(new_final) > NAME_MAX) {
          dir_close(cur_dir);
          free(new_path_copy);
          return false;
        }

        strlcpy(final_name, new_final, NAME_MAX + 1);
        *parent_dir = cur_dir;
        free(new_path_copy);
        return true;
      }

      free(new_path);
      return result;
    }

    /* Not a symlink - verify it's a directory */
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
   or if internal memory allocation fails.
   The operation is wrapped in a WAL transaction for crash consistency. */
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

  /* Begin WAL transaction for atomic file creation */
  struct wal_txn* txn = wal_txn_begin();
  if (txn != NULL) {
    wal_set_current_txn(txn);
  }

  block_sector_t inode_sector = 0;
  bool success = (free_map_allocate(1, &inode_sector) && inode_create(inode_sector, initial_size) &&
                  dir_add(parent_dir, final_name, inode_sector));

  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);

  /* Commit or abort the transaction */
  if (txn != NULL) {
    if (success) {
      wal_txn_commit(txn);
    } else {
      wal_txn_abort(txn);
    }
    wal_set_current_txn(NULL);
  }

  dir_close(parent_dir);
  return success;
}

/* Opens the file with the given NAME, following symlinks.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* name) {
  struct dir* parent_dir = NULL;
  char final_name[NAME_MAX + 1];
  struct inode* inode = NULL;
  int symlink_depth = 0;

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

  /* Follow symlinks at the final component */
  while (inode_is_symlink(inode)) {
    if (++symlink_depth > MAX_SYMLINK_DEPTH) {
      inode_close(inode);
      dir_close(parent_dir);
      return NULL; /* Too many levels of symlinks */
    }

    /* Read the symlink target */
    off_t target_len = inode_length(inode);
    char* target = malloc(target_len + 1);
    if (target == NULL) {
      inode_close(inode);
      dir_close(parent_dir);
      return NULL;
    }

    off_t bytes_read = inode_read_at(inode, target, target_len, 0);
    inode_close(inode);

    if (bytes_read != target_len) {
      free(target);
      dir_close(parent_dir);
      return NULL;
    }
    target[target_len] = '\0';

    /* For relative symlink targets, resolve from the symlink's parent directory.
       For absolute targets, use parse_path which starts from root. */
    if (target[0] == '/') {
      /* Absolute path - resolve from root */
      dir_close(parent_dir);
      if (!parse_path(target, &parent_dir, final_name)) {
        free(target);
        return NULL;
      }
    } else {
      /* Relative path - resolve from symlink's parent directory (parent_dir).
         Look up the target starting from parent_dir. */
      struct dir* cur_dir = parent_dir; /* Keep using parent_dir */

      /* Parse target into components and traverse */
      char* target_copy = malloc(strlen(target) + 1);
      if (target_copy == NULL) {
        free(target);
        dir_close(cur_dir);
        return NULL;
      }
      strlcpy(target_copy, target, strlen(target) + 1);

      char* components[MAX_PATH_COMPONENTS];
      int num_components = 0;
      char* save_ptr;
      char* token;

      for (token = strtok_r(target_copy, "/", &save_ptr); token != NULL;
           token = strtok_r(NULL, "/", &save_ptr)) {
        if (num_components >= MAX_PATH_COMPONENTS) {
          free(target_copy);
          free(target);
          dir_close(cur_dir);
          return NULL;
        }
        components[num_components++] = token;
      }

      if (num_components == 0) {
        /* Target is empty or "/" - return parent_dir itself */
        free(target_copy);
        free(target);
        inode = dir_get_inode(cur_dir);
        inode = inode_reopen(inode);
        dir_close(cur_dir);
        return file_open(inode);
      }

      /* Traverse all but the last component */
      for (int i = 0; i < num_components - 1; i++) {
        char* comp = components[i];

        if (strcmp(comp, ".") == 0) {
          continue;
        }

        if (strcmp(comp, "..") == 0) {
          struct inode* parent_inode = NULL;
          if (dir_lookup(cur_dir, "..", &parent_inode)) {
            struct dir* new_dir = dir_open(parent_inode);
            if (new_dir == NULL) {
              free(target_copy);
              free(target);
              dir_close(cur_dir);
              return NULL;
            }
            dir_close(cur_dir);
            cur_dir = new_dir;
          }
          continue;
        }

        struct inode* next_inode = NULL;
        if (!dir_lookup(cur_dir, comp, &next_inode)) {
          free(target_copy);
          free(target);
          dir_close(cur_dir);
          return NULL;
        }

        if (!inode_is_dir(next_inode)) {
          inode_close(next_inode);
          free(target_copy);
          free(target);
          dir_close(cur_dir);
          return NULL;
        }

        struct dir* next_dir = dir_open(next_inode);
        if (next_dir == NULL) {
          free(target_copy);
          free(target);
          dir_close(cur_dir);
          return NULL;
        }
        dir_close(cur_dir);
        cur_dir = next_dir;
      }

      /* Set final_name from last component */
      strlcpy(final_name, components[num_components - 1], NAME_MAX + 1);
      parent_dir = cur_dir;
      free(target_copy);
    }
    free(target);

    /* Handle case where target is a directory (empty final_name) */
    if (final_name[0] == '\0') {
      inode = dir_get_inode(parent_dir);
      inode = inode_reopen(inode);
      dir_close(parent_dir);
      return file_open(inode);
    }

    /* Look up the target */
    if (!dir_lookup(parent_dir, final_name, &inode)) {
      dir_close(parent_dir);
      return NULL;
    }
  }

  dir_close(parent_dir);
  return file_open(inode);
}

/* Deletes the file or directory named NAME.
   Returns true if successful, false on failure.
   Fails if:
   - No file/directory named NAME exists
   - NAME is the root directory
   - NAME is a non-empty directory
   The operation is wrapped in a WAL transaction for crash consistency. */
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

  /* Begin WAL transaction for atomic file removal */
  struct wal_txn* txn = wal_txn_begin();
  if (txn != NULL) {
    wal_set_current_txn(txn);
  }

  /* Remove the directory entry */
  bool success = dir_remove(parent_dir, final_name);

  /* Commit or abort the transaction */
  if (txn != NULL) {
    if (success) {
      wal_txn_commit(txn);
    } else {
      wal_txn_abort(txn);
    }
    wal_set_current_txn(NULL);
  }

  dir_close(parent_dir);

  return success;
}

/* Changes the current working directory to DIR_PATH.
   Returns true if successful, false on failure. */
bool filesys_chdir(const char* dir_path) {
  struct dir* parent_dir = NULL;
  char final_name[NAME_MAX + 1];
  int symlink_depth = 0;

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

  /* Follow symlinks to reach the actual directory */
  while (inode_is_symlink(inode)) {
    if (++symlink_depth > MAX_SYMLINK_DEPTH) {
      inode_close(inode);
      return false; /* Too many levels of symlinks */
    }

    /* Read the symlink target */
    off_t target_len = inode_length(inode);
    char* target = malloc(target_len + 1);
    if (target == NULL) {
      inode_close(inode);
      return false;
    }

    off_t bytes_read = inode_read_at(inode, target, target_len, 0);
    inode_close(inode);

    if (bytes_read != target_len) {
      free(target);
      return false;
    }
    target[target_len] = '\0';

    /* Parse the target path */
    if (!parse_path(target, &parent_dir, final_name)) {
      free(target);
      return false;
    }
    free(target);

    /* Handle case where target is a directory (empty final_name) */
    if (final_name[0] == '\0') {
      inode = dir_get_inode(parent_dir);
      inode = inode_reopen(inode);
      dir_close(parent_dir);
      continue;
    }

    /* Look up the target */
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
   Returns true if successful, false on failure.
   The operation is wrapped in a WAL transaction for crash consistency. */
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

  /* Begin WAL transaction for atomic directory creation */
  struct wal_txn* txn = wal_txn_begin();
  if (txn != NULL) {
    wal_set_current_txn(txn);
  }

  bool success = false;

  /* Allocate a sector for the new directory's inode */
  block_sector_t new_sector = 0;
  if (!free_map_allocate(1, &new_sector)) {
    goto done;
  }

  /* Get the parent directory's sector for the .. entry */
  block_sector_t parent_sector = inode_get_inumber(dir_get_inode(parent_dir));

  /* Create the new directory with . and .. entries */
  if (!dir_create_with_parent(new_sector, parent_sector, 16)) {
    free_map_release(new_sector, 1);
    goto done;
  }

  /* Add the new directory entry to the parent */
  if (!dir_add(parent_dir, final_name, new_sector)) {
    /* Failed to add entry - need to clean up the directory we created */
    struct inode* new_inode = inode_open(new_sector);
    if (new_inode != NULL) {
      inode_remove(new_inode);
      inode_close(new_inode);
    }
    goto done;
  }

  success = true;

done:
  /* Commit or abort the transaction */
  if (txn != NULL) {
    if (success) {
      wal_txn_commit(txn);
    } else {
      wal_txn_abort(txn);
    }
    wal_set_current_txn(NULL);
  }

  dir_close(parent_dir);
  return success;
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

/* ═══════════════════════════════════════════════════════════════════════════
 * HARD LINK AND SYMBOLIC LINK SUPPORT
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Creates a hard link from NEWPATH to the file at OLDPATH.
   Returns true if successful, false otherwise.

   TODO: You need to implement this function.

   Steps:
   1. Parse oldpath to get the source inode
   2. Verify the source is a regular file (not a directory - POSIX restriction)
   3. Verify the source inode is not marked as removed
   4. Parse newpath to get the destination parent directory and name
   5. Verify destination doesn't already exist
   6. Add a directory entry in newpath's parent pointing to the source inode's sector
   7. Increment the source inode's nlink count

   Wrap the operation in a WAL transaction for crash consistency. */
bool filesys_link(const char* oldpath, const char* newpath) {
  struct inode* src_inode = NULL;
  struct dir* src_dir = NULL;
  char src_name[NAME_MAX + 1];

  /* Step 1: Parse oldpath to get the source inode */
  if (!parse_path(oldpath, &src_dir, src_name)) {
    return false;
  }

  /* Cannot link with empty name (e.g., "/" or path ending in "/") */
  if (src_name[0] == '\0') {
    dir_close(src_dir);
    return false;
  }

  /* Lookup the inode for the source file */
  if (!dir_lookup(src_dir, src_name, &src_inode)) {
    dir_close(src_dir);
    return false;
  }

  dir_close(src_dir);

  /* Step 2: Verify the source is a regular file (not a directory - POSIX restriction) */
  if (inode_is_dir(src_inode)) {
    inode_close(src_inode);
    return false;
  }

  /* Step 3: Verify the source inode is not marked as removed */
  if (inode_is_removed(src_inode)) {
    inode_close(src_inode);
    return false;
  }

  /* Step 4: Parse newpath to get the destination parent directory and name */
  struct dir* dst_dir = NULL;
  char dst_name[NAME_MAX + 1];

  if (!parse_path(newpath, &dst_dir, dst_name)) {
    inode_close(src_inode);
    return false;
  }

  /* Cannot create link with empty name (e.g., "/" or path ending in "/") */
  if (dst_name[0] == '\0') {
    dir_close(dst_dir);
    inode_close(src_inode);
    return false;
  }

  /* Step 5: Verify destination doesn't already exist */
  struct inode* existing = NULL;
  if (dir_lookup(dst_dir, dst_name, &existing)) {
    /* Destination already exists */
    inode_close(existing);
    dir_close(dst_dir);
    inode_close(src_inode);
    return false;
  }

  /* Begin WAL transaction for atomic link creation */
  struct wal_txn* txn = wal_txn_begin();
  if (txn != NULL) {
    wal_set_current_txn(txn);
  }

  /* Get the source inode's sector number */
  block_sector_t src_sector = inode_get_inumber(src_inode);

  /* Step 6: Add a directory entry in newpath's parent pointing to the source inode's sector */
  bool success = dir_add(dst_dir, dst_name, src_sector);

  /* Step 7: Increment the source inode's nlink count */
  if (success) {
    inode_inc_nlink(src_inode);
  }

  /* Commit or abort the transaction */
  if (txn != NULL) {
    if (success) {
      wal_txn_commit(txn);
    } else {
      wal_txn_abort(txn);
    }
    wal_set_current_txn(NULL);
  }

  /* Clean up */
  dir_close(dst_dir);
  inode_close(src_inode);
  return success;
}

/* Creates a symbolic link at LINKPATH pointing to TARGET.
   Returns true if successful, false otherwise.
   Note: Target path is stored as-is; it doesn't need to exist (dangling symlinks allowed). */
bool filesys_symlink(const char* target, const char* linkpath) {
  struct dir* parent_dir = NULL;
  char link_name[NAME_MAX + 1];

  /* Step 1: Parse linkpath to get parent directory and link name */
  if (!parse_path(linkpath, &parent_dir, link_name))
    return false;

  /* Cannot create symlink with empty name */
  if (link_name[0] == '\0') {
    dir_close(parent_dir);
    return false;
  }

  /* Step 2: Verify linkpath doesn't already exist */
  struct inode* existing = NULL;
  if (dir_lookup(parent_dir, link_name, &existing)) {
    inode_close(existing);
    dir_close(parent_dir);
    return false;
  }

  /* Begin WAL transaction for atomic symlink creation */
  struct wal_txn* txn = wal_txn_begin();
  if (txn != NULL) {
    wal_set_current_txn(txn);
  }

  bool success = false;

  /* Step 3: Allocate a new inode sector */
  block_sector_t inode_sector = 0;
  if (!free_map_allocate(1, &inode_sector))
    goto done;

  /* Step 4: Create symlink inode with target stored as content */
  if (!inode_create_symlink(inode_sector, target)) {
    free_map_release(inode_sector, 1);
    goto done;
  }

  /* Step 5: Add directory entry for the symlink */
  if (!dir_add(parent_dir, link_name, inode_sector)) {
    /* Clean up the symlink inode we created */
    struct inode* symlink_inode = inode_open(inode_sector);
    if (symlink_inode != NULL) {
      inode_remove(symlink_inode);
      inode_close(symlink_inode);
    }
    goto done;
  }

  success = true;

done:
  /* Commit or abort the transaction */
  if (txn != NULL) {
    if (success) {
      wal_txn_commit(txn);
    } else {
      wal_txn_abort(txn);
    }
    wal_set_current_txn(NULL);
  }

  dir_close(parent_dir);
  return success;
}

/* Reads the target of the symbolic link at PATH into BUF.
   Returns the number of bytes placed in BUF (not including null terminator),
   or -1 on error.
   Note: Does NOT follow the symlink - reads the symlink itself. */
int filesys_readlink(const char* path, char* buf, size_t bufsize) {
  struct dir* parent_dir = NULL;
  char final_name[NAME_MAX + 1];

  /* Step 1: Parse path to get parent directory and final name */
  if (!parse_path(path, &parent_dir, final_name))
    return -1;

  /* Cannot readlink with empty name */
  if (final_name[0] == '\0') {
    dir_close(parent_dir);
    return -1;
  }

  /* Look up the inode */
  struct inode* inode = NULL;
  if (!dir_lookup(parent_dir, final_name, &inode)) {
    dir_close(parent_dir);
    return -1;
  }
  dir_close(parent_dir);

  /* Step 2: Verify the inode is a symbolic link */
  if (!inode_is_symlink(inode)) {
    inode_close(inode);
    return -1;
  }

  /* Step 3: Read the symlink's content (target path) into buf */
  off_t target_len = inode_length(inode);
  size_t bytes_to_read = (size_t)target_len < bufsize ? (size_t)target_len : bufsize;

  off_t bytes_read = inode_read_at(inode, buf, bytes_to_read, 0);
  inode_close(inode);

  /* Step 4: Return the number of bytes read */
  return (int)bytes_read;
}
