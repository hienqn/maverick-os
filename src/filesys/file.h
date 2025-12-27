#ifndef FILESYS_FILE_H
#define FILESYS_FILE_H

#include "filesys/off_t.h"

struct inode;

/* Opening and closing files. */
struct file* file_open(struct inode* inode);
struct file* file_reopen(struct file* file);
struct file* file_dup(struct file* file);
void file_close(struct file* file);
struct inode* file_get_inode(struct file* file);

/* Reading and writing. */
off_t file_read(struct file* file, void* buffer, off_t size);
off_t file_read_at(struct file* file, void* buffer, off_t size, off_t start);
off_t file_write(struct file* file, const void* buffer, off_t size);
off_t file_write_at(struct file* file, const void* buffer, off_t size, off_t start);

/* Preventing writes. */
void file_deny_write(struct file* file);
void file_allow_write(struct file* file);

/* File position. */
void file_seek(struct file* file, off_t position);
off_t file_tell(struct file* file);
off_t file_length(struct file* file);

#endif /* filesys/file.h */
