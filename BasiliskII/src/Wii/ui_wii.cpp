/*
 *  ui_wii.cpp - controls, menus and drawing, Wii / Wii U
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Runs once per 60Hz tick from the CPU loop: reads the controllers, USB
 *  keyboard and mouse, sends ADB events to the Mac and draws the Mac
 *  screen with whatever is on top of it (HOME menu, file browser, Mac key
 *  overlay, the libwiiosk text keyboard, update popup). While a menu, the
 *  browser or the text keyboard is open the emulated Mac waits: ui_tick()
 *  does not return until they are closed.
 *
 *  Controls (Wii Remote; the Wii U GamePad, Classic and Pro controllers and
 *  GameCube pads map onto the same buttons through libwiiinput /
 *  libwiiuinput):
 *    pointer / touch / USB mouse   Mac mouse          A, B, Z   click
 *    D-Pad                         arrow keys         stick     move mouse
 *    1 or C                        command            2         option
 *    +                             type text (libwiiosk)
 *    -                             display mode       HOME      menu
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "adb.h"
#include "prefs.h"
#include "macos_util.h"
#include "plat.h"
#include "wii_glue.h"

#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#include <ctype.h>

#define DEBUG 0
#include "debug.h"

#ifndef APP_TITLE
#define APP_TITLE "Basilisk II"
#endif
#ifndef APP_VERSION
#define APP_VERSION "1.0.0"
#endif

/* ======================================================================
	ADB key codes (raw ADB, as Basilisk II's ADBKeyDown() takes them)
   ====================================================================== */

enum {
	K_A = 0x00, K_S = 0x01, K_D = 0x02, K_F = 0x03, K_H = 0x04, K_G = 0x05,
	K_Z = 0x06, K_X = 0x07, K_C = 0x08, K_V = 0x09, K_ISO = 0x0a, K_B = 0x0b,
	K_Q = 0x0c, K_W = 0x0d, K_E = 0x0e, K_R = 0x0f, K_Y = 0x10, K_T = 0x11,
	K_1 = 0x12, K_2 = 0x13, K_3 = 0x14, K_4 = 0x15, K_6 = 0x16, K_5 = 0x17,
	K_Equal = 0x18, K_9 = 0x19, K_7 = 0x1a, K_Minus = 0x1b, K_8 = 0x1c,
	K_0 = 0x1d, K_RBracket = 0x1e, K_O = 0x1f, K_U = 0x20, K_LBracket = 0x21,
	K_I = 0x22, K_P = 0x23, K_Return = 0x24, K_L = 0x25, K_J = 0x26,
	K_Quote = 0x27, K_K = 0x28, K_Semicolon = 0x29, K_Backslash = 0x2a,
	K_Comma = 0x2b, K_Slash = 0x2c, K_N = 0x2d, K_M = 0x2e, K_Period = 0x2f,
	K_Tab = 0x30, K_Space = 0x31, K_Grave = 0x32, K_Delete = 0x33,
	K_Escape = 0x35, K_Control = 0x36, K_Command = 0x37, K_Shift = 0x38,
	K_CapsLock = 0x39, K_Option = 0x3a, K_Left = 0x3b, K_Right = 0x3c,
	K_Down = 0x3d, K_Up = 0x3e,
	K_KPDecimal = 0x41, K_KPMultiply = 0x43, K_KPAdd = 0x45, K_Clear = 0x47,
	K_KPDivide = 0x4b, K_Enter = 0x4c, K_KPSubtract = 0x4e, K_KPEqual = 0x51,
	K_KP0 = 0x52, K_KP1 = 0x53, K_KP2 = 0x54, K_KP3 = 0x55, K_KP4 = 0x56,
	K_KP5 = 0x57, K_KP6 = 0x58, K_KP7 = 0x59, K_KP8 = 0x5b, K_KP9 = 0x5c,
	K_F5 = 0x60, K_F6 = 0x61, K_F7 = 0x62, K_F3 = 0x63, K_F8 = 0x64,
	K_F9 = 0x65, K_F11 = 0x67, K_F13 = 0x69, K_F14 = 0x6b, K_F10 = 0x6d,
	K_F12 = 0x6f, K_F15 = 0x71, K_Help = 0x72, K_Home = 0x73, K_PageUp = 0x74,
	K_FwdDelete = 0x75, K_F4 = 0x76, K_End = 0x77, K_F2 = 0x78,
	K_PageDown = 0x79, K_F1 = 0x7a,
	K_None = 0xff
};

/* ======================================================================
	Drawing helpers
   ====================================================================== */

#define WHITE RGBA(255, 255, 255, 255)
#define BLACK RGBA(0, 0, 0, 255)
#define PANEL RGBA(235, 235, 240, 245)
#define GREY RGBA(70, 70, 70, 255)

static void frame_rect(float x, float y, float w, float h, float t, uint32 c)
{
	plat_fill_rect(x, y, w, t, c);
	plat_fill_rect(x, y + h - t, w, t, c);
	plat_fill_rect(x, y, t, h, c);
	plat_fill_rect(x + w - t, y, t, h, c);
}

static float text_width(const char *s)
{
	return 8.0f * strlen(s);
}

static void text_centered(float cx, float y, const char *s, uint32 c)
{
	plat_text(cx - text_width(s) / 2, y, s, c);
}

static void draw_pointer(float x, float y)
{
	plat_fill_tri(x - 2, y - 3, x - 2, y + 21, x + 15, y + 15, BLACK);
	plat_fill_tri(x, y, x, y + 16, x + 11, y + 12, WHITE);
}

/* ======================================================================
	State
   ====================================================================== */

static bool menu_open = false;
static bool browser_open = false;
static bool keys_visible = false;	// Mac key overlay
static bool type_open = false;		// libwiiosk text keyboard
static bool popup_open = false;
static plat_popup popup;

static char toast_text[80];
static uint32 toast_until = 0;
static bool overlay_shown = false;

static bool pointer_active = false;
static int pointer_x = 0, pointer_y = 0;
static bool cursor_moved = false;
static uint8 mouse_pressed = 0, mouse_held = 0;
static int mouse_wheel = 0;

static bool quit_wanted = false;

enum { SCALE_FILL = 0, SCALE_ASPECT, SCALE_INTEGER, NUM_SCALES };
static const char *const scale_names[NUM_SCALES] = {
	"Fill screen", "Keep aspect", "Integer scale"
};
static int scale_mode = SCALE_ASPECT;
static bool smoothing = true;

// where the Mac screen is drawn
static float disp_x = 0, disp_y = 0, disp_w = VIRT_W, disp_h = VIRT_H;
static int disp_for_w = 0, disp_for_h = 0, disp_for_mode = -1;

static void show_toast(const char *s)
{
	snprintf(toast_text, sizeof(toast_text), "%s", s);
	toast_until = plat_frame() + 150;
}

static bool toast_visible(void)
{
	return (int32)(toast_until - plat_frame()) > 0;
}

static bool ui_pauses_mac(void)
{
	return menu_open || browser_open || popup_open || type_open;
}

static void update_display_rect(void)
{
	int mw = video_width(), mh = video_height();
	if (mw <= 0 || mh <= 0)
		return;
	if (mw == disp_for_w && mh == disp_for_h && scale_mode == disp_for_mode)
		return;
	disp_for_w = mw;
	disp_for_h = mh;
	disp_for_mode = scale_mode;

	// how much wider a virtual pixel is than tall on this TV
	float aspect = plat_widescreen() ? 16.0f / 9.0f : 4.0f / 3.0f;
	float k = aspect / ((float)VIRT_W / VIRT_H);
	float s = VIRT_W * k / mw;
	if ((float)VIRT_H / mh < s)
		s = (float)VIRT_H / mh;

	switch (scale_mode) {
	case SCALE_INTEGER:
		if (s >= 1.0f)
			s = floorf(s);
		break;
	case SCALE_FILL:
		disp_x = disp_y = 0;
		disp_w = VIRT_W;
		disp_h = VIRT_H;
		return;
	default:
		break;
	}
	disp_w = mw * s / k;
	disp_h = mh * s;
	disp_x = (VIRT_W - disp_w) / 2;
	disp_y = (VIRT_H - disp_h) / 2;
}

