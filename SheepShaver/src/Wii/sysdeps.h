/*
 *  sysdeps.h - System dependent definitions for the Wii and Wii U
 *
 *  SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
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
 *  Both consoles have a PowerPC (a 750, a G3), but homebrew cannot give
 *  Mac OS the memory map and the traps the native mode needs, so the
 *  PowerPC is the kpx_cpu interpreter. The Mac address space is mapped in
 *  64 KB pages through a table (TABLE_ADDRESSING, see main_wii.cpp), and
 *  the interpreter checks the clock between blocks to run the 60Hz tick
 *  (PPC_CHECK_TICKS): no threads.
 */

#ifndef SYSDEPS_H
#define SYSDEPS_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>

#include "user_strings_wii.h"

/* Define for external components */
#define SHEEPSHAVER 1

/* What configure would find (PowerPC EABI, newlib) */
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8
#define SIZEOF_LONG_DOUBLE 8
#define SIZEOF_VOID_P 4
#define HAVE_MALLOC_H 1
#define HAVE_FLOOR 1
#define HAVE_FLOORF 1
#define HAVE_CEIL 1
#define HAVE_CEILF 1
#define HAVE_ROUND 1
#define HAVE_ROUNDF 1
#define HAVE_TRUNC 1
#define HAVE_TRUNCF 1
#define HAVE_LOG2 1
#define HAVE_LOG2F 1
#define HAVE_EXP2 1
#define HAVE_EXP2F 1
#define HAVE_LRINT 1
#define HAVE_FENV_H 1			/* the host FPU is a PowerPC FPU too */

/* No signals: Mac memory faults cannot happen through the page table, so
   the SIGSEGV handler in sheepshaver_glue.cpp is never installed */
#define HAVE_SIGSEGV_SKIP_INSTRUCTION 1
/* ENABLE_MON stays undefined: main.cpp tests it with #ifdef */

/* The PowerPC interpreter, Mac memory through the page table */
#define EMULATED_PPC 1
#define REAL_ADDRESSING 0
#define DIRECT_ADDRESSING 0
#define TABLE_ADDRESSING 1
#define POWERPC_ROM 1
#define ROM_IS_WRITE_PROTECTED 0
#define USE_SCRATCHMEM_SUBTERFUGE 0
#define USE_ETHER_FULL_DRIVER 0
#define SUPPORTS_EXTFS 0
#define SUPPORTS_UDP_TUNNEL 0

/* kpx_cpu configuration */
#define ENABLE_DYNGEN 0
#define PPC_ENABLE_JIT 0
#define PPC_REENTRANT_JIT 0
#define PPC_CHECK_INTERRUPTS 1
#define PPC_DECODE_CACHE 1
#define PPC_FLIGHT_RECORDER 0
#define PPC_PROFILE_COMPILE_TIME 0
#define PPC_PROFILE_GENERIC_CALLS 0
#define PPC_PROFILE_REGS_USE 0
#define KPX_MAX_CPUS 1

/* The 60Hz tick and the Time Manager, checked between translated blocks */
extern int32_t ppc_tick_countdown;
extern void ppc_do_check_ticks(void);
#define PPC_CHECK_TICKS() do { if (--ppc_tick_countdown <= 0) ppc_do_check_ticks(); } while (0)

/* PowerPC: big-endian, unaligned access is fine */
#define WORDS_BIGENDIAN 1

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
typedef uint32 uintptr;
typedef int32 intptr;

typedef off_t loff_t;

/* Network byte order is the host's */
#ifndef htons
#define htons(x) ((uint16)(x))
#define ntohs(x) ((uint16)(x))
#define htonl(x) ((uint32)(x))
#define ntohl(x) ((uint32)(x))
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

