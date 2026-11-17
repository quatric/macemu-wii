/*
 *  vm_alloc.h - memory for the Wii and Wii U (no virtual memory to map)
 *
 *  SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef VM_ALLOC_H
#define VM_ALLOC_H

#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#define VM_MAP_FAILED			((void *)-1)
#define VM_MAP_SHARED			0x01
#define VM_MAP_PRIVATE			0x02
#define VM_MAP_FIXED			0x04
#define VM_MAP_32BIT			0x08
#define VM_MAP_DEFAULT			(VM_MAP_PRIVATE)

#define VM_PAGE_NOACCESS		0x0
#define VM_PAGE_READ			0x1
#define VM_PAGE_WRITE			0x2
#define VM_PAGE_EXECUTE			0x4
#define VM_PAGE_DEFAULT			(VM_PAGE_READ | VM_PAGE_WRITE)

static inline int vm_init(void) { return 0; }
static inline void vm_exit(void) {}

/* Zeroed, 32 byte aligned memory */
static inline void *vm_acquire(size_t size, int options = VM_MAP_DEFAULT)
{
	void *p = memalign(32, size);
	if (p == NULL)
		return VM_MAP_FAILED;
	memset(p, 0, size);
	return p;
}

static inline int vm_release(void *addr, size_t size)
{
	if (addr != VM_MAP_FAILED)
		free(addr);
	return 0;
}

static inline int vm_protect(void *addr, size_t size, int prot) { return 0; }
static inline int vm_get_page_size(void) { return 4096; }

#endif