static void set_scale_mode(int m)
{
	scale_mode = (m + NUM_SCALES) % NUM_SCALES;
	disp_for_mode = -1;
	update_display_rect();
}

static void save_settings(void)
{
	PrefsReplaceInt32("displaymode", scale_mode);
	PrefsReplaceBool("smoothing", smoothing);
	SaveDisplayPrefs();
}

/* ======================================================================
	Mac keys, merged from several sources
   ====================================================================== */

enum {
	SRC_PAD = 1 << 0,
	SRC_USB = 1 << 1,
	SRC_KEYS = 1 << 2,	// the Mac key overlay
	SRC_TYPE = 1 << 3	// typing text
};

static uint8 key_sources[128];

static void mac_key(int code, int src, bool down)
{
	if (code < 0 || code >= 128)
		return;
	uint8 old = key_sources[code];
	uint8 nw = down ? (old | src) : (old & ~src);
	key_sources[code] = nw;
	if ((old != 0) != (nw != 0)) {
		if (nw)
			ADBKeyDown(code);
		else
			ADBKeyUp(code);
	}
}

static void mac_keys_release(int src)
{
	for (int i = 0; i < 128; i++)
		if (key_sources[i] & src)
			mac_key(i, src, false);
}

/* --- USB keyboard (HID usages) --- */

static uint8 hid_to_adb[256];
static bool hid_was_down[256];
static bool caps_locked = false;

static void init_hid_map(void)
{
	static const uint8 letters[26] = {
		K_A, K_B, K_C, K_D, K_E, K_F, K_G, K_H, K_I, K_J, K_K, K_L, K_M,
		K_N, K_O, K_P, K_Q, K_R, K_S, K_T, K_U, K_V, K_W, K_X, K_Y, K_Z
	};
	static const uint8 digits[10] = {
		K_1, K_2, K_3, K_4, K_5, K_6, K_7, K_8, K_9, K_0
	};
	static const uint8 keypad[10] = {
		K_KP1, K_KP2, K_KP3, K_KP4, K_KP5, K_KP6, K_KP7, K_KP8, K_KP9, K_KP0
	};
	static const uint8 fkeys[12] = {
		K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12
	};

	memset(hid_to_adb, K_None, sizeof(hid_to_adb));
	for (int i = 0; i < 26; i++)
		hid_to_adb[0x04 + i] = letters[i];
	for (int i = 0; i < 10; i++) {
		hid_to_adb[0x1e + i] = digits[i];
		hid_to_adb[0x59 + i] = keypad[i];
	}
	for (int i = 0; i < 12; i++)
		hid_to_adb[0x3a + i] = fkeys[i];

	hid_to_adb[0x28] = K_Return;
	hid_to_adb[0x29] = K_Escape;
	hid_to_adb[0x2a] = K_Delete;
	hid_to_adb[0x2b] = K_Tab;
	hid_to_adb[0x2c] = K_Space;
	hid_to_adb[0x2d] = K_Minus;
	hid_to_adb[0x2e] = K_Equal;
	hid_to_adb[0x2f] = K_LBracket;
	hid_to_adb[0x30] = K_RBracket;
	hid_to_adb[0x31] = K_Backslash;
	hid_to_adb[0x32] = K_Backslash;
	hid_to_adb[0x33] = K_Semicolon;
	hid_to_adb[0x34] = K_Quote;
	hid_to_adb[0x35] = K_Grave;
	hid_to_adb[0x36] = K_Comma;
	hid_to_adb[0x37] = K_Period;
	hid_to_adb[0x38] = K_Slash;
	hid_to_adb[0x39] = K_CapsLock;

	hid_to_adb[0x46] = K_F13;
	hid_to_adb[0x47] = K_F14;
	hid_to_adb[0x48] = K_F15;
	hid_to_adb[0x49] = K_Help;
	hid_to_adb[0x4a] = K_Home;
	hid_to_adb[0x4b] = K_PageUp;
	hid_to_adb[0x4c] = K_FwdDelete;
	hid_to_adb[0x4d] = K_End;
	hid_to_adb[0x4e] = K_PageDown;
	hid_to_adb[0x4f] = K_Right;
	hid_to_adb[0x50] = K_Left;
	hid_to_adb[0x51] = K_Down;
	hid_to_adb[0x52] = K_Up;

	hid_to_adb[0x53] = K_Clear;
	hid_to_adb[0x54] = K_KPDivide;
	hid_to_adb[0x55] = K_KPMultiply;
	hid_to_adb[0x56] = K_KPSubtract;
	hid_to_adb[0x57] = K_KPAdd;
	hid_to_adb[0x58] = K_Enter;
	hid_to_adb[0x63] = K_KPDecimal;
	hid_to_adb[0x64] = K_ISO;
	hid_to_adb[0x67] = K_KPEqual;

	// PC layout: Alt sits where command is on a Mac keyboard
	hid_to_adb[0xe0] = K_Control;
	hid_to_adb[0xe1] = K_Shift;
	hid_to_adb[0xe2] = K_Command;
	hid_to_adb[0xe3] = K_Option;
	hid_to_adb[0xe4] = K_Control;
	hid_to_adb[0xe5] = K_Shift;
	hid_to_adb[0xe6] = K_Command;
	hid_to_adb[0xe7] = K_Option;
}

static void update_usb_keyboard(bool to_mac)
{
	for (int i = 4; i < 256; i++) {
		bool down = INPUT_KeyHeld(i);
		if (down == hid_was_down[i])
			continue;
		hid_was_down[i] = down;

		int code = hid_to_adb[i];
		if (code == K_None)
			continue;

		if (code == K_CapsLock) {
			// the Mac's caps lock key latches down
			if (down && to_mac) {
				caps_locked = !caps_locked;
				mac_key(K_CapsLock, SRC_USB, caps_locked);
				INPUT_KeyboardSetLed(INPUT_LED_CAPS, caps_locked);
			}
			continue;
		}
		if (down) {
			if (to_mac)
				mac_key(code, SRC_USB, true);
		} else
			mac_key(code, SRC_USB, false);
	}
}

/* ======================================================================
	Mac key overlay (special keys: command, option, escape, arrows...)
   ====================================================================== */

struct overlay_key {
	float x, w;		// in key units
	uint8 row;
	const char *lo, *hi;
	uint8 code;
};

#define KEY_UNIT 40.0f
#define KEY_ROW_H 34.0f
#define KEYS_X0 ((VIRT_W - 15 * KEY_UNIT) / 2)
#define KEYS_Y0 (VIRT_H - 5 * KEY_ROW_H - 14.0f)

