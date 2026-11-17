/*
 *  sysdeps.h - System dependent definitions for the Wii and Wii U
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/*
 *  Both consoles are 32 bit big-endian PowerPC, like the classic Macs'
 *  successors, so Mac memory needs no byte swapping. The 68k is the UAE
 *  interpreter with banked memory (no MMU tricks are needed), and the CPU
 *  loop itself runs the 60Hz tick (USE_CPU_EMUL_SERVICES): no threads.
 *
 *  This file is also included by the host build of the CPU generators
 *  (build68k, gencpu), so it uses no console headers.
 */

/* The SheepShaver build compiles the shared files in this folder too:
   they take its definitions (a quoted include finds this file first) */
#ifdef SHEEPSHAVER_BUILD
#include "../../../SheepShaver/src/Wii/sysdeps.h"
#else

#ifndef SYSDEPS_H
#define SYSDEPS_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>

#include "user_strings_wii.h"

/* Mac and host address space are distinct; UAE memory banks */
#define REAL_ADDRESSING 0
#define DIRECT_ADDRESSING 0

/* Using the 68k emulator */
#define EMULATED_68K 1
#define USE_PREFETCH_BUFFER 0
#define ROM_IS_WRITE_PROTECTED 1
#define USE_JIT 0

/* No host file system bridge or networking */
#define SUPPORTS_EXTFS 0
#define SUPPORTS_UDP_TUNNEL 0

/* The CPU emulator checks for the 60Hz tick itself */
#define USE_CPU_EMUL_SERVICES

/* PowerPC: big-endian, unaligned access is fine */
#define WORDS_BIGENDIAN 1
#define CPU_CAN_ACCESS_UNALIGNED

/* What configure would find (PowerPC EABI, newlib) */
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8
#define SIZEOF_LONG_DOUBLE 8
#define SIZEOF_VOID_P 4
#define HAVE_ISNAN 1
#define HAVE_ISINF 1

/* Data types */
typedef unsigned char uint8;
typedef signed char int8;
typedef unsigned short uint16;
typedef short int16;
typedef unsigned int uint32;
typedef int int32;
#ifndef _UINT64
typedef unsigned long long uint64;
#define _UINT64
#endif
typedef long long int64;
#define VAL64(a) (a ## LL)
#define UVAL64(a) (a ## uLL)

#if UINTPTR_MAX == 0xffffffffu
typedef uint32 uintptr;
typedef int32 intptr;
#else
typedef uint64 uintptr;
typedef int64 intptr;
#endif

typedef off_t loff_t;

/* Network byte order is the host's on PowerPC (the ROM and resource
   patches use these); a little-endian host only runs the generators */
#ifndef htons
#if defined(__BIG_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define htons(x) ((uint16)(x))
#define ntohs(x) ((uint16)(x))
#define htonl(x) ((uint32)(x))
#define ntohl(x) ((uint32)(x))
#endif
#endif

/* Time data type for Time Manager emulation: microseconds */
typedef uint64 tm_time_t;

/* Float formats */
#define UNKNOWN_FLOAT_FORMAT 0
#define IEEE_FLOAT_FORMAT 1
#define VAX_FLOAT_FORMAT 2
#define IBM_FLOAT_FORMAT 3
#define C4X_FLOAT_FORMAT 4
#define HOST_FLOAT_FORMAT IEEE_FLOAT_FORMAT

/* UAE CPU data types */
#define uae_s8 int8
#define uae_u8 uint8
#define uae_s16 int16
#define uae_u16 uint16
#define uae_s32 int32
#define uae_u32 uint32
#define uae_s64 int64
#define uae_u64 uint64
typedef uae_u32 uaecptr;

/* Timing functions */
extern uint64 GetTicks_usec(void);
extern void Delay_usec(uint32 usec);

/* One thread: no locking needed */
typedef volatile int spinlock_t;
static const spinlock_t SPIN_LOCK_UNLOCKED = 0;
static inline void spin_lock(spinlock_t *lock) { (void)lock; }
static inline void spin_unlock(spinlock_t *lock) { (void)lock; }
static inline int spin_trylock(spinlock_t *lock) { (void)lock; return 1; }

/* UAE CPU defines: big-endian, unaligned access allowed */
static inline uae_u32 do_get_mem_long(uae_u32 *a) {return *a;}
static inline uae_u32 do_get_mem_word(uae_u16 *a) {return *a;}
static inline void do_put_mem_long(uae_u32 *a, uae_u32 v) {*a = v;}
static inline void do_put_mem_word(uae_u16 *a, uae_u32 v) {*a = v;}

static inline uae_u32 do_byteswap_32(uae_u32 v)
	{ return (((v >> 24) & 0xff) | ((v >> 8) & 0xff00) | ((v & 0xff) << 24) | ((v & 0xff00) << 8)); }
static inline uae_u32 do_byteswap_16(uae_u32 v)
	{ return (((v >> 8) & 0xff) | ((v & 0xff) << 8)); }

#define do_get_mem_byte(a) ((uae_u32)*((uae_u8 *)(a)))
#define do_put_mem_byte(a, v) (*(uae_u8 *)(a) = (v))

#define call_mem_get_func(func, addr) ((*func)(addr))
#define call_mem_put_func(func, addr, v) ((*func)(addr, v))
#define __inline__ inline
#define CPU_EMU_SIZE 0
#undef NO_INLINE_MEMORY_ACCESS
#undef MD_HAVE_MEM_1_FUNCS
#define ENUMDECL typedef enum
#define ENUMNAME(name) name
#define write_log printf

#define ASM_SYM(a)

#ifndef REGPARAM
# define REGPARAM
#endif
#define REGPARAM2

#endif

#endif /* SHEEPSHAVER_BUILD */
