#ifndef THREADS_INIT_H
#define THREADS_INIT_H

#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file threads/init.h
 * @brief Kernel initialization header file.
 *
 * This header file provides the interface for kernel initialization.
 * It declares the initial page directory that contains only kernel
 * virtual memory mappings, which is set up during boot before any
 * user processes are created.
 */

/**
 * @brief Page directory with kernel mappings only.
 *
 * This page directory is created during kernel initialization in
 * paging_init(). It contains mappings for all physical memory pages
 * to their corresponding kernel virtual addresses. This is the base
 * page directory used before any user processes are created.
 *
 * Each user process will get its own page directory, but they all
 * share the kernel mappings from this initial directory.
 *
 * @note This is set up before thread scheduling begins and remains
 *       valid for the lifetime of the kernel.
 */
extern uint32_t* init_page_dir;

#endif /* threads/init.h */
