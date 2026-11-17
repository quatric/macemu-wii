/*
 *  sigsegv.h - no memory fault handling on the Wii and Wii U
 *
 *  SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Only the types, so that the kpx_cpu glue compiles; nothing installs the
 *  handler.
 */

#ifndef SIGSEGV_H
#define SIGSEGV_H

typedef char *sigsegv_address_t;

enum sigsegv_return_t {
	SIGSEGV_RETURN_SUCCESS,
	SIGSEGV_RETURN_FAILURE,
	SIGSEGV_RETURN_SKIP_INSTRUCTION
};

struct sigsegv_info_t {
	sigsegv_address_t addr;
	sigsegv_address_t pc;
};

static inline sigsegv_address_t sigsegv_get_fault_address(sigsegv_info_t *sip) { return sip->addr; }
static inline sigsegv_address_t sigsegv_get_fault_instruction_address(sigsegv_info_t *sip) { return sip->pc; }

#endif
