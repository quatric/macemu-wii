/*
 *  plat_wii.cpp - the Wii back end (libogc)
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Video:  GX draws a 640x480 virtual screen over the whole TV picture (the
 *          video line is widened to 720 pixels: no side borders). The Mac
 *          screen is an RGB565 texture.
 *  Sound:  one ASND voice, double buffered.
 *  Input:  libwiiinput. Updates: libwiiupdate. Typing: libwiiosk.
 */

#include "sysdeps.h"
#include "plat.h"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <asndlib.h>
#include <fat.h>
#include <malloc.h>
#include <sys/stat.h>

#include <wiiupdate.h>
#include <wiiosk_input_wii.h>

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
#define APP_FOLDER "BasiliskIIWii"
#endif

extern "C" u8 console_font_8x16[];

/* ======================================================================
	System
   ====================================================================== */

static volatile bool shutdown_requested = false;
static volatile bool reset_requested = false;
static char app_dir[256];
static const char *app_path = NULL;

static void power_button_cb(void)
{
	shutdown_requested = true;
}

static void reset_button_cb(u32 irq, void *ctx)
{
	reset_requested = true;
}

static void wiimote_power_cb(s32 chan)
{
	shutdown_requested = true;
}

/* The folder the app was started from, e.g. sd:/apps/BasiliskIIWii/ */
static void set_app_dir(int argc, char **argv)
{
	if (argc > 0 && argv[0] && strchr(argv[0], ':')) {
		app_path = argv[0];
		snprintf(app_dir, sizeof(app_dir), "%s", argv[0]);
		char *slash = strrchr(app_dir, '/');
		if (slash)
			slash[1] = 0;
		struct stat st;
		if (stat(app_dir, &st) == 0)
			return;
	}
	static const char *const tries[] = {
		"sd:/apps/" APP_FOLDER "/", "usb:/apps/" APP_FOLDER "/", NULL
	};
	for (int i = 0; tries[i]; i++) {
		struct stat st;
		if (stat(tries[i], &st) == 0) {
			snprintf(app_dir, sizeof(app_dir), "%s", tries[i]);
			return;
		}
	}
	// make it on the SD card
	mkdir("sd:/apps", 0777);
	mkdir("sd:/apps/" APP_FOLDER, 0777);
	snprintf(app_dir, sizeof(app_dir), "sd:/apps/" APP_FOLDER "/");
}

const char *plat_app_dir(void)
{
	return app_dir;
}

const char *const *plat_roots(const char *const **labels)
{
	static const char *const roots[] = { "sd:/", "usb:/", NULL };
	*labels = roots;
	return roots;
}

int plat_default_ram_mb(void)
{
	return 32;
}

bool plat_poll_system(void)
{
	return !shutdown_requested && !reset_requested;
}

uint64 plat_usec(void)
{
	return ticks_to_microsecs(gettime());
}

uint32 plat_frame(void)
{
	return (uint32)(plat_usec() / 16667);
}

void plat_sleep_usec(uint32 usec)
{
	usleep(usec);
}

/* ======================================================================
	Video
   ====================================================================== */

#define FIFO_SIZE (256 * 1024)

static GXRModeObj *rmode = NULL;
static void *xfb[2] = { NULL, NULL };
static int cur_xfb = 0;
static void *gp_fifo = NULL;
static bool wide_screen = false;
static volatile u32 retrace_count = 0;
static u32 last_flip_retrace = 0;

static void retrace_cb(u32 count)
{
	retrace_count++;
}

// the Mac screen: linear pixels from video_wii.cpp, tiled for GX
static plat_pixel *screen_pixels = NULL;
static u16 *screen_tex = NULL;
static int screen_w = 0, screen_h = 0, tex_w = 0, tex_h = 0;
static GXTexObj screen_tex_obj;

#define FONT_TEX_W 128
#define FONT_TEX_H 256
static u16 font_tex[FONT_TEX_W * FONT_TEX_H] ATTRIBUTE_ALIGN(32);
static GXTexObj font_tex_obj;

// a solid texel of the font texture: the middle of glyph 0xDB (full block)
#define SOLID_S ((11 * 8 + 4) / (f32)FONT_TEX_W)
#define SOLID_T ((13 * 16 + 8) / (f32)FONT_TEX_H)

// index of a 16 bit texel inside a 4x4-tiled texture of width w
#define TILE16(x, y, w) \
	((((y) >> 2) * ((w) >> 2) + ((x) >> 2)) * 16 + (((y) & 3) << 2) + ((x) & 3))