static const overlay_key overlay_keys[] = {
	{ 0, 1, 0, "`", "~", K_Grave }, { 1, 1, 0, "1", "!", K_1 },
	{ 2, 1, 0, "2", "@", K_2 }, { 3, 1, 0, "3", "#", K_3 },
	{ 4, 1, 0, "4", "$", K_4 }, { 5, 1, 0, "5", "%", K_5 },
	{ 6, 1, 0, "6", "^", K_6 }, { 7, 1, 0, "7", "&", K_7 },
	{ 8, 1, 0, "8", "*", K_8 }, { 9, 1, 0, "9", "(", K_9 },
	{ 10, 1, 0, "0", ")", K_0 }, { 11, 1, 0, "-", "_", K_Minus },
	{ 12, 1, 0, "=", "+", K_Equal }, { 13, 2, 0, "delete", "delete", K_Delete },

	{ 0, 1.5f, 1, "tab", "tab", K_Tab }, { 1.5f, 1, 1, "q", "Q", K_Q },
	{ 2.5f, 1, 1, "w", "W", K_W }, { 3.5f, 1, 1, "e", "E", K_E },
	{ 4.5f, 1, 1, "r", "R", K_R }, { 5.5f, 1, 1, "t", "T", K_T },
	{ 6.5f, 1, 1, "y", "Y", K_Y }, { 7.5f, 1, 1, "u", "U", K_U },
	{ 8.5f, 1, 1, "i", "I", K_I }, { 9.5f, 1, 1, "o", "O", K_O },
	{ 10.5f, 1, 1, "p", "P", K_P }, { 11.5f, 1, 1, "[", "{", K_LBracket },
	{ 12.5f, 1, 1, "]", "}", K_RBracket }, { 13.5f, 1.5f, 1, "\\", "|", K_Backslash },

	{ 0, 1.75f, 2, "caps", "caps", K_CapsLock }, { 1.75f, 1, 2, "a", "A", K_A },
	{ 2.75f, 1, 2, "s", "S", K_S }, { 3.75f, 1, 2, "d", "D", K_D },
	{ 4.75f, 1, 2, "f", "F", K_F }, { 5.75f, 1, 2, "g", "G", K_G },
	{ 6.75f, 1, 2, "h", "H", K_H }, { 7.75f, 1, 2, "j", "J", K_J },
	{ 8.75f, 1, 2, "k", "K", K_K }, { 9.75f, 1, 2, "l", "L", K_L },
	{ 10.75f, 1, 2, ";", ":", K_Semicolon }, { 11.75f, 1, 2, "'", "\"", K_Quote },
	{ 12.75f, 2.25f, 2, "return", "return", K_Return },

	{ 0, 2.25f, 3, "shift", "shift", K_Shift }, { 2.25f, 1, 3, "z", "Z", K_Z },
	{ 3.25f, 1, 3, "x", "X", K_X }, { 4.25f, 1, 3, "c", "C", K_C },
	{ 5.25f, 1, 3, "v", "V", K_V }, { 6.25f, 1, 3, "b", "B", K_B },
	{ 7.25f, 1, 3, "n", "N", K_N }, { 8.25f, 1, 3, "m", "M", K_M },
	{ 9.25f, 1, 3, ",", "<", K_Comma }, { 10.25f, 1, 3, ".", ">", K_Period },
	{ 11.25f, 1, 3, "/", "?", K_Slash }, { 12.25f, 2.75f, 3, "shift", "shift", K_Shift },

	{ 0, 1.5f, 4, "ctrl", "ctrl", K_Control }, { 1.5f, 1.5f, 4, "option", "option", K_Option },
	{ 3, 1.5f, 4, "cmd", "cmd", K_Command }, { 4.5f, 5.5f, 4, "", "", K_Space },
	{ 10, 1, 4, "esc", "esc", K_Escape }, { 11, 1, 4, "\x1b", "\x1b", K_Left },
	{ 12, 1, 4, "\x19", "\x19", K_Down }, { 13, 1, 4, "\x18", "\x18", K_Up },
	{ 14, 1, 4, "\x1a", "\x1a", K_Right },
};

#define NUM_OVERLAY_KEYS ((int)(sizeof(overlay_keys) / sizeof(overlay_keys[0])))

static int key_pressed = -1;		// key held by the pointer
static bool key_shift = false, key_option = false, key_command = false,
	key_control = false;			// one-shot modifiers
static bool key_caps = false;		// lock

static void overlay_key_rect(int i, float *x, float *y, float *w, float *h)
{
	*x = KEYS_X0 + overlay_keys[i].x * KEY_UNIT + 2;
	*y = KEYS_Y0 + overlay_keys[i].row * KEY_ROW_H + 2;
	*w = overlay_keys[i].w * KEY_UNIT - 4;
	*h = KEY_ROW_H - 4;
}

static bool point_in_keys(int x, int y)
{
	return keys_visible && x >= KEYS_X0 - 10 && x < KEYS_X0 + 15 * KEY_UNIT + 10
		&& y >= KEYS_Y0 - 10 && y < KEYS_Y0 + 5 * KEY_ROW_H + 10;
}

static int overlay_key_at(int px, int py)
{
	for (int i = 0; i < NUM_OVERLAY_KEYS; i++) {
		float x, y, w, h;
		overlay_key_rect(i, &x, &y, &w, &h);
		if (px >= x - 2 && px < x + w + 2 && py >= y - 2 && py < y + h + 2)
			return i;
	}
	return -1;
}

static bool *modifier_flag(int code)
{
	switch (code) {
	case K_Shift: return &key_shift;
	case K_Option: return &key_option;
	case K_Command: return &key_command;
	case K_Control: return &key_control;
	case K_CapsLock: return &key_caps;
	default: return NULL;
	}
}

static void release_one_shots(void)
{
	static const uint8 mods[] = { K_Shift, K_Option, K_Command, K_Control };
	for (unsigned i = 0; i < sizeof(mods); i++) {
		bool *f = modifier_flag(mods[i]);
		if (*f) {
			*f = false;
			mac_key(mods[i], SRC_KEYS, false);
		}
	}
}

static void overlay_press(int i)
{
	int code = overlay_keys[i].code;
	bool *flag = modifier_flag(code);
	if (flag) {
		*flag = !*flag;
		mac_key(code, SRC_KEYS, *flag);
	} else {
		key_pressed = i;
		mac_key(code, SRC_KEYS, true);
	}
}

static void overlay_release(void)
{
	if (key_pressed >= 0) {
		mac_key(overlay_keys[key_pressed].code, SRC_KEYS, false);
		key_pressed = -1;
		release_one_shots();
	}
}

static void overlay_hide(void)
{
	overlay_release();
	release_one_shots();
	keys_visible = false;
}

static void draw_overlay_keys(void)
{
	int hover = pointer_active ? overlay_key_at(pointer_x, pointer_y) : -1;
	bool shifted = key_shift || key_sources[K_Shift] != 0;
	bool upper = shifted || key_caps || caps_locked;

	plat_fill_rect(KEYS_X0 - 10, KEYS_Y0 - 10, 15 * KEY_UNIT + 20, 5 * KEY_ROW_H + 20,
		RGBA(24, 24, 32, 215));

	for (int i = 0; i < NUM_OVERLAY_KEYS; i++) {
		const overlay_key *k = &overlay_keys[i];
		bool *flag = modifier_flag(k->code);
		bool lit = i == key_pressed || (flag && *flag);
		uint32 bg = lit ? RGBA(90, 140, 230, 240)
			: i == hover ? RGBA(120, 120, 135, 240)
			: RGBA(70, 70, 80, 240);
		float x, y, w, h;
		overlay_key_rect(i, &x, &y, &w, &h);
		plat_fill_rect(x, y, w, h, bg);

		const char *label;
		if (strlen(k->lo) == 1 && k->lo[0] >= 'a' && k->lo[0] <= 'z')
			label = upper ? k->hi : k->lo;
		else
			label = shifted ? k->hi : k->lo;
		text_centered(x + w / 2, y + (h - 16) / 2, label, WHITE);
	}
}

/* ======================================================================
	Typing text (libwiiosk)

	The + button opens the software keyboard from libwiiosk. When it is
	confirmed, the text is typed into the Mac one key stroke at a time, as
	a US keyboard would: shift for capitals and symbols, option for the
	Mac Roman extras and option dead keys for accented letters.
   ====================================================================== */

#define TS_SHIFT 1
#define TS_OPTION 2

struct type_stroke {
	uint8 code;
	uint8 mods;
};

#define TYPE_QUEUE_SIZE 4096
static type_stroke type_queue[TYPE_QUEUE_SIZE];
static int type_in = 0, type_out = 0;
static int type_phase = 0;		// 0 idle, 1 stroke down, 2 released
static type_stroke type_cur;

static osk_keyboard *type_osk = NULL;

static bool type_push(uint8 code, uint8 mods)
{
	int next = (type_in + 1) % TYPE_QUEUE_SIZE;
	if (next == type_out)
		return false;
	type_queue[type_in].code = code;
	type_queue[type_in].mods = mods;
	type_in = next;
	return true;
}

