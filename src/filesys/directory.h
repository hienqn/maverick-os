#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "filesys/off_t.h"

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

/* Forward declarations. */
struct inode;
struct dir;

/* Opening and closing directories. */
bool dir_create(block_sector_t sector, size_t entry_cnt);
bool dir_create_with_parent(block_sector_t sector, block_sector_t parent_sector, size_t entry_cnt);
struct dir* dir_open(struct inode* inode);
struct dir* dir_open_root(void);
struct dir* dir_reopen(struct dir* dir);
void dir_close(struct dir* dir);
struct inode* dir_get_inode(struct dir* dir);
off_t dir_get_pos(struct dir* dir);
void dir_set_pos(struct dir* dir, off_t pos);

/* Reading and writing. */
bool dir_lookup(const struct dir* dir, const char* name, struct inode** inode);
bool dir_add(struct dir* dir, const char* name, block_sector_t inode_sector);
bool dir_remove(struct dir* dir, const char* name);
bool dir_readdir(struct dir* dir, char name[NAME_MAX + 1]);
bool dir_is_empty(struct dir* dir);

#endif /* filesys/directory.h */