static void build_font_texture(void)
{
	for (int c = 0; c < 256; c++) {
		int col = c & 15, row = c >> 4;
		for (int gy = 0; gy < 16; gy++) {
			u8 bits = console_font_8x16[c * 16 + gy];
			for (int gx = 0; gx < 8; gx++) {
				int x = col * 8 + gx, y = row * 16 + gy;
				// IA8: alpha in the high byte, intensity in the low
				font_tex[TILE16(x, y, FONT_TEX_W)] = (bits & (0x80 >> gx)) ? 0xffff : 0x00ff;
			}
		}
	}
	DCFlushRange(font_tex, sizeof(font_tex));
	GX_InitTexObj(&font_tex_obj, font_tex, FONT_TEX_W, FONT_TEX_H, GX_TF_IA8,
		GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(&font_tex_obj, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f,
		GX_FALSE, GX_FALSE, GX_ANISO_1);
}

/* The GX state our drawing needs; libwiiosk's OSK_Draw() changes it */
static void setup_draw_state(void)
{
	Mtx44 proj;
	Mtx ident;

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);

	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaUpdate(GX_TRUE);
	GX_SetColorUpdate(GX_TRUE);

	guMtxIdentity(ident);
	GX_LoadPosMtxImm(ident, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	guOrtho(proj, 0, VIRT_H, 0, VIRT_W, 0, 1000);
	GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);

	GX_LoadTexObj(&font_tex_obj, GX_TEXMAP0);
}

