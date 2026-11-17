/*
 *  plat.h - what the Wii and Wii U back ends provide to the shared code
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  plat_wii.cpp (libogc: GX, ASND, libwiiinput, libwiiupdate) and
 *  plat_wiiu.cpp (wut: SDL2, libwiiuinput, libwiiuupdate) implement this.
 *  Everything else in this folder is shared. Controllers are read through
 *  the INPUT_* calls, which libwiiinput and libwiiuinput both provide.
 */

#ifndef PLAT_H
#define PLAT_H

#ifdef __WIIU__
#include <input_wiiu.h>
#include <padscore/wpad.h>
#else
#include <input_wii.h>
#include <wiiuse/wpad.h>
#endif
#include <wiiosk.h>

/* Virtual screen used for drawing and for the pointer */
#ifdef __WIIU__
#define VIRT_W 854
#define VIRT_H 480
#else
#define VIRT_W 640
#define VIRT_H 480
#endif

/* Pixel of the Mac screen texture */
#ifdef __WIIU__
typedef uint32 plat_pixel;          /* RGBA8888 */
#define PLAT_RGB(r, g, b) ((plat_pixel)(((r) << 24) | ((g) << 16) | ((b) << 8) | 0xff))
#else
typedef uint16 plat_pixel;          /* RGB565 */
#define PLAT_RGB(r, g, b) ((plat_pixel)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#endif

#define RGBA(r, g, b, a) ((uint32)(((r) << 24) | ((g) << 16) | ((b) << 8) | (a)))

/* --- system --- */

/* Storage, controllers, video; sets the app folder (ends with '/') */
extern bool plat_init(int argc, char **argv);
extern void plat_exit(void);
extern const char *plat_app_dir(void);
/* Roots for the file browser, NULL terminated; labels for display */
extern const char *const *plat_roots(const char *const **labels);
/* Default RAM size in MB */
extern int plat_default_ram_mb(void);
/* Pump system events; false once the console asks the app to quit
   (power button, the Wii U's ProcUI) */
extern bool plat_poll_system(void);
/* Leave for the loader (Wii) or the Wii U Menu; does not return */
extern void plat_quit(bool power_off) __attribute__((noreturn));

/* --- time --- */

extern uint64 plat_usec(void);       /* monotonic microseconds */
extern uint32 plat_frame(void);      /* 60Hz counter */
extern void plat_sleep_usec(uint32 usec);

/* --- video, in VIRT_W x VIRT_H coordinates --- */

/* (Re)create the texture of the Mac screen; *pixels is w x h, row stride w */
extern bool plat_screen_size(int w, int h, plat_pixel **pixels);
/* Rows y0 <= y < y1 of the pixels changed */
extern void plat_screen_update(int y0, int y1);
extern bool plat_widescreen(void);   /* the picture is 16:9 (Wii setting) */

extern void plat_begin_frame(void);
extern void plat_draw_screen(float x, float y, float w, float h, bool smooth);
extern void plat_fill_rect(float x, float y, float w, float h, uint32 rgba);
extern void plat_fill_tri(float x0, float y0, float x1, float y1,
	float x2, float y2, uint32 rgba);
extern void plat_text(float x, float y, const char *s, uint32 rgba);   /* 8x16 */
extern void plat_osk_draw(osk_keyboard *osk);
extern void plat_end_frame(void);

/* --- libwiiosk --- */

/* screen size, widescreen flag and sounds for this console */
extern void plat_osk_config(osk_config *cfg);
extern void plat_osk_read(osk_input *in);

/* --- updates (libwiiupdate / libwiiuupdate) --- */

struct plat_popup {
	const char *title;
	const char *text;
	const char *detail;
	const char *hint;
	int progress;          /* 0-100, else -1 */
};
enum { PLAT_POPUP_CLOSED = 0, PLAT_POPUP_OPEN, PLAT_POPUP_EXIT };
extern void plat_update_start(void);
extern int plat_update_popup(uint32 wpad_down, plat_popup *p);
extern void plat_update_stop(void);

/* --- audio --- */

/* Stereo 16 bit at `rate`; fill() is called from the audio thread or
   interrupt and must not block */
typedef void (*plat_audio_fill)(int16 *stereo, int frames);
extern bool plat_audio_open(int rate, plat_audio_fill fill);
extern void plat_audio_close(void);

#endif
