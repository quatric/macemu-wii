/*
 *  video_wii.cpp - Video/graphics emulation, Wii / Wii U
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Mac frame buffer is plain memory mapped into the UAE memory banks.
 *  Every refresh compares it with a copy row by row and converts the rows
 *  that changed into the console's texture (RGB565 on the Wii, RGBA8888
 *  on the Wii U); ui_wii.cpp draws that texture scaled over the TV.
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "adb.h"
#include "prefs.h"
#include "user_strings.h"
#include "video.h"
#include "video_defs.h"
#include "plat.h"
#include "wii_glue.h"

#include <vector>
#include <malloc.h>

#define DEBUG 0
#include "debug.h"

class Wii_monitor_desc : public monitor_desc {
public:
	Wii_monitor_desc(const vector<video_mode> &available_modes, video_depth default_depth, uint32 default_id)
		: monitor_desc(available_modes, default_depth, default_id) {}
	~Wii_monitor_desc() {}

	virtual void switch_to_current_mode(void);
	virtual void set_palette(uint8 *pal, int num);

	bool video_open(void);
	void video_close(void);
};

// Screen size the classic (Mac Plus/SE) ROM patches use
uint32 MacScreenWidth = 512;
uint32 MacScreenHeight = 342;

static vector<video_mode> VideoModes;
static bool classic_mode = false;

static uint8 *the_buffer = NULL;			// Mac frame buffer
static uint8 *the_buffer_copy = NULL;		// what was converted last
static uint32 the_buffer_size = 0;			// allocated size of both
static plat_pixel *pixels = NULL;			// the console texture
static int cur_w = 0, cur_h = 0;
static video_depth cur_depth = VDEPTH_1BIT;
static uint32 cur_bpr = 0;
static bool all_dirty = true;

// palette lookups
static plat_pixel host_palette[256];
static plat_pixel expand1[256][8];
static plat_pixel expand2[256][4];
static plat_pixel expand4[256][2];
static plat_pixel direct16[32768];			// 16 bit: 555 to the texture

#if defined(__WIIU__)
static const int MaxW = 1024, MaxH = 768;
#else
static const int MaxW = 800, MaxH = 600;
#endif

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

static void add_mode(int w, int h, uint32 id, video_depth depth)
{
	video_mode mode;
	mode.x = w;
	mode.y = h;
	mode.resolution_id = id;
	mode.depth = depth;
	mode.bytes_per_row = TrivialBytesPerRow(w, depth);
	mode.user_data = 0;
	VideoModes.push_back(mode);
}

/* Set Mac frame layout and base address */
static void set_mac_frame_buffer(Wii_monitor_desc &monitor)
{
	MacFrameLayout = FLAYOUT_DIRECT;
	if (TwentyFourBitAddressing)
		monitor.set_mac_frame_base(MacFrameBaseMac24Bit);
	else
		monitor.set_mac_frame_base(MacFrameBaseMac);

	const video_mode &mode = monitor.get_current_mode();
	MacFrameBaseHost = the_buffer;
	MacFrameSize = mode.bytes_per_row * mode.y;
	InitFrameBufferMapping();
}

bool Wii_monitor_desc::video_open(void)
{
	const video_mode &mode = get_current_mode();

	cur_w = mode.x;
	cur_h = mode.y;
	cur_depth = mode.depth;
	cur_bpr = mode.bytes_per_row;
	MacScreenWidth = cur_w;
	MacScreenHeight = cur_h;

	uint32 size = cur_bpr * cur_h;
	if (size > the_buffer_size)
		return false;
	memset(the_buffer, 0, size);
	memset(the_buffer_copy, 0xff, size);

	if (!plat_screen_size(cur_w, cur_h, &pixels))
		return false;

	set_mac_frame_buffer(*this);
	all_dirty = true;

	// Absolute mouse positions, in Mac screen pixels
	ADBSetRelMouseMode(false);
	return true;
}

void Wii_monitor_desc::video_close(void)
{
}