/* US layout: printable ASCII to a key and whether shift is needed */
static bool ascii_stroke(uint32 c, uint8 *code, uint8 *mods)
{
	static const char unshifted[] = "`1234567890-=[]\\;',./";
	static const char shifted[] = "~!@#$%^&*()_+{}|:\"<>?";
	static const uint8 keys[] = {
		K_Grave, K_1, K_2, K_3, K_4, K_5, K_6, K_7, K_8, K_9, K_0, K_Minus,
		K_Equal, K_LBracket, K_RBracket, K_Backslash, K_Semicolon, K_Quote,
		K_Comma, K_Period, K_Slash
	};
	static const uint8 letters[26] = {
		K_A, K_B, K_C, K_D, K_E, K_F, K_G, K_H, K_I, K_J, K_K, K_L, K_M,
		K_N, K_O, K_P, K_Q, K_R, K_S, K_T, K_U, K_V, K_W, K_X, K_Y, K_Z
	};

	*mods = 0;
	if (c >= 'a' && c <= 'z') {
		*code = letters[c - 'a'];
		return true;
	}
	if (c >= 'A' && c <= 'Z') {
		*code = letters[c - 'A'];
		*mods = TS_SHIFT;
		return true;
	}
	switch (c) {
	case ' ': *code = K_Space; return true;
	case '\n': *code = K_Return; return true;
	case '\t': *code = K_Tab; return true;
	default: break;
	}
	if (c > ' ' && c < 0x7f) {
		const char *p = strchr(unshifted, (int)c);
		if (p) {
			*code = keys[p - unshifted];
			return true;
		}
		p = strchr(shifted, (int)c);
		if (p) {
			*code = keys[p - shifted];
			*mods = TS_SHIFT;
			return true;
		}
	}
	return false;
}

/* Characters outside ASCII that a US Mac keyboard types: one option
   stroke (base == 0) or an option dead key followed by the base letter. */
struct type_extra {
	uint16 cp;
	char key;		// ASCII key pressed with option (shifted form = shift)
	char base;		// letter after the dead key, or 0
};

static const type_extra type_extras[] = {
	// option dead keys: acute e, grave `, circumflex i, umlaut u, tilde n
	{ 0xE1, 'e', 'a' }, { 0xE9, 'e', 'e' }, { 0xED, 'e', 'i' },
	{ 0xF3, 'e', 'o' }, { 0xFA, 'e', 'u' }, { 0xC1, 'e', 'A' },
	{ 0xC9, 'e', 'E' }, { 0xCD, 'e', 'I' }, { 0xD3, 'e', 'O' },
	{ 0xDA, 'e', 'U' },
	{ 0xE0, '`', 'a' }, { 0xE8, '`', 'e' }, { 0xEC, '`', 'i' },
	{ 0xF2, '`', 'o' }, { 0xF9, '`', 'u' }, { 0xC0, '`', 'A' },
	{ 0xC8, '`', 'E' }, { 0xCC, '`', 'I' }, { 0xD2, '`', 'O' },
	{ 0xD9, '`', 'U' },
	{ 0xE2, 'i', 'a' }, { 0xEA, 'i', 'e' }, { 0xEE, 'i', 'i' },
	{ 0xF4, 'i', 'o' }, { 0xFB, 'i', 'u' }, { 0xC2, 'i', 'A' },
	{ 0xCA, 'i', 'E' }, { 0xCE, 'i', 'I' }, { 0xD4, 'i', 'O' },
	{ 0xDB, 'i', 'U' },
	{ 0xE4, 'u', 'a' }, { 0xEB, 'u', 'e' }, { 0xEF, 'u', 'i' },
	{ 0xF6, 'u', 'o' }, { 0xFC, 'u', 'u' }, { 0xFF, 'u', 'y' },
	{ 0xC4, 'u', 'A' }, { 0xCB, 'u', 'E' }, { 0xCF, 'u', 'I' },
	{ 0xD6, 'u', 'O' }, { 0xDC, 'u', 'U' }, { 0x178, 'u', 'Y' },
	{ 0xE3, 'n', 'a' }, { 0xF1, 'n', 'n' }, { 0xF5, 'n', 'o' },
	{ 0xC3, 'n', 'A' }, { 0xD1, 'n', 'N' }, { 0xD5, 'n', 'O' },

	// single option strokes
	{ 0xE7, 'c', 0 }, { 0xC7, 'C', 0 }, { 0xDF, 's', 0 },
	{ 0xE5, 'a', 0 }, { 0xC5, 'A', 0 }, { 0xE6, '\'', 0 },
	{ 0xC6, '"', 0 }, { 0xF8, 'o', 0 }, { 0xD8, 'O', 0 },
	{ 0x153, 'q', 0 }, { 0x152, 'Q', 0 }, { 0xBF, '?', 0 },
	{ 0xA1, '1', 0 }, { 0xA3, '3', 0 }, { 0xA5, 'y', 0 },
	{ 0xA9, 'g', 0 }, { 0xAE, 'r', 0 }, { 0x2122, '2', 0 },
	{ 0xB0, '*', 0 }, { 0x2022, '8', 0 }, { 0xA2, '4', 0 },
	{ 0xA7, '6', 0 }, { 0xB6, '7', 0 }, { 0xAB, '\\', 0 },
	{ 0xBB, '|', 0 }, { 0x201C, '[', 0 }, { 0x201D, '{', 0 },
	{ 0x2018, ']', 0 }, { 0x2019, '}', 0 }, { 0x2026, ';', 0 },
	{ 0x2013, '-', 0 }, { 0x2014, '_', 0 }, { 0xB1, '+', 0 },
	{ 0xF7, '/', 0 }, { 0xB5, 'm', 0 }, { 0xAC, 'l', 0 },
	{ 0x3C0, 'p', 0 }, { 0x221E, '5', 0 }, { 0x2260, '=', 0 },
	{ 0x2264, ',', 0 }, { 0x2265, '.', 0 }, { 0x20AC, '@', 0 },
	{ 0xA0, ' ', 0 }
};

#define NUM_TYPE_EXTRAS ((int)(sizeof(type_extras) / sizeof(type_extras[0])))

/* Queue the strokes for one code point; unknown characters are skipped */
static void type_code_point(uint32 cp)
{
	uint8 code, mods;

	if (cp == '\r')
		return;
	if (ascii_stroke(cp, &code, &mods)) {
		type_push(code, mods);
		return;
	}
	for (int i = 0; i < NUM_TYPE_EXTRAS; i++) {
		if (type_extras[i].cp != cp)
			continue;
		if (ascii_stroke((uint8)type_extras[i].key, &code, &mods))
			type_push(code, mods | TS_OPTION);
		if (type_extras[i].base && ascii_stroke((uint8)type_extras[i].base, &code, &mods))
			type_push(code, mods);
		return;
	}
}

static void type_utf8(const char *s)
{
	const uint8 *p = (const uint8 *)s;

	while (*p) {
		uint32 cp = *p++;
		int more = 0;
		if (cp >= 0xf0) {
			cp &= 0x07;
			more = 3;
		} else if (cp >= 0xe0) {
			cp &= 0x0f;
			more = 2;
		} else if (cp >= 0xc0) {
			cp &= 0x1f;
			more = 1;
		} else if (cp >= 0x80)
			continue;	// stray continuation byte
		for (; more > 0 && (*p & 0xc0) == 0x80; more--)
			cp = (cp << 6) | (*p++ & 0x3f);
		if (more == 0)
			type_code_point(cp);
	}
}

static bool type_pending(void)
{
	return type_phase != 0 || type_in != type_out;
}

static void type_set_mods(uint8 mods, bool down)
{
	if (mods & TS_SHIFT)
		mac_key(K_Shift, SRC_TYPE, down);
	if (mods & TS_OPTION)
		mac_key(K_Option, SRC_TYPE, down);
}

/* Once per tick: press a stroke, release it on the next tick and leave a
   tick free, so the Mac's ADB buffer (16 events, emptied every tick)
   never overflows: 20 characters a second. */
