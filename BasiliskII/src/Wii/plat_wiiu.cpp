/*
 *  plat_wiiu.cpp - the Wii U back end (wut, Aroma)
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Video:  SDL2, one window that SDL shows on the TV and on the GamePad,
 *          drawn as an 854x480 virtual screen (16:9, square pixels).
 *  Sound:  an SDL audio device.
 *  Input:  libwiiuinput (GamePad with touch, Wii Remotes, Pro and Classic
 *          controllers, USB keyboard and mouse). Updates: libwiiuupdate.
 *          Typing: libwiiosk's SDL renderer.
 */

#include "sysdeps.h"
#include "plat.h"

#include <SDL2/SDL.h>
#include <coreinit/time.h>
#include <coreinit/thread.h>
#include <coreinit/foreground.h>
#include <vpad/input.h>
#include <sys/stat.h>

#include <wiiuupdate.h>
#include <wiiosk_sdl.h>
#include <wiiosk_input_wiiu.h>

#include "wiiu_font.h"

#ifndef APP_VERSION
#define APP_VERSION "1.0.0"
#endif
#ifndef UPDATE_OWNER
#define UPDATE_OWNER "quatric"
#endif
#ifndef UPDATE_REPO
#define UPDATE_REPO "macemu-wii"
#endif
#ifndef APP_FOLDER
#define APP_FOLDER "BasiliskIIWiiU"
#endif
#ifndef APP_TITLE
#define APP_TITLE "Basilisk II Wii U"
#endif

#define SD_ROOT "fs:/vol/external01"

/* ======================================================================
	System
   ====================================================================== */

static bool quit_requested = false;
static char app_dir[256];

const char *plat_app_dir(void)
{
	return app_dir;
}

const char *const *plat_roots(const char *const **labels)
{
	static const char *const roots[] = { SD_ROOT "/", NULL };
	static const char *const names[] = { "sd:/", NULL };
	*labels = names;
	return roots;
}

int plat_default_ram_mb(void)
{
	return 64;
}

bool plat_poll_system(void)
{
	// SDL runs ProcUI: the power button and the system asking the app to
	// close come as SDL_QUIT
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		if (ev.type == SDL_QUIT)
			quit_requested = true;
	}
	return !quit_requested;
}

uint64 plat_usec(void)
{
	return OSTicksToMicroseconds(OSGetSystemTime());
}

uint32 plat_frame(void)
{
	return (uint32)(plat_usec() / 16667);
}

void plat_sleep_usec(uint32 usec)
{
	OSSleepTicks(OSMicrosecondsToTicks(usec));
}

/* ======================================================================
	Video
   ====================================================================== */

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *screen_tex = NULL;
static SDL_Texture *font_tex = NULL;
static plat_pixel *screen_pixels = NULL;
static int screen_w = 0, screen_h = 0;

#define FONT_TEX_W 128
#define FONT_TEX_H 256

static bool build_font_texture(void)
{
	static uint32 pix[FONT_TEX_H][FONT_TEX_W];
	for (int c = 0; c < 256; c++) {
		int col = c & 15, row = c >> 4;
		for (int gy = 0; gy < 16; gy++) {
			uint8 bits = console_font_8x16[c * 16 + gy];
			for (int gx = 0; gx < 8; gx++)
				pix[row * 16 + gy][col * 8 + gx] = (bits & (0x80 >> gx)) ? RGBA(255, 255, 255, 255) : 0;
		}
	}
	font_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
		SDL_TEXTUREACCESS_STATIC, FONT_TEX_W, FONT_TEX_H);
	if (font_tex == NULL)
		return false;
	SDL_UpdateTexture(font_tex, NULL, pix, FONT_TEX_W * sizeof(uint32));
	SDL_SetTextureBlendMode(font_tex, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(font_tex, SDL_ScaleModeNearest);
	return true;
}

static bool screen_init(void)
{
	// one window: SDL shows it on the TV and on the GamePad
	window = SDL_CreateWindow(APP_TITLE, SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED, 1280, 720, 0);
	if (window == NULL)
		return false;
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (renderer == NULL)
		return false;
	SDL_RenderSetLogicalSize(renderer, VIRT_W, VIRT_H);
	if (!build_font_texture())
		return false;

	// libwiiosk draws through the same renderer
	OSK_SDL_SetRenderer(renderer);
	return true;
}

