/*
 *  xpram_wii.cpp - XPRAM handling, Wii / Wii U specific stuff
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "sysdeps.h"
#include "xpram.h"
#include "plat.h"

// XPRAM file, in the app folder
static char xpram_path[512];

static void make_path(void)
{
	snprintf(xpram_path, sizeof(xpram_path), "%sxpram", plat_app_dir());
}

void LoadXPRAM(const char *vmdir)
{
	(void)vmdir;
	make_path();
	FILE *f = fopen(xpram_path, "rb");
	if (f != NULL) {
		if (fread(XPRAM, 1, XPRAM_SIZE, f) != XPRAM_SIZE) {
			// a short file leaves the rest as it was
		}
		fclose(f);
	}
}

void SaveXPRAM(void)
{
	FILE *f = fopen(xpram_path, "wb");
	if (f != NULL) {
		fwrite(XPRAM, 1, XPRAM_SIZE, f);
		fclose(f);
	}
}

void ZapPRAM(void)
{
	make_path();
	unlink(xpram_path);
}