static void type_step(void)
{
	switch (type_phase) {
	case 1:
		mac_key(type_cur.code, SRC_TYPE, false);
		type_set_mods(type_cur.mods, false);
		type_phase = 2;
		return;
	case 2:
		type_phase = 0;
		return;
	}
	if (type_in == type_out)
		return;
	type_cur = type_queue[type_out];
	type_out = (type_out + 1) % TYPE_QUEUE_SIZE;
	type_set_mods(type_cur.mods, true);
	mac_key(type_cur.code, SRC_TYPE, true);
	type_phase = 1;
}

static void type_cancel(void)
{
	type_in = type_out = 0;
	type_phase = 0;
	mac_keys_release(SRC_TYPE);
}

static void type_open_keyboard(void)
{
	if (type_open)
		return;

	osk_config cfg;
	OSK_DefaultConfig(&cfg);
	OSK_UseSystemLanguage(&cfg);
	plat_osk_config(&cfg);
	cfg.title = "Type into the Mac";
	cfg.max_chars = 2048;
	cfg.flags |= OSK_FLAG_MULTILINE | OSK_FLAG_PREDICT_OFF;

	type_osk = OSK_Create(&cfg);
	if (type_osk == NULL) {
		show_toast("Could not open the keyboard");
		return;
	}

	// keys typed on a USB keyboard before now are not for the box
	input_key_event ev;
	while (INPUT_KeyPop(&ev)) {
	}

	if (keys_visible)
		overlay_hide();
	type_open = true;
}

static void type_close_keyboard(bool type_it)
{
	if (!type_open)
		return;
	if (type_it)
		type_utf8(OSK_GetText(type_osk));
	OSK_Destroy(type_osk);
	type_osk = NULL;
	type_open = false;
}

static void type_update_keyboard(void)
{
	osk_input in;
	plat_osk_read(&in);
	osk_status st = OSK_Update(type_osk, &in);
	if (st == OSK_STATUS_DONE)
		type_close_keyboard(true);
	else if (st == OSK_STATUS_CANCELLED)
		type_close_keyboard(false);
}

/* ======================================================================
	File browser (disk images, CD images, the ROM)
   ====================================================================== */

#define BROWSER_MAX 512
#define BROWSER_NAME 256
#define BROWSER_LINES 18

struct browser_entry {
	char name[BROWSER_NAME];
	bool is_dir;
};

static browser_entry *entries = NULL;
static int entry_count = 0, entry_sel = 0, entry_top = 0;
static char browser_path[512];		// "" lists the devices
static const char *browser_title = "Insert disk image";
static bool browser_choosing = false;	// ui_choose_file()
static char browser_choice[768];
static bool browser_chosen = false;

static int compare_entries(const void *a, const void *b)
{
	const browser_entry *A = (const browser_entry *)a, *B = (const browser_entry *)b;
	if (A->is_dir != B->is_dir)
		return A->is_dir ? -1 : 1;
	return strcasecmp(A->name, B->name);
}

/* the root of `path` and its label, or -1 */
static int path_root(const char *path)
{
	const char *const *labels;
	const char *const *roots = plat_roots(&labels);
	int best = -1;
	size_t best_len = 0;
	for (int i = 0; roots[i]; i++) {
		size_t n = strlen(roots[i]);
		if (strncmp(path, roots[i], n) == 0 && n > best_len) {
			best = i;
			best_len = n;
		}
	}
	return best;
}

static void display_path(const char *path, char *out, size_t n)
{
	const char *const *labels;
	const char *const *roots = plat_roots(&labels);
	int r = path_root(path);
	if (r >= 0)
		snprintf(out, n, "%s%s", labels[r], path + strlen(roots[r]));
	else
		snprintf(out, n, "%s", path);
}

void ui_display_path(const char *path, char *out, size_t n)
{
	display_path(path, out, n);
}

static void browser_list_devices(void)
{
	const char *const *labels;
	const char *const *roots = plat_roots(&labels);

	browser_path[0] = 0;
	entry_count = 0;
	for (int i = 0; roots[i] && entry_count < BROWSER_MAX; i++) {
		DIR *d = opendir(roots[i]);
		if (d) {
			closedir(d);
			snprintf(entries[entry_count].name, BROWSER_NAME, "%s", roots[i]);
			entries[entry_count].is_dir = true;
			entry_count++;
		}
	}
	entry_sel = entry_top = 0;
}

static void browser_populate(void)
{
	entry_count = entry_sel = entry_top = 0;

	DIR *d = opendir(browser_path);
	if (d == NULL) {
		browser_list_devices();
		return;
	}

	strcpy(entries[0].name, "..");
	entries[0].is_dir = true;
	entry_count = 1;

	struct dirent *e;
	while ((e = readdir(d)) != NULL && entry_count < BROWSER_MAX) {
		if (e->d_name[0] == '.')
			continue;
		browser_entry *be = &entries[entry_count];
		snprintf(be->name, BROWSER_NAME, "%s", e->d_name);
		if (e->d_type != DT_UNKNOWN)
			be->is_dir = e->d_type == DT_DIR;
		else {
			char full[768];
			struct stat st;
			snprintf(full, sizeof(full), "%s%s", browser_path, e->d_name);
			be->is_dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
		}
		entry_count++;
	}
	closedir(d);

	if (entry_count > 1)
		qsort(&entries[1], entry_count - 1, sizeof(browser_entry), compare_entries);
}

static bool browser_open_at(const char *dir)
{
	if (entries == NULL) {
		entries = (browser_entry *)malloc(BROWSER_MAX * sizeof(browser_entry));
		if (entries == NULL)
			return false;
	}
	snprintf(browser_path, sizeof(browser_path), "%s", dir);
	size_t n = strlen(browser_path);
	if (n && browser_path[n - 1] != '/' && n + 1 < sizeof(browser_path))
		strcat(browser_path, "/");
	browser_populate();
	browser_open = true;
	menu_open = false;
	return true;
}

static void browser_move(int delta)
{
	entry_sel += delta;
	if (entry_sel >= entry_count)
		entry_sel = entry_count - 1;
	if (entry_sel < 0)
		entry_sel = 0;
	if (entry_sel < entry_top)
		entry_top = entry_sel;
	if (entry_sel >= entry_top + BROWSER_LINES)
		entry_top = entry_sel - BROWSER_LINES + 1;
}

static void browser_up(void)
{
	if (browser_path[0] == 0)
		return;

	const char *const *labels;
	const char *const *roots = plat_roots(&labels);
	int r = path_root(browser_path);
	if (r >= 0 && strcmp(browser_path, roots[r]) == 0) {
		browser_list_devices();
		return;
	}

	// drop the last component ("a/b/c/" -> "a/b/")
	size_t n = strlen(browser_path);
	if (n && browser_path[n - 1] == '/')
		browser_path[--n] = 0;
	char *slash = strrchr(browser_path, '/');
	if (slash == NULL) {
		browser_list_devices();
		return;
	}
	slash[1] = 0;
	browser_populate();
}

static void insert_image(const char *path, const char *name)
{
	char msg[80];
	switch (SysInsertImage(path)) {
	case INSERT_DISK:
	case INSERT_CDROM:
		snprintf(msg, sizeof(msg), "Inserted %.60s", name);
		break;
	case INSERT_NO_DRIVE:
		snprintf(msg, sizeof(msg), "No free drive: eject a disk first");
		break;
	default:
		snprintf(msg, sizeof(msg), "Could not open %.50s", name);
		break;
	}
	show_toast(msg);
}

static void browser_activate(void)
{
	if (entry_sel < 0 || entry_sel >= entry_count)
		return;
	browser_entry *e = &entries[entry_sel];

	if (browser_path[0] == 0) {
		browser_open_at(e->name);
		return;
	}
	if (e->is_dir) {
		if (strcmp(e->name, "..") == 0)
			browser_up();
		else {
			size_t n = strlen(browser_path);
			snprintf(browser_path + n, sizeof(browser_path) - n, "%s/", e->name);
			browser_populate();
		}
		return;
	}

	char path[768];
	snprintf(path, sizeof(path), "%s%s", browser_path, e->name);
	browser_open = false;
	if (browser_choosing) {
		snprintf(browser_choice, sizeof(browser_choice), "%s", path);
		browser_chosen = true;
	} else
		insert_image(path, e->name);
}

