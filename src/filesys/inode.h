#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

/* Initialization. */
void inode_init(void);

/* Inode creation. */
bool inode_create(block_sector_t sector, off_t length);
bool inode_create_dir(block_sector_t sector, off_t length);

/* Opening and closing. */
struct inode* inode_open(block_sector_t sector);
struct inode* inode_reopen(struct inode* inode);
void inode_close(struct inode* inode);

/* Inode properties. */
block_sector_t inode_get_inumber(const struct inode* inode);
off_t inode_length(const struct inode* inode);
bool inode_is_dir(struct inode* inode);
bool inode_is_removed(const struct inode* inode);
void inode_set_skip_wal(struct inode* inode, bool skip);

/* Removal. */
void inode_remove(struct inode* inode);

/* Reading and writing. */
off_t inode_read_at(struct inode* inode, void* buffer, off_t size, off_t offset);
off_t inode_write_at(struct inode* inode, const void* buffer, off_t size, off_t offset);

/* Write denial for executables. */
void inode_deny_write(struct inode* inode);
void inode_allow_write(struct inode* inode);

#endif /* filesys/inode.h */
