/*
 *  video_wii.cpp - Video/graphics emulation, Wii / Wii U
 *
 *  SheepShaver (C) 1997-2008 Marc Hellwig and Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Mac frame buffer is one host block, mapped at a fixed Mac address
 *  by main_wii.cpp. Every refresh compares it with a copy row by row and
 *  converts the rows that changed into the console's texture (RGB565 on
 *  the Wii, RGBA8888 on the Wii U); ui_wii.cpp draws that texture.
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "adb.h"
#include "prefs.h"
#include "user_strings.h"
#include "macos_util.h"
#include "video.h"
#include "video_defs.h"
#include "plat.h"
#include "wii_glue.h"

#include <malloc.h>

#define DEBUG 0
#include "debug.h"

// main_wii.cpp: map the frame buffer, returns its Mac address
extern uint32 MapFrameBuffer(uint8 *host, uint32 size);

#if defined(__WIIU__)
static const int MaxW = 1024, MaxH = 768;
#else
static const int MaxW = 800, MaxH = 600;
#endif

static uint8 *the_buffer = NULL;			// Mac frame buffer
static uint8 *the_buffer_copy = NULL;		// what was converted last
static uint32 the_buffer_size = 0;
static plat_pixel *pixels = NULL;			// the console texture
static int cur_w = 0, cur_h = 0;
static int cur_depth = APPLE_8_BIT;
static uint32 cur_bpr = 0;
static bool all_dirty = true;

// palette lookups
static plat_pixel host_palette[256];
static plat_pixel expand1[256][8];
static plat_pixel expand2[256][4];
static plat_pixel expand4[256][2];
static plat_pixel direct16[32768];			// 16 bit: 555 to the texture

static void build_expand(void)
{
	for (int i = 0; i < 256; i++) {
		for (int b = 0; b < 8; b++)
			expand1[i][b] = host_palette[(i >> (7 - b)) & 1];
		for (int b = 0; b < 4; b++)
			expand2[i][b] = host_palette[(i >> (6 - 2 * b)) & 3];
		expand4[i][0] = host_palette[i >> 4];
		expand4[i][1] = host_palette[i & 15];
	}
}

static void build_direct16(void)
{
	for (int i = 0; i < 32768; i++) {
		int r = (i >> 10) & 31, g = (i >> 5) & 31, b = i & 31;
		direct16[i] = PLAT_RGB((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
	}
}

/* Show VModes[cur_mode] */
static bool video_open(void)
{
	const VideoInfo &mode = VModes[cur_mode];

	cur_w = mode.viXsize;
	cur_h = mode.viYsize;
	cur_depth = mode.viAppleMode;
	cur_bpr = mode.viRowBytes;

	uint32 size = cur_bpr * cur_h;
	if (size > the_buffer_size)
		return false;
	memset(the_buffer, 0, size);
	memset(the_buffer_copy, 0xff, size);

	if (!plat_screen_size(cur_w, cur_h, &pixels))
		return false;
	all_dirty = true;
	return true;
}

bool VideoInit(void)
{
	// Frame buffer for the largest mode, mapped once
	the_buffer_size = MaxW * MaxH * 4;
	the_buffer = (uint8 *)memalign(1 << 16, the_buffer_size);
	the_buffer_copy = (uint8 *)malloc(the_buffer_size);
	if (!the_buffer || !the_buffer_copy) {
		ErrorAlert(GetString(STR_NOT_ENOUGH_MEMORY_ERR));
		return false;
	}
	screen_base = MapFrameBuffer(the_buffer, the_buffer_size);

	// Default mode from the "screen" pref ("win/640/480")
	int default_width = 640, default_height = 480;
	const char *mode_str = PrefsFindString("screen");
	if (mode_str)
		sscanf(mode_str, "%*[a-z]/%d/%d", &default_width, &default_height);
	int default_depth = APPLE_8_BIT;
	int bits = PrefsFindInt32("displaycolordepth");
	if (bits > 0)
		default_depth = DepthModeForPixelDepth(bits);

	// All the modes, in every depth
	static const struct { int w, h; uint32 id; } res[] = {
		{ 640, 480, APPLE_640x480 }, { 800, 600, APPLE_800x600 },
		{ 1024, 768, APPLE_1024x768 }, { 0, 0, 0 }
	};
	VideoInfo *p = VModes;
	cur_mode = -1;
	for (int i = 0; res[i].w; i++) {
		if (res[i].w > MaxW || res[i].h > MaxH)
			continue;
		for (int d = APPLE_1_BIT; d <= APPLE_32_BIT; d++, p++) {
			p->viType = DIS_SCREEN;
			p->viRowBytes = TrivialBytesPerRow(res[i].w, d);
			p->viXsize = res[i].w;
			p->viYsize = res[i].h;
			p->viAppleMode = d;
			p->viAppleID = res[i].id;
			if (res[i].w == default_width && res[i].h == default_height && d == default_depth)
				cur_mode = p - VModes;
		}
	}
	p->viType = DIS_INVALID;	// End marker
	p->viRowBytes = 0;
	p->viXsize = p->viYsize = 0;
	p->viAppleMode = 0;
	p->viAppleID = 0;
	if (cur_mode < 0)
		cur_mode = APPLE_8_BIT - APPLE_1_BIT;	// 640x480, 256 colours
	display_type = DIS_SCREEN;

	build_direct16();

	// Until the Mac sets a palette: pixel value 0 is white, the rest black
	for (int i = 0; i < 256; i++) {
		uint8 c = i == 0 ? 0xff : 0x00;
		mac_pal[i].red = mac_pal[i].green = mac_pal[i].blue = c;
		host_palette[i] = PLAT_RGB(c, c, c);
	}
	build_expand();

	if (!video_open()) {
		ErrorAlert(GetString(STR_NO_XVISUAL_ERR));
		return false;
	}

	// Absolute mouse positions, in Mac screen pixels
	ADBSetRelMouseMode(false);
	video_activated = true;
	return true;
}