/* Byte swapping */
static inline uint16 generic_bswap_16(uint16 x)
{
	return ((x & 0xff) << 8) | ((x >> 8) & 0xff);
}
static inline uint32 generic_bswap_32(uint32 x)
{
	return (((x & 0xff000000) >> 24) | ((x & 0x00ff0000) >> 8)
		| ((x & 0x0000ff00) << 8) | ((x & 0x000000ff) << 24));
}
static inline uint64 generic_bswap_64(uint64 x)
{
	return ((uint64)generic_bswap_32((uint32)x) << 32) | generic_bswap_32((uint32)(x >> 32));
}
#define bswap_16 generic_bswap_16
#define bswap_32 generic_bswap_32
#define bswap_64 generic_bswap_64
#define tswap16(x) (x)
#define tswap32(x) (x)
#define tswap64(x) (x)

/* One thread: no locking needed */
typedef volatile int spinlock_t;
static const spinlock_t SPIN_LOCK_UNLOCKED = 0;
static inline void spin_lock(spinlock_t *lock) { (void)lock; }
static inline void spin_unlock(spinlock_t *lock) { (void)lock; }
static inline int spin_trylock(spinlock_t *lock) { (void)lock; return 1; }
#define HAVE_SPINLOCKS 1

/* Timing functions */
extern uint64 GetTicks_usec(void);
extern void Delay_usec(uint32 usec);

/* Macintosh colour */
typedef struct rgb_color {
	uint8 red;
	uint8 green;
	uint8 blue;
	uint8 alpha;
} rgb_color;

// Macro for calling MacOS routines
#define CallMacOS(type, tvect) call_macos((uintptr)tvect)
#define CallMacOS1(type, tvect, arg1) call_macos1((uintptr)tvect, (uintptr)arg1)
#define CallMacOS2(type, tvect, arg1, arg2) call_macos2((uintptr)tvect, (uintptr)arg1, (uintptr)arg2)
#define CallMacOS3(type, tvect, arg1, arg2, arg3) call_macos3((uintptr)tvect, (uintptr)arg1, (uintptr)arg2, (uintptr)arg3)
#define CallMacOS4(type, tvect, arg1, arg2, arg3, arg4) call_macos4((uintptr)tvect, (uintptr)arg1, (uintptr)arg2, (uintptr)arg3, (uintptr)arg4)
#define CallMacOS5(type, tvect, arg1, arg2, arg3, arg4, arg5) call_macos5((uintptr)tvect, (uintptr)arg1, (uintptr)arg2, (uintptr)arg3, (uintptr)arg4, (uintptr)arg5)
#define CallMacOS6(type, tvect, arg1, arg2, arg3, arg4, arg5, arg6) call_macos6((uintptr)tvect, (uintptr)arg1, (uintptr)arg2, (uintptr)arg3, (uintptr)arg4, (uintptr)arg5, (uintptr)arg6)
#define CallMacOS7(type, tvect, arg1, arg2, arg3, arg4, arg5, arg6, arg7) call_macos7((uintptr)tvect, (uintptr)arg1, (uintptr)arg2, (uintptr)arg3, (uintptr)arg4, (uintptr)arg5, (uintptr)arg6, (uintptr)arg7)

#ifdef __cplusplus
extern "C" {
#endif
extern uint32 call_macos(uint32 tvect);
extern uint32 call_macos1(uint32 tvect, uint32 arg1);
extern uint32 call_macos2(uint32 tvect, uint32 arg1, uint32 arg2);
extern uint32 call_macos3(uint32 tvect, uint32 arg1, uint32 arg2, uint32 arg3);
extern uint32 call_macos4(uint32 tvect, uint32 arg1, uint32 arg2, uint32 arg3, uint32 arg4);
extern uint32 call_macos5(uint32 tvect, uint32 arg1, uint32 arg2, uint32 arg3, uint32 arg4, uint32 arg5);
extern uint32 call_macos6(uint32 tvect, uint32 arg1, uint32 arg2, uint32 arg3, uint32 arg4, uint32 arg5, uint32 arg6);
extern uint32 call_macos7(uint32 tvect, uint32 arg1, uint32 arg2, uint32 arg3, uint32 arg4, uint32 arg5, uint32 arg6, uint32 arg7);
#ifdef __cplusplus
}
#endif

/* Atomic operations (main_wii.cpp) */
extern int atomic_add(int *var, int v);
extern int atomic_and(int *var, int v);
extern int atomic_or(int *var, int v);

#endif
