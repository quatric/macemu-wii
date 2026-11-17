/*
 *  timer_wii.cpp - Time Manager emulation, Wii / Wii U specific stuff
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "sysdeps.h"
#include "macos_util.h"
#include "timer.h"
#include "plat.h"

/* tm_time_t is a count of microseconds */

void Microseconds(uint32 &hi, uint32 &lo)
{
	uint64 t = plat_usec();
	hi = (uint32)(t >> 32);
	lo = (uint32)t;
}

uint32 TimerDateTime(void)
{
	/* the console clock keeps local time */
	return TimeToMacTime(time(NULL));
}

void timer_current_time(tm_time_t &t)
{
	t = plat_usec();
}

void timer_add_time(tm_time_t &res, tm_time_t a, tm_time_t b)
{
	res = a + b;
}

void timer_sub_time(tm_time_t &res, tm_time_t a, tm_time_t b)
{
	res = a - b;
}

int timer_cmp_time(tm_time_t a, tm_time_t b)
{
	int64 d = (int64)(a - b);
	return d < 0 ? -1 : (d > 0 ? 1 : 0);
}

/* Mac times are positive milliseconds or negative microseconds */
void timer_mac2host_time(tm_time_t &res, int32 mactime)
{
	if (mactime > 0)
		res = (uint64)mactime * 1000;
	else
		res = (uint64)(-(int64)mactime);
}

int32 timer_host2mac_time(tm_time_t hosttime)
{
	int64 t = (int64)hosttime;
	if (t < 0)
		return 0;
	if (t < 0x7fffffff)
		return -(int32)t;                       /* microseconds */
	t = (t + 999) / 1000;
	if (t > 0x7fffffff)
		t = 0x7fffffff;
	return (int32)t;                            /* milliseconds */
}

uint64 GetTicks_usec(void)
{
	return plat_usec();
}

void Delay_usec(uint32 usec)
{
	plat_sleep_usec(usec);
}

void idle_wait(void)
{
}

void idle_resume(void)
{
}