void VideoExit(void)
{
	video_activated = false;
}

void VideoQuitFullScreen(void)
{
}

/* Called from the Mac's 60Hz interrupt */
void VideoVBL(void)
{
	if (private_data != NULL && private_data->interruptsEnabled)
		VSLDoInterruptService(private_data->vslServiceID);
}

/* the 60Hz tick: input, menus, drawing (ui_wii.cpp) */
void VideoRefresh(void)
{
	static int skip = 0;
	int frameskip = PrefsFindInt32("frameskip");

	if (frameskip < 1)
		frameskip = 1;
	bool draw = (++skip >= frameskip);
	if (draw)
		skip = 0;
	ui_tick(draw);
}

void video_set_palette(void)
{
	if (IsDirectMode(cur_depth))
		return;		// gamma table in direct modes: not used
	for (int i = 0; i < 256; i++)
		host_palette[i] = PLAT_RGB(mac_pal[i].red, mac_pal[i].green, mac_pal[i].blue);
	build_expand();
	all_dirty = true;
}

/* The Mac draws its own cursor into the frame buffer */
bool video_can_change_cursor(void)
{
	return false;
}

void video_set_cursor(void)
{
}

void video_set_dirty_area(int x, int y, int w, int h)
{
}

int16 video_mode_change(VidLocals *csSave, uint32 ParamPtr)
{
	if ((csSave->saveData == ReadMacInt32(ParamPtr + csData)) &&
		(csSave->saveMode == ReadMacInt16(ParamPtr + csMode)))
		return noErr;

	for (int i = 0; VModes[i].viType != DIS_INVALID; i++) {
		if ((ReadMacInt16(ParamPtr + csMode) == VModes[i].viAppleMode) &&
			(ReadMacInt32(ParamPtr + csData) == VModes[i].viAppleID)) {
			csSave->saveMode = ReadMacInt16(ParamPtr + csMode);
			csSave->saveData = ReadMacInt32(ParamPtr + csData);
			csSave->savePage = ReadMacInt16(ParamPtr + csPage);

			DisableInterrupt();
			cur_mode = i;
			if (!video_open()) {
				ErrorAlert(GetString(STR_NO_XVISUAL_ERR));
				QuitEmulator();
			}
			WriteMacInt32(ParamPtr + csBaseAddr, screen_base);
			csSave->saveBaseAddr = screen_base;
			csSave->saveData = VModes[cur_mode].viAppleID;
			csSave->saveMode = VModes[cur_mode].viAppleMode;
			EnableInterrupt();
			return noErr;
		}
	}
	return paramErr;
}

int video_width(void)
{
	return cur_w;
}

int video_height(void)
{
	return cur_h;
}

void video_mark_dirty(void)
{
	all_dirty = true;
}

static void convert_row(int y)
{
	const uint8 *src = the_buffer + y * cur_bpr;
	plat_pixel *dst = pixels + y * cur_w;
	int n;

	switch (cur_depth) {
	case APPLE_1_BIT:
		n = cur_w / 8;
		for (int i = 0; i < n; i++, dst += 8)
			memcpy(dst, expand1[src[i]], 8 * sizeof(plat_pixel));
		break;
	case APPLE_2_BIT:
		n = cur_w / 4;
		for (int i = 0; i < n; i++, dst += 4)
			memcpy(dst, expand2[src[i]], 4 * sizeof(plat_pixel));
		break;
	case APPLE_4_BIT:
		n = cur_w / 2;
		for (int i = 0; i < n; i++, dst += 2) {
			dst[0] = expand4[src[i]][0];
			dst[1] = expand4[src[i]][1];
		}
		break;
	case APPLE_8_BIT:
		for (int i = 0; i < cur_w; i++)
			dst[i] = host_palette[src[i]];
		break;
	case APPLE_16_BIT: {
		const uint16 *s = (const uint16 *)src;	// xRRRRRGGGGGBBBBB, host order
		for (int i = 0; i < cur_w; i++)
			dst[i] = direct16[s[i] & 0x7fff];
		break;
	}
	case APPLE_32_BIT: {
		const uint32 *s = (const uint32 *)src;	// xRGB, host order
		for (int i = 0; i < cur_w; i++) {
			uint32 px = s[i];
			dst[i] = PLAT_RGB((px >> 16) & 0xff, (px >> 8) & 0xff, px & 0xff);
		}
		break;
	}
	}
}

bool video_convert(void)
{
	if (!pixels || !the_buffer)
		return false;

	int top = cur_h, bottom = 0;
	for (int y = 0; y < cur_h; y++) {
		uint8 *a = the_buffer + y * cur_bpr;
		uint8 *b = the_buffer_copy + y * cur_bpr;
		if (all_dirty || memcmp(a, b, cur_bpr) != 0) {
			memcpy(b, a, cur_bpr);
			convert_row(y);
			if (y < top)
				top = y;
			bottom = y + 1;
		}
	}
	all_dirty = false;

	if (bottom > top) {
		plat_screen_update(top, bottom);
		return true;
	}
	return false;
}