#define BROWSER_X 40.0f
#define BROWSER_Y 30.0f
#define BROWSER_LINE_H 20.0f

static void draw_browser(void)
{
	const float x = BROWSER_X, y = BROWSER_Y, w = VIRT_W - 2 * BROWSER_X;
	const float h = 74 + BROWSER_LINES * BROWSER_LINE_H;
	char line[96], shown[512];

	plat_fill_rect(x, y, w, h, PANEL);
	frame_rect(x, y, w, h, 2, BLACK);
	plat_fill_rect(x, y, w, 24, BLACK);
	text_centered(VIRT_W / 2, y + 4, browser_title, WHITE);

	if (browser_path[0])
		display_path(browser_path, shown, sizeof(shown));
	else
		snprintf(shown, sizeof(shown), "Devices");
	int max_chars = (int)((w - 20) / 8);
	if ((int)strlen(shown) > max_chars)
		snprintf(line, sizeof(line), "...%s", shown + strlen(shown) - (max_chars - 3));
	else
		snprintf(line, sizeof(line), "%s", shown);
	plat_text(x + 10, y + 28, line, GREY);

	const char *const *labels;
	plat_roots(&labels);
	for (int i = 0; i < BROWSER_LINES && entry_top + i < entry_count; i++) {
		browser_entry *e = &entries[entry_top + i];
		float ly = y + 48 + i * BROWSER_LINE_H;
		bool sel = entry_top + i == entry_sel;
		if (sel)
			plat_fill_rect(x + 6, ly - 2, w - 12, BROWSER_LINE_H, BLACK);
		const char *name = e->name;
		if (browser_path[0] == 0) {
			int r = path_root(e->name);
			if (r >= 0)
				name = labels[r];
		}
		snprintf(line, sizeof(line), e->is_dir ? "[%.*s]" : "%.*s", max_chars - 4, name);
		plat_text(x + 12, ly, line, sel ? WHITE : BLACK);
	}

	text_centered(VIRT_W / 2, y + h - 22, "A: open   B: up   1: close", GREY);
}

static int browser_line_at(int px, int py)
{
	const float y = BROWSER_Y + 48;
	if (px < BROWSER_X + 6 || px >= VIRT_W - BROWSER_X - 6 || py < y - 2)
		return -1;
	int i = (int)((py - (y - 2)) / BROWSER_LINE_H);
	if (i >= BROWSER_LINES || entry_top + i >= entry_count)
		return -1;
	return entry_top + i;
}

/* ======================================================================
	HOME menu
   ====================================================================== */

enum {
	MENU_INSERT = 0,
	MENU_DISPLAY,
	MENU_SMOOTHING,
	MENU_TYPE,
	MENU_KEYS,
	MENU_INTERRUPT,
	MENU_QUIT,
	MENU_RESUME,
	NUM_MENU_ITEMS
};

static int menu_sel = 0;
static bool settings_changed = false;

#define MENU_W 460.0f
#define MENU_ITEM_H 26.0f
#define MENU_X ((VIRT_W - MENU_W) / 2)
#define MENU_Y 50.0f
#define MENU_ITEMS_Y (MENU_Y + 40)

static void menu_item_text(int i, char *buf, size_t n)
{
	switch (i) {
	case MENU_INSERT:
		snprintf(buf, n, "Insert disk or CD image...");
		break;
	case MENU_DISPLAY:
		snprintf(buf, n, "Display:  < %s >", scale_names[scale_mode]);
		break;
	case MENU_SMOOTHING:
		snprintf(buf, n, "Smoothing:  < %s >", smoothing ? "On" : "Off");
		break;
	case MENU_TYPE:
		snprintf(buf, n, "Type text...");
		break;
	case MENU_KEYS:
		snprintf(buf, n, "%s Mac keyboard (special keys)", keys_visible ? "Hide" : "Show");
		break;
	case MENU_INTERRUPT:
		snprintf(buf, n, "Interrupt (programmer's switch)");
		break;
	case MENU_QUIT:
		snprintf(buf, n, "Quit Basilisk II");
		break;
	default:
		snprintf(buf, n, "Resume");
		break;
	}
}

static void menu_close(void)
{
	menu_open = false;
	if (settings_changed) {
		save_settings();
		settings_changed = false;
	}
}

static void menu_change(int dir)
{
	switch (menu_sel) {
	case MENU_DISPLAY:
		set_scale_mode(scale_mode + dir);
		settings_changed = true;
		break;
	case MENU_SMOOTHING:
		smoothing = !smoothing;
		settings_changed = true;
		break;
	default:
		break;
	}
}

static void menu_activate(void)
{
	switch (menu_sel) {
	case MENU_INSERT:
		menu_close();
		browser_title = "Insert disk or CD image";
		browser_choosing = false;
		browser_open_at(plat_app_dir());
		break;
	case MENU_DISPLAY:
	case MENU_SMOOTHING:
		menu_change(1);
		break;
	case MENU_TYPE:
		menu_close();
		type_open_keyboard();
		break;
	case MENU_KEYS:
		if (keys_visible)
			overlay_hide();
		else
			keys_visible = true;
		menu_close();
		break;
	case MENU_INTERRUPT:
		menu_close();
		TriggerNMI();
		break;
	case MENU_QUIT:
		menu_close();
		quit_wanted = true;
		break;
	default:
		menu_close();
		break;
	}
}

static int menu_item_at(int px, int py)
{
	if (px < MENU_X || px >= MENU_X + MENU_W || py < MENU_ITEMS_Y)
		return -1;
	int i = (int)((py - MENU_ITEMS_Y) / MENU_ITEM_H);
	return i < NUM_MENU_ITEMS ? i : -1;
}

static void draw_menu(void)
{
	const float h = 40 + NUM_MENU_ITEMS * MENU_ITEM_H + 112;
	char buf[96];

	plat_fill_rect(MENU_X, MENU_Y, MENU_W, h, PANEL);
	frame_rect(MENU_X, MENU_Y, MENU_W, h, 2, BLACK);
	plat_fill_rect(MENU_X, MENU_Y, MENU_W, 26, BLACK);
	text_centered(VIRT_W / 2, MENU_Y + 5, APP_TITLE " " APP_VERSION, WHITE);

	for (int i = 0; i < NUM_MENU_ITEMS; i++) {
		float y = MENU_ITEMS_Y + i * MENU_ITEM_H;
		bool sel = i == menu_sel;
		if (sel)
			plat_fill_rect(MENU_X + 8, y, MENU_W - 16, MENU_ITEM_H - 2, BLACK);
		menu_item_text(i, buf, sizeof(buf));
		plat_text(MENU_X + 24, y + 4, buf, sel ? WHITE : BLACK);
	}

	float y = MENU_ITEMS_Y + NUM_MENU_ITEMS * MENU_ITEM_H + 10;
	plat_text(MENU_X + 16, y, "Point + A/B: mouse    D-Pad: arrow keys", GREY);
	plat_text(MENU_X + 16, y + 18, "1: command  2: option  -: display mode", GREY);
	plat_text(MENU_X + 16, y + 36, "+: type text  Stick: move mouse", GREY);
	plat_text(MENU_X + 16, y + 54, "USB keyboard and mouse work too.", GREY);
	plat_text(MENU_X + 16, y + 72, "Alt = command, Windows key = option", GREY);
}

/* ======================================================================
	Update popup (libwiiupdate / libwiiuupdate)
   ====================================================================== */