bool VideoInit(bool classic)
{
	classic_mode = classic;
	VideoModes.clear();

	// Frame buffer for the largest mode, allocated once
	the_buffer_size = MaxW * MaxH * 4;
	the_buffer = (uint8 *)memalign(32, the_buffer_size);
	the_buffer_copy = (uint8 *)malloc(the_buffer_size);
	if (!the_buffer || !the_buffer_copy) {
		ErrorAlert(STR_NO_MEM_ERR);
		return false;
	}

	// Default mode from the "screen" pref ("win/640/480")
	int default_width = 640, default_height = 480;
	const char *mode_str = PrefsFindString("screen");
	if (mode_str)
		sscanf(mode_str, "%*[a-z]/%d/%d", &default_width, &default_height);

	uint32 default_id = 0x81;
	video_depth default_depth = VDEPTH_8BIT;

	if (classic) {
		add_mode(512, 342, 0x80, VDEPTH_1BIT);
		default_id = 0x80;
		default_depth = VDEPTH_1BIT;
	} else {
		static const struct { int w, h; uint32 id; } res[] = {
			{ 512, 384, 0x80 }, { 640, 480, 0x81 }, { 800, 600, 0x82 },
			{ 1024, 768, 0x83 }, { 0, 0, 0 }
		};
		for (int i = 0; res[i].w; i++) {
			if (res[i].w > MaxW || res[i].h > MaxH)
				continue;
			for (int d = VDEPTH_1BIT; d <= VDEPTH_32BIT; d++)
				add_mode(res[i].w, res[i].h, res[i].id, (video_depth)d);
			if (res[i].w == default_width && res[i].h == default_height)
				default_id = res[i].id;
		}
		int bits = PrefsFindInt32("displaycolordepth");
		if (bits > 0)
			default_depth = DepthModeForPixelDepth(bits);
	}

	build_direct16();

	// Until the Mac sets a palette (and always on a classic Mac, which has
	// none): pixel value 0 is white, 1 black
	for (int i = 0; i < 256; i++)
		host_palette[i] = i == 0 ? PLAT_RGB(255, 255, 255) : PLAT_RGB(0, 0, 0);
	build_expand();

	Wii_monitor_desc *monitor = new Wii_monitor_desc(VideoModes, default_depth, default_id);
	VideoMonitors.push_back(monitor);
	return monitor->video_open();
}

void VideoExit(void)
{
	vector<monitor_desc *>::iterator i, end = VideoMonitors.end();
	for (i = VideoMonitors.begin(); i != end; ++i)
		dynamic_cast<Wii_monitor_desc *>(*i)->video_close();
	VideoMonitors.clear();
	VideoModes.clear();
}

void VideoQuitFullScreen(void)
{
}

void VideoInterrupt(void)
{
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

void Wii_monitor_desc::set_palette(uint8 *pal, int num)
{
	if (IsDirectMode(get_current_mode()))
		return;		// gamma table in direct modes: not used

	for (int i = 0; i < 256; i++) {
		int c = i & (num - 1);	// fewer colours repeat
		host_palette[i] = PLAT_RGB(pal[c * 3], pal[c * 3 + 1], pal[c * 3 + 2]);
	}
	build_expand();
	all_dirty = true;
}

void Wii_monitor_desc::switch_to_current_mode(void)
{
	video_close();
	if (!video_open()) {
		ErrorAlert(STR_NO_XVISUAL_ERR);
		QuitEmulator();
	}
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
	case VDEPTH_1BIT:
		n = cur_w / 8;
		for (int i = 0; i < n; i++, dst += 8)
			memcpy(dst, expand1[src[i]], 8 * sizeof(plat_pixel));
		break;
	case VDEPTH_2BIT:
		n = cur_w / 4;
		for (int i = 0; i < n; i++, dst += 4)
			memcpy(dst, expand2[src[i]], 4 * sizeof(plat_pixel));
		break;
	case VDEPTH_4BIT:
		n = cur_w / 2;
		for (int i = 0; i < n; i++, dst += 2) {
			dst[0] = expand4[src[i]][0];
			dst[1] = expand4[src[i]][1];
		}
		break;
	case VDEPTH_8BIT:
		for (int i = 0; i < cur_w; i++)
			dst[i] = host_palette[src[i]];
		break;
	case VDEPTH_16BIT: {
		const uint16 *s = (const uint16 *)src;	// xRRRRRGGGGGBBBBB, host order
		for (int i = 0; i < cur_w; i++)
			dst[i] = direct16[s[i] & 0x7fff];
		break;
	}
	case VDEPTH_32BIT: {
		const uint32 *s = (const uint32 *)src;	// xRGB, host order
		for (int i = 0; i < cur_w; i++) {
			uint32 p = s[i];
			dst[i] = PLAT_RGB((p >> 16) & 0xff, (p >> 8) & 0xff, p & 0xff);
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