static bool screen_init(void)
{
	VIDEO_Init();

	rmode = VIDEO_GetPreferredMode(NULL);
	wide_screen = CONF_GetAspectRatio() == CONF_ASPECT_16_9;

	// the full width of the video line: the picture reaches the TV's edges
	rmode->viWidth = VI_MAX_WIDTH_NTSC;
	rmode->viXOrigin = 0;

	VIDEO_Configure(rmode);
	VIDEO_SetPostRetraceCallback(retrace_cb);

	for (int i = 0; i < 2; i++) {
		xfb[i] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
		VIDEO_ClearFrameBuffer(rmode, xfb[i], COLOR_BLACK);
	}
	VIDEO_SetNextFramebuffer(xfb[0]);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();

	gp_fifo = memalign(32, FIFO_SIZE);
	if (gp_fifo == NULL)
		return false;
	memset(gp_fifo, 0, FIFO_SIZE);
	GX_Init(gp_fifo, FIFO_SIZE);

	GXColor black = { 0, 0, 0, 0xff };
	GX_SetCopyClear(black, GX_MAX_Z24);
	GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
	f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
	u32 xfb_height = GX_SetDispCopyYScale(yscale);
	GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopyDst(rmode->fbWidth, xfb_height);
	GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
	GX_SetFieldMode(rmode->field_rendering,
		(rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
	GX_SetPixelFmt(rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetCullMode(GX_CULL_NONE);
	GX_CopyDisp(xfb[0], GX_TRUE);
	GX_SetDispCopyGamma(GX_GM_1_0);

	build_font_texture();
	setup_draw_state();
	return true;
}

static void screen_exit(void)
{
	if (rmode) {
		GX_AbortFrame();
		GX_Flush();
		VIDEO_SetBlack(TRUE);
		VIDEO_Flush();
		VIDEO_WaitVSync();
	}
}

bool plat_widescreen(void)
{
	return wide_screen;
}

bool plat_screen_size(int w, int h, plat_pixel **pixels)
{
	if (w == screen_w && h == screen_h && screen_pixels) {
		*pixels = screen_pixels;
		return true;
	}
	free(screen_pixels);
	free(screen_tex);
	screen_pixels = NULL;
	screen_tex = NULL;
	screen_w = screen_h = 0;

	int tw = (w + 3) & ~3, th = (h + 3) & ~3;
	if (tw > 1024 || th > 1024)
		return false;
	screen_pixels = (plat_pixel *)malloc(w * h * sizeof(plat_pixel));
	screen_tex = (u16 *)memalign(32, tw * th * sizeof(u16));
	if (!screen_pixels || !screen_tex)
		return false;
	memset(screen_pixels, 0, w * h * sizeof(plat_pixel));
	memset(screen_tex, 0, tw * th * sizeof(u16));
	DCFlushRange(screen_tex, tw * th * sizeof(u16));

	screen_w = w;
	screen_h = h;
	tex_w = tw;
	tex_h = th;
	GX_InitTexObj(&screen_tex_obj, screen_tex, tex_w, tex_h, GX_TF_RGB565,
		GX_CLAMP, GX_CLAMP, GX_FALSE);
	*pixels = screen_pixels;
	return true;
}

void plat_screen_update(int y0, int y1)
{
	if (!screen_tex)
		return;
	if (y1 > screen_h)
		y1 = screen_h;
	for (int y = y0; y < y1; y++) {
		u16 *dst = screen_tex + TILE16(0, y, tex_w);
		const plat_pixel *src = screen_pixels + y * screen_w;
		int blocks = screen_w / 4;
		for (int bx = 0; bx < blocks; bx++) {
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			dst[3] = src[3];
			dst += 16;
			src += 4;
		}
	}
	int t0 = y0 & ~3, t1 = (y1 + 3) & ~3;
	DCFlushRange(screen_tex + t0 * tex_w, (t1 - t0) * tex_w * sizeof(u16));
	GX_InvalidateTexAll();
}

static void draw_quad(f32 x0, f32 y0, f32 x1, f32 y1, f32 s0, f32 t0, f32 s1, f32 t1, u32 color)
{
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	GX_Position3f32(x0, y0, 0);
	GX_Color1u32(color);
	GX_TexCoord2f32(s0, t0);
	GX_Position3f32(x1, y0, 0);
	GX_Color1u32(color);
	GX_TexCoord2f32(s1, t0);
	GX_Position3f32(x1, y1, 0);
	GX_Color1u32(color);
	GX_TexCoord2f32(s1, t1);
	GX_Position3f32(x0, y1, 0);
	GX_Color1u32(color);
	GX_TexCoord2f32(s0, t1);
	GX_End();
}

void plat_begin_frame(void)
{
	GX_InvVtxCache();
	GX_LoadTexObj(&font_tex_obj, GX_TEXMAP0);
}

void plat_draw_screen(float x, float y, float w, float h, bool smooth)
{
	if (!screen_tex)
		return;
	u8 filt = smooth ? GX_LINEAR : GX_NEAR;
	GX_InitTexObjLOD(&screen_tex_obj, filt, filt, 0.0f, 0.0f, 0.0f,
		GX_FALSE, GX_FALSE, GX_ANISO_1);
	GX_LoadTexObj(&screen_tex_obj, GX_TEXMAP0);
	draw_quad(x, y, x + w, y + h, 0, 0, (f32)screen_w / tex_w, (f32)screen_h / tex_h,
		0xffffffff);
	GX_LoadTexObj(&font_tex_obj, GX_TEXMAP0);
}

void plat_fill_rect(float x, float y, float w, float h, uint32 rgba)
{
	draw_quad(x, y, x + w, y + h, SOLID_S, SOLID_T, SOLID_S, SOLID_T, rgba);
}

void plat_fill_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32 rgba)
{
	GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
	GX_Position3f32(x0, y0, 0);
	GX_Color1u32(rgba);
	GX_TexCoord2f32(SOLID_S, SOLID_T);
	GX_Position3f32(x1, y1, 0);
	GX_Color1u32(rgba);
	GX_TexCoord2f32(SOLID_S, SOLID_T);
	GX_Position3f32(x2, y2, 0);
	GX_Color1u32(rgba);
	GX_TexCoord2f32(SOLID_S, SOLID_T);
	GX_End();
}

void plat_text(float x, float y, const char *s, uint32 rgba)
{
	for (; *s; s++, x += 8) {
		u8 c = (u8)*s;
		if (c == ' ')
			continue;
		f32 s0 = ((c & 15) * 8) / (f32)FONT_TEX_W;
		f32 t0 = ((c >> 4) * 16) / (f32)FONT_TEX_H;
		draw_quad(x, y, x + 8, y + 16, s0, t0,
			s0 + 8 / (f32)FONT_TEX_W, t0 + 16 / (f32)FONT_TEX_H, rgba);
	}
}

void plat_osk_draw(osk_keyboard *osk)
{
	// libwiiosk draws over the whole screen with its own GX state
	OSK_Draw(osk);
	setup_draw_state();
}

void plat_end_frame(void)
{
	GX_DrawDone();

	// never two frames in one video field: the one shown would be redrawn
	if (retrace_count == last_flip_retrace)
		VIDEO_WaitVSync();

	GX_CopyDisp(xfb[cur_xfb], GX_TRUE);
	GX_Flush();
	VIDEO_SetNextFramebuffer(xfb[cur_xfb]);
	VIDEO_Flush();
	last_flip_retrace = retrace_count;
	cur_xfb ^= 1;
}

/* ======================================================================
	libwiiosk
   ====================================================================== */

void plat_osk_config(osk_config *cfg)
{
	static bool sound_ready = false;
	cfg->screen_width = VIRT_W;
	cfg->screen_height = VIRT_H;
	if (wide_screen)
		cfg->flags |= OSK_FLAG_WIDESCREEN;
	if (!sound_ready) {
		OSK_InitSound(false);	// ASND is already running
		sound_ready = true;
	}
}

void plat_osk_read(osk_input *in)
{
	OSK_ReadInputWii(in);
}

/* ======================================================================
	Updates (libwiiupdate)
   ====================================================================== */

static wiiupdate_popup update_popup;

void plat_update_start(void)
{
	static const char *const update_files[] = { "boot.dol", "meta.xml", "icon.png", NULL };
	WIIUPDATE_SetAppPath(app_path);
	WIIUPDATE_SetUpdateFiles(update_files);
	WIIUPDATE_Start(UPDATE_OWNER, UPDATE_REPO, APP_VERSION);
}

int plat_update_popup(uint32 wpad_down, plat_popup *p)
{
	int st = WIIUPDATE_Popup(wpad_down, &update_popup);
	p->title = update_popup.title;
	p->text = update_popup.text;
	p->detail = update_popup.detail;
	p->hint = update_popup.hint;
	p->progress = update_popup.progress;
	if (st == WIIUPDATE_POPUP_EXIT)
		return PLAT_POPUP_EXIT;
	return st == WIIUPDATE_POPUP_OPEN ? PLAT_POPUP_OPEN : PLAT_POPUP_CLOSED;
}

void plat_update_stop(void)
{
	if (WIIUPDATE_InstallState() != WIIUPDATE_INSTALL_DOWNLOADING)
		WIIUPDATE_Cancel();
}

/* ======================================================================
	Sound (ASND)
   ====================================================================== */

#define SND_FRAMES 1024
static int16 snd_out[2][SND_FRAMES * 2] ATTRIBUTE_ALIGN(32);
static volatile int snd_cur = 0;
static plat_audio_fill snd_fill = NULL;
static int snd_rate = 0;
static bool snd_open = false;

static void snd_voice_cb(s32 voice)
{
	if (!ASND_TestVoiceBufferReady(voice))
		return;
	snd_fill(snd_out[snd_cur], SND_FRAMES);
	DCFlushRange(snd_out[snd_cur], sizeof(snd_out[0]));
	if (ASND_AddVoice(voice, snd_out[snd_cur], sizeof(snd_out[0])) == SND_OK)
		snd_cur ^= 1;
}

bool plat_audio_open(int rate, plat_audio_fill fill)
{
	if (snd_open)
		return true;
	snd_fill = fill;
	snd_rate = rate;

	fill(snd_out[0], SND_FRAMES);
	DCFlushRange(snd_out[0], sizeof(snd_out[0]));
	snd_cur = 1;
	if (ASND_SetVoice(0, VOICE_STEREO_16BIT, rate, 0, snd_out[0], sizeof(snd_out[0]),
		MAX_VOLUME, MAX_VOLUME, snd_voice_cb) != SND_OK)
		return false;
	snd_open = true;
	return true;
}

void plat_audio_close(void)
{
	if (snd_open) {
		ASND_StopVoice(0);
		snd_open = false;
	}
}

/* ======================================================================
	Start and quit
   ====================================================================== */

bool plat_init(int argc, char **argv)
{
	SYS_SetPowerCallback(power_button_cb);
	SYS_SetResetCallback(reset_button_cb);

	fatInitDefault();
	set_app_dir(argc, argv);

	if (!screen_init())
		return false;

	INPUT_Init();
	INPUT_SetScreenSize(VIRT_W, VIRT_H);
	INPUT_SetCursorMode(INPUT_CURSOR_IR | INPUT_CURSOR_MOUSE);
	// the keyboard is a Mac keyboard here, not a controller
	INPUT_KeyboardMapClear();
	WPAD_SetPowerButtonCallback(wiimote_power_cb);

	ASND_Init();
	ASND_Pause(0);
	return true;
}

void plat_exit(void)
{
}

void plat_quit(bool power_off)
{
	plat_audio_close();
	ASND_End();
	INPUT_Shutdown();
	screen_exit();
	if (power_off || shutdown_requested)
		SYS_ResetSystem(SYS_POWEROFF, 0, 0);
	exit(0);
}