static void screen_exit(void)
{
	OSK_SDL_Shutdown();
	if (font_tex)
		SDL_DestroyTexture(font_tex);
	if (screen_tex)
		SDL_DestroyTexture(screen_tex);
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (window)
		SDL_DestroyWindow(window);
	font_tex = screen_tex = NULL;
	renderer = NULL;
	window = NULL;
}

bool plat_widescreen(void)
{
	return true;
}

bool plat_screen_size(int w, int h, plat_pixel **pixels)
{
	if (w == screen_w && h == screen_h && screen_pixels) {
		*pixels = screen_pixels;
		return true;
	}
	if (screen_tex)
		SDL_DestroyTexture(screen_tex);
	free(screen_pixels);
	screen_tex = NULL;
	screen_pixels = NULL;
	screen_w = screen_h = 0;

	screen_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
		SDL_TEXTUREACCESS_STREAMING, w, h);
	screen_pixels = (plat_pixel *)malloc(w * h * sizeof(plat_pixel));
	if (!screen_tex || !screen_pixels)
		return false;
	memset(screen_pixels, 0, w * h * sizeof(plat_pixel));
	SDL_UpdateTexture(screen_tex, NULL, screen_pixels, w * sizeof(plat_pixel));
	screen_w = w;
	screen_h = h;
	*pixels = screen_pixels;
	return true;
}

void plat_screen_update(int y0, int y1)
{
	if (!screen_tex || y1 <= y0)
		return;
	SDL_Rect r = { 0, y0, screen_w, y1 - y0 };
	SDL_UpdateTexture(screen_tex, &r, screen_pixels + y0 * screen_w, screen_w * sizeof(plat_pixel));
}

void plat_begin_frame(void)
{
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
}

void plat_draw_screen(float x, float y, float w, float h, bool smooth)
{
	if (!screen_tex)
		return;
	SDL_SetTextureScaleMode(screen_tex, smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
	SDL_FRect d = { x, y, w, h };
	SDL_RenderCopyF(renderer, screen_tex, NULL, &d);
}

void plat_fill_rect(float x, float y, float w, float h, uint32 rgba)
{
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, (rgba >> 24) & 0xff, (rgba >> 16) & 0xff,
		(rgba >> 8) & 0xff, rgba & 0xff);
	SDL_FRect r = { x, y, w, h };
	SDL_RenderFillRectF(renderer, &r);
}

void plat_fill_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32 rgba)
{
	SDL_Vertex v[3];
	SDL_Color c = { (Uint8)(rgba >> 24), (Uint8)(rgba >> 16), (Uint8)(rgba >> 8), (Uint8)rgba };
	v[0].position.x = x0; v[0].position.y = y0;
	v[1].position.x = x1; v[1].position.y = y1;
	v[2].position.x = x2; v[2].position.y = y2;
	for (int i = 0; i < 3; i++) {
		v[i].color = c;
		v[i].tex_coord.x = v[i].tex_coord.y = 0;
	}
	SDL_RenderGeometry(renderer, NULL, v, 3, NULL, 0);
}

void plat_text(float x, float y, const char *s, uint32 rgba)
{
	SDL_SetTextureColorMod(font_tex, (rgba >> 24) & 0xff, (rgba >> 16) & 0xff, (rgba >> 8) & 0xff);
	SDL_SetTextureAlphaMod(font_tex, rgba & 0xff);
	for (; *s; s++, x += 8) {
		uint8 c = (uint8)*s;
		if (c == ' ')
			continue;
		SDL_Rect src = { (c & 15) * 8, (c >> 4) * 16, 8, 16 };
		SDL_FRect dst = { x, y, 8, 16 };
		SDL_RenderCopyF(renderer, font_tex, &src, &dst);
	}
}

void plat_osk_draw(osk_keyboard *osk)
{
	OSK_Draw(osk);
}

void plat_end_frame(void)
{
	SDL_RenderPresent(renderer);
}

/* ======================================================================
	libwiiosk
   ====================================================================== */

