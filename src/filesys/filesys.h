#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "filesys/directory.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0 /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1 /* Root directory file inode sector. */

/* Block device that contains the file system. */
extern struct block* fs_device;

void filesys_init(bool format);
void filesys_done(void);
bool filesys_create(const char* name, off_t initial_size);
struct file* filesys_open(const char* name);
bool filesys_remove(const char* name);
bool filesys_chdir(const char* dir_path);
bool filesys_mkdir(const char* dir_path);

/* Hard link support. */
bool filesys_link(const char* oldpath, const char* newpath);

/* Symbolic link support. */
bool filesys_symlink(const char* target, const char* linkpath);
int filesys_readlink(const char* path, char* buf, size_t bufsize);

/* Path parsing utilities.
   parse_path() resolves a path to its parent directory and final component.
   Returns true on success, false on failure.
   On success:
   - *parent_dir is set to the parent directory (caller must close it)
   - final_name is filled with the final path component (up to NAME_MAX chars)
   On failure:
   - *parent_dir is set to NULL
   - final_name contents are undefined */
bool parse_path(const char* path, struct dir** parent_dir, char* final_name);

/* Helper to get the starting directory for a path (root or cwd). */
struct dir* get_start_dir(const char* path);

#endif /* filesys/filesys.h */