static void draw_popup(void)
{
	const float w = 520, h = 150;
	const float x = (VIRT_W - w) / 2, y = (VIRT_H - h) / 2;

	plat_fill_rect(x, y, w, h, RGBA(235, 235, 240, 250));
	frame_rect(x, y, w, h, 2, BLACK);
	plat_fill_rect(x, y, w, 26, BLACK);
	text_centered(VIRT_W / 2, y + 5, popup.title ? popup.title : "", WHITE);
	if (popup.text)
		text_centered(VIRT_W / 2, y + 40, popup.text, BLACK);
	if (popup.detail && popup.detail[0])
		text_centered(VIRT_W / 2, y + 62, popup.detail, GREY);
	if (popup.progress >= 0) {
		float bw = w - 60;
		frame_rect(x + 30, y + 86, bw, 18, 2, BLACK);
		plat_fill_rect(x + 32, y + 88, (bw - 4) * popup.progress / 100.0f, 14,
			RGBA(40, 90, 200, 255));
	}
	if (popup.hint && popup.hint[0])
		text_centered(VIRT_W / 2, y + h - 26, popup.hint, BLACK);
}

/* ======================================================================
	Alerts
   ====================================================================== */

static const char *alert_title, *alert_text, *alert_yes, *alert_no;
static bool alert_open = false;

#define ALERT_W 560.0f
#define ALERT_COLS 64

/* split the text into lines of at most ALERT_COLS characters */
static int wrap_text(const char *s, char lines[][ALERT_COLS + 1], int max_lines)
{
	int n = 0;
	while (*s && n < max_lines) {
		int len = 0, brk = -1;
		while (s[len] && s[len] != '\n' && len < ALERT_COLS) {
			if (s[len] == ' ')
				brk = len;
			len++;
		}
		if (s[len] && s[len] != '\n' && brk > 0)
			len = brk;
		memcpy(lines[n], s, len);
		lines[n][len] = 0;
		n++;
		s += len;
		while (*s == ' ')
			s++;
		if (*s == '\n')
			s++;
	}
	return n;
}

static void draw_alert(void)
{
	char lines[12][ALERT_COLS + 1];
	int n = wrap_text(alert_text ? alert_text : "", lines, 12);
	const float h = 80 + n * 18 + 30;
	const float x = (VIRT_W - ALERT_W) / 2, y = (VIRT_H - h) / 2;

	plat_fill_rect(x, y, ALERT_W, h, RGBA(235, 235, 240, 250));
	frame_rect(x, y, ALERT_W, h, 2, BLACK);
	plat_fill_rect(x, y, ALERT_W, 26, RGBA(40, 48, 72, 255));
	text_centered(VIRT_W / 2, y + 5, alert_title, WHITE);
	for (int i = 0; i < n; i++)
		plat_text(x + 20, y + 40 + i * 18, lines[i], BLACK);

	char hint[96];
	if (alert_no)
		snprintf(hint, sizeof(hint), "A: %s    B: %s", alert_yes, alert_no);
	else
		snprintf(hint, sizeof(hint), "A: %s", alert_yes);
	text_centered(VIRT_W / 2, y + h - 30, hint, BLACK);
}

/* ======================================================================
	Drawing a frame
   ====================================================================== */

static bool overlay_wanted(void)
{
	return menu_open || browser_open || keys_visible || popup_open || type_open
		|| alert_open || toast_visible();
}

static void render_frame(void)
{
	bool overlay = overlay_wanted();

	update_display_rect();

	plat_begin_frame();
	if (video_width() > 0) {
		bool exact = disp_w == video_width() && disp_h == video_height()
			&& !(plat_widescreen() && VIRT_W == 640);
		plat_draw_screen(disp_x, disp_y, disp_w, disp_h, smoothing && !exact);
	}

	if (keys_visible)
		draw_overlay_keys();
	if (menu_open)
		draw_menu();
	if (browser_open)
		draw_browser();
	if (type_open)
		plat_osk_draw(type_osk);
	if (alert_open)
		draw_alert();
	if (popup_open)
		draw_popup();
	if (toast_visible()) {
		float tw = text_width(toast_text) + 24;
		plat_fill_rect((VIRT_W - tw) / 2, 24, tw, 26, RGBA(0, 0, 0, 200));
		text_centered(VIRT_W / 2, 29, toast_text, WHITE);
	}
	if (pointer_active && (menu_open || browser_open || alert_open
		|| point_in_keys(pointer_x, pointer_y)))
		draw_pointer(pointer_x, pointer_y);

	plat_end_frame();
	overlay_shown = overlay;
}

/* ======================================================================
	Input
   ====================================================================== */

#define PAD_MOUSE_BUTTONS (WPAD_BUTTON_A | WPAD_BUTTON_B | INPUT_BUTTON_Z)

static uint32 repeat_button = 0, repeat_at = 0;

/* D-Pad presses with auto repeat, for the menus */
static uint32 repeated_down(uint32 down, uint32 held)
{
	const uint32 dirs = WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT;
	uint32 r = plat_frame();
	uint32 out = down;

	if (down & dirs) {
		repeat_button = down & dirs;
		repeat_at = r + 24;
	} else if ((held & repeat_button) && (int32)(r - repeat_at) >= 0) {
		out |= repeat_button;
		repeat_at = r + 5;
	} else if (!(held & repeat_button))
		repeat_button = 0;
	return out;
}

// the Mac mouse, in Mac screen pixels
static float mac_mouse_x = 320, mac_mouse_y = 240;
static int sent_mouse_x = -1, sent_mouse_y = -1;
static bool mac_mouse_down = false, sent_mouse_down = false;

static void send_mouse(void)
{
	int w = video_width(), h = video_height();
	if (w <= 0 || h <= 0)
		return;
	if (mac_mouse_x < 0)
		mac_mouse_x = 0;
	if (mac_mouse_x > w - 1)
		mac_mouse_x = w - 1;
	if (mac_mouse_y < 0)
		mac_mouse_y = 0;
	if (mac_mouse_y > h - 1)
		mac_mouse_y = h - 1;

	int x = (int)mac_mouse_x, y = (int)mac_mouse_y;
	if (x != sent_mouse_x || y != sent_mouse_y) {
		ADBMouseMoved(x, y);
		sent_mouse_x = x;
		sent_mouse_y = y;
	}
	if (mac_mouse_down != sent_mouse_down) {
		if (mac_mouse_down)
			ADBMouseDown(0);
		else
			ADBMouseUp(0);
		sent_mouse_down = mac_mouse_down;
	}
}

static void release_emulated_inputs(void)
{
	mac_keys_release(SRC_PAD);
	mac_keys_release(SRC_TYPE);
	type_phase = 0;
	overlay_release();
	mac_mouse_down = false;
	send_mouse();
}

/* read the controllers; returns the buttons pressed and held */
static void read_input(uint32 *down, uint32 *held)
{
	INPUT_Update();
	*down = INPUT_PadsDown();
	*held = INPUT_PadsHeld();

	pointer_active = INPUT_CursorActive();
	if (pointer_active)
		INPUT_CursorPosition(&pointer_x, &pointer_y);
	cursor_moved = INPUT_CursorMoved();
	mouse_pressed = INPUT_MouseButtonsPressed();
	mouse_held = INPUT_MouseButtonsHeld();
	int dx, dy;
	INPUT_MouseDelta(&dx, &dy, &mouse_wheel);
}