void plat_osk_config(osk_config *cfg)
{
	cfg->screen_width = VIRT_W;
	cfg->screen_height = VIRT_H;
	// the Wii U picture is 16:9
	cfg->flags |= OSK_FLAG_WIDESCREEN;
}

void plat_osk_read(osk_input *in)
{
	OSK_ReadInputWiiU(in);
}

/* ======================================================================
	Updates (libwiiuupdate)
   ====================================================================== */

static wiiuupdate_popup update_popup;
static bool update_started = false;

void plat_update_start(void)
{
	WIIUUPDATE_Start(UPDATE_OWNER, UPDATE_REPO, APP_VERSION);
	update_started = true;
}

int plat_update_popup(uint32 wpad_down, plat_popup *p)
{
	// libwiiuupdate takes GamePad buttons
	uint32 vpad = ((wpad_down & WPAD_BUTTON_A) ? VPAD_BUTTON_A : 0)
		| ((wpad_down & WPAD_BUTTON_B) ? VPAD_BUTTON_B : 0);
	int st = WIIUUPDATE_Popup(vpad, &update_popup);
	p->title = update_popup.title;
	p->text = update_popup.text;
	p->detail = update_popup.detail;
	p->hint = update_popup.hint;
	p->progress = update_popup.progress;
	if (st == WIIUUPDATE_POPUP_EXIT)
		return PLAT_POPUP_EXIT;
	return st == WIIUUPDATE_POPUP_OPEN ? PLAT_POPUP_OPEN : PLAT_POPUP_CLOSED;
}

void plat_update_stop(void)
{
	if (update_started) {
		WIIUUPDATE_Shutdown();
		update_started = false;
	}
}

/* ======================================================================
	Sound (SDL)
   ====================================================================== */

static SDL_AudioDeviceID snd_dev = 0;
static plat_audio_fill snd_fill = NULL;

static void snd_callback(void *udata, Uint8 *stream, int len)
{
	snd_fill((int16 *)stream, len / 4);
}

bool plat_audio_open(int rate, plat_audio_fill fill)
{
	if (snd_dev)
		return true;
	snd_fill = fill;

	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = rate;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = snd_callback;
	// SDL converts to the console's own rate
	snd_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (snd_dev == 0)
		return false;
	SDL_PauseAudioDevice(snd_dev, 0);
	return true;
}

void plat_audio_close(void)
{
	if (snd_dev) {
		SDL_CloseAudioDevice(snd_dev);
		snd_dev = 0;
	}
}

/* ======================================================================
	Start and quit
   ====================================================================== */

bool plat_init(int argc, char **argv)
{
	// a .wuhb has no folder of its own: the prefs, ROM and disk images
	// live next to it on the SD card
	snprintf(app_dir, sizeof(app_dir), SD_ROOT "/wiiu/apps/" APP_FOLDER "/");
	mkdir(SD_ROOT "/wiiu", 0777);
	mkdir(SD_ROOT "/wiiu/apps", 0777);
	mkdir(SD_ROOT "/wiiu/apps/" APP_FOLDER, 0777);

	// before SDL: the USB keyboard library takes one client, and the
	// keyboard is libwiiuinput's (SDL carries on without it)
	INPUT_Init();
	INPUT_SetScreenSize(VIRT_W, VIRT_H);
	INPUT_SetCursorMode(INPUT_CURSOR_IR | INPUT_CURSOR_MOUSE | INPUT_CURSOR_TOUCH);
	// a touch on the GamePad clicks where it touches
	INPUT_SetTouchButton(WPAD_BUTTON_A);
	// the keyboard is a Mac keyboard here, not a controller
	INPUT_KeyboardMapClear();
	// HOME opens Basilisk II's menu, which can quit
	OSEnableHomeButtonMenu(FALSE);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
		return false;
	return screen_init();
}

void plat_exit(void)
{
}

void plat_quit(bool power_off)
{
	plat_audio_close();
	plat_update_stop();
	screen_exit();
	OSEnableHomeButtonMenu(TRUE);
	INPUT_Shutdown();
	// SDL hands the app back to ProcUI and the Wii U Menu
	SDL_Quit();
	exit(0);
}