/* The update popup and the menus. Returns false if the app should quit. */
static void handle_ui(uint32 down, uint32 held)
{
	// the update popup takes the controls while it is up
	int st = plat_update_popup(down, &popup);
	if (st == PLAT_POPUP_EXIT) {
		popup_open = false;
		quit_wanted = true;
		return;
	}
	popup_open = st == PLAT_POPUP_OPEN;
	if (popup_open)
		return;

	if (type_open) {
		type_update_keyboard();
		return;
	}

	if (down & WPAD_BUTTON_HOME) {
		if (menu_open)
			menu_close();
		else if (browser_open && !browser_choosing)
			browser_open = false;
		else if (!browser_open) {
			menu_open = true;
			menu_sel = 0;
		}
		down &= ~WPAD_BUTTON_HOME;
	}

	bool click = (down & WPAD_BUTTON_A) || (mouse_pressed & INPUT_MOUSE_LEFT);

	if (menu_open) {
		uint32 rd = repeated_down(down, held);
		int hover = pointer_active ? menu_item_at(pointer_x, pointer_y) : -1;
		if (cursor_moved && hover >= 0)
			menu_sel = hover;
		if (rd & WPAD_BUTTON_UP)
			menu_sel = (menu_sel + NUM_MENU_ITEMS - 1) % NUM_MENU_ITEMS;
		if (rd & WPAD_BUTTON_DOWN)
			menu_sel = (menu_sel + 1) % NUM_MENU_ITEMS;
		if (rd & WPAD_BUTTON_LEFT)
			menu_change(-1);
		if (rd & WPAD_BUTTON_RIGHT)
			menu_change(1);
		if (click)
			menu_activate();
		else if (down & WPAD_BUTTON_B)
			menu_close();
	} else if (browser_open) {
		uint32 rd = repeated_down(down, held);
		int hover = pointer_active ? browser_line_at(pointer_x, pointer_y) : -1;
		if (cursor_moved && hover >= 0)
			entry_sel = hover;
		if (rd & WPAD_BUTTON_UP)
			browser_move(-1);
		if (rd & WPAD_BUTTON_DOWN)
			browser_move(1);
		if (rd & WPAD_BUTTON_LEFT)
			browser_move(-BROWSER_LINES);
		if (rd & WPAD_BUTTON_RIGHT)
			browser_move(BROWSER_LINES);
		if (mouse_wheel)
			browser_move(mouse_wheel > 0 ? -3 : 3);
		if (click)
			browser_activate();
		else if (down & WPAD_BUTTON_B)
			browser_up();
		else if (down & WPAD_BUTTON_1)
			browser_open = false;
	}
}

/* Controls going to the Mac */
static void mac_input(uint32 down, uint32 held)
{
	type_step();

	if (down & WPAD_BUTTON_PLUS) {
		if (keys_visible)
			overlay_hide();
		else if (type_pending()) {
			type_cancel();
			show_toast("Typing stopped");
		} else
			type_open_keyboard();
	}
	if (down & WPAD_BUTTON_MINUS) {
		char msg[48];
		set_scale_mode(scale_mode + 1);
		save_settings();
		snprintf(msg, sizeof(msg), "Display: %s", scale_names[scale_mode]);
		show_toast(msg);
	}

	mac_key(K_Up, SRC_PAD, held & WPAD_BUTTON_UP);
	mac_key(K_Down, SRC_PAD, held & WPAD_BUTTON_DOWN);
	mac_key(K_Left, SRC_PAD, held & WPAD_BUTTON_LEFT);
	mac_key(K_Right, SRC_PAD, held & WPAD_BUTTON_RIGHT);
	mac_key(K_Command, SRC_PAD, held & (WPAD_BUTTON_1 | INPUT_BUTTON_C));
	mac_key(K_Option, SRC_PAD, held & WPAD_BUTTON_2);

	update_usb_keyboard(true);

	// pointer: Wii Remote IR, GamePad touch or USB mouse, absolute
	bool on_keys = pointer_active && point_in_keys(pointer_x, pointer_y);
	bool click_down = (down & PAD_MOUSE_BUTTONS)
		|| (mouse_pressed & (INPUT_MOUSE_LEFT | INPUT_MOUSE_RIGHT));
	bool click_held = (held & PAD_MOUSE_BUTTONS)
		|| (mouse_held & (INPUT_MOUSE_LEFT | INPUT_MOUSE_RIGHT));

	if (keys_visible) {
		if (on_keys && click_down && !mac_mouse_down) {
			int k = overlay_key_at(pointer_x, pointer_y);
			if (k >= 0)
				overlay_press(k);
		}
		if (key_pressed >= 0 && !click_held)
			overlay_release();
	}

	update_display_rect();
	if (pointer_active && !on_keys && key_pressed < 0
		&& (cursor_moved || click_down || mac_mouse_down) && disp_w > 0 && disp_h > 0) {
		mac_mouse_x = (pointer_x - disp_x) * video_width() / disp_w;
		mac_mouse_y = (pointer_y - disp_y) * video_height() / disp_h;
	}

	if (mac_mouse_down)
		mac_mouse_down = click_held;
	else if (click_down && !on_keys && key_pressed < 0)
		mac_mouse_down = true;

	// analog sticks move the mouse from where it is
	float sx, sy;
	float len = INPUT_Stick(INPUT_STICK_LEFT, &sx, &sy, NULL);
	if (len > 0.15f) {
		float speed = 2.0f + 8.0f * (len - 0.15f) / 0.85f;
		mac_mouse_x += sx / len * speed;
		mac_mouse_y -= sy / len * speed;
	}

	send_mouse();
}

/* wait for the next 60Hz frame */
static void wait_frame(void)
{
	uint32 f = plat_frame();
	while (plat_frame() == f)
		plat_sleep_usec(2000);
}

static void check_quit(void)
{
	if (!plat_poll_system())
		quit_wanted = true;
	if (quit_wanted) {
		quit_wanted = false;
		menu_open = browser_open = false;
		type_close_keyboard(false);
		QuitEmulator();
	}
}

/* ======================================================================
	Entry points
   ====================================================================== */

void ui_init(void)
{
	init_hid_map();
	int m = PrefsFindInt32("displaymode");
	scale_mode = (m >= 0 && m < NUM_SCALES) ? m : SCALE_ASPECT;
	smoothing = PrefsFindBool("smoothing");
	plat_update_start();
}

void ui_exit(void)
{
	type_close_keyboard(false);
	plat_update_stop();
	free(entries);
	entries = NULL;
}

void ui_tick(bool draw_mac)
{
	uint32 down, held;

	check_quit();
	read_input(&down, &held);
	handle_ui(down, held);

	if (ui_pauses_mac()) {
		// The Mac waits while a menu, the browser or the text keyboard is
		// open: stay here until they close
		release_emulated_inputs();
		update_usb_keyboard(false);
		video_convert();
		while (ui_pauses_mac()) {
			render_frame();
			wait_frame();
			check_quit();
			read_input(&down, &held);
			handle_ui(down, held);
			update_usb_keyboard(false);
		}
		video_mark_dirty();
		render_frame();
		return;
	}

	mac_input(down, held);

	bool changed = draw_mac && video_convert();
	if (changed || overlay_wanted() || overlay_shown)
		render_frame();
}

#ifdef TEST_UI
/* emulator test runs without a ROM (main_wii.cpp) */
void ui_test(int step)
{
	switch (step) {
	case 0:
		keys_visible = true;
		show_toast("Test pattern");
		break;
	case 1:
		overlay_hide();
		type_open_keyboard();
		break;
	case 2:
		menu_open = true;
		break;
	}
}
#endif

bool ui_alert(const char *title, const char *text, const char *yes, const char *no)
{
	alert_title = title;
	alert_text = text;
	alert_yes = yes;
	alert_no = no;
	alert_open = true;

	bool result = true;
	uint32 down, held;
	// let go of what was pressed before the alert came up
	read_input(&down, &held);
	for (;;) {
		render_frame();
		wait_frame();
		if (!plat_poll_system()) {
			alert_open = false;
			plat_quit(false);
		}
		read_input(&down, &held);
		if (down & WPAD_BUTTON_A) {
			result = true;
			break;
		}
		if (down & (WPAD_BUTTON_B | WPAD_BUTTON_HOME)) {
			result = no == NULL;
			break;
		}
	}
	alert_open = false;
	render_frame();
	return result;
}

bool ui_choose_file(const char *title, char *path, size_t n)
{
	uint32 down, held;

	browser_title = title;
	browser_choosing = true;
	browser_chosen = false;
	if (!browser_open_at(plat_app_dir())) {
		browser_choosing = false;
		return false;
	}
	read_input(&down, &held);
	while (browser_open) {
		render_frame();
		wait_frame();
		if (!plat_poll_system())
			plat_quit(false);
		read_input(&down, &held);
		if (down & WPAD_BUTTON_HOME)
			browser_open = false;
		else
			handle_ui(down, held);
	}
	browser_choosing = false;
	render_frame();
	if (!browser_chosen)
		return false;
	snprintf(path, n, "%s", browser_choice);
	return true;
}
