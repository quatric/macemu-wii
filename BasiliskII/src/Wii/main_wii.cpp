/*
 *  main_wii.cpp - Startup code, Wii / Wii U
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  One thread does everything: the 68k emulation calls cpu_do_check_ticks()
 *  as it runs (USE_CPU_EMUL_SERVICES), which runs the 60Hz tick: Mac
 *  interrupts, the screen, controllers and menus (ui_wii.cpp), sound.
 *
 *  The app folder (sd:/apps/BasiliskIIWii/ on the Wii,
 *  sd:/wiiu/apps/BasiliskIIWiiU/ on the Wii U) holds the prefs file, the
 *  XPRAM, the Mac ROM and usually the disk images.
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "sys.h"
#include "rom_patches.h"
#include "xpram.h"
#include "timer.h"
#include "video.h"
#include "prefs.h"
#include "macos_util.h"
#include "user_strings.h"
#include "version.h"
#include "main.h"
#include "plat.h"
#include "wii_glue.h"

#include <dirent.h>
#include <malloc.h>
#include <sys/stat.h>

#define DEBUG 0
#include "debug.h"

// CPU and FPU type, addressing mode
int CPUType;
bool CPUIs68060;
int FPUType;
bool TwentyFourBitAddressing;

// Ethernet over VDE (not used here)
char *vde_sock = NULL;

static uint8 last_xpram[XPRAM_SIZE];
static uint8 *ram_rom_area = NULL;

static void one_tick(void);


/*
 *  Update virtual clock and trigger interrupts if necessary
 *  (from main_unix.cpp)
 */

static uint64 emulated_ticks_start = 0;
static uint64 emulated_ticks_count = 0;
static int64 emulated_ticks_current = 0;
static int32 emulated_ticks_quantum = 1000;
int32 emulated_ticks = emulated_ticks_quantum;

void cpu_do_check_ticks(void)
{
	uint64 now;
	static uint64 next = 0;
	if (next == 0)
		next = emulated_ticks_start = GetTicks_usec();

	// Update total instructions count
	if (emulated_ticks <= 0) {
		emulated_ticks_current += (emulated_ticks_quantum - emulated_ticks);
		if (emulated_ticks_current < 0) {
			now = GetTicks_usec();
			goto recalibrate_quantum;
		}
	}

	// Check for interrupt opportunity
	now = GetTicks_usec();
	if (next < now) {
		one_tick();
		// a menu may have held the Mac for a while: catch up
		now = GetTicks_usec();
		do {
			next += 16625;
		} while (next < now);
		emulated_ticks_count++;

		// Recalibrate 1000 Hz quantum every 10 ticks
		static uint64 last = 0;
		if (last == 0)
			last = now;
		else if (now - last > 166250) {
		  recalibrate_quantum:
			emulated_ticks_quantum = ((uint64)emulated_ticks_current * 1000) / (now - last);
			if (emulated_ticks_quantum < 100)
				emulated_ticks_quantum = 100;
			emulated_ticks_current = 0;
			last = now;
		}
	}

	// Update countdown
	if (emulated_ticks <= 0)
		emulated_ticks += emulated_ticks_quantum;
}


/*
 *  Files in the app folder
 */

static bool has_suffix(const char *name, const char *const *suffixes)
{
	const char *dot = strrchr(name, '.');
	if (dot == NULL)
		return false;
	for (int i = 0; suffixes[i]; i++)
		if (strcasecmp(dot, suffixes[i]) == 0)
			return true;
	return false;
}

static bool valid_rom_size(loff_t size)
{
	return size == 64 * 1024 || size == 128 * 1024 || size == 256 * 1024
		|| size == 512 * 1024 || size == 1024 * 1024;
}

static loff_t file_size(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
		return -1;
	return st.st_size;
}

/* The ROM: the "rom" pref, else a file called ROM or *.rom in the app
   folder */
static bool find_rom(char *path, size_t n)
{
	const char *pref = PrefsFindString("rom");
	if (pref && *pref) {
		if (pref[0] == '/' || strchr(pref, ':'))
			snprintf(path, n, "%s", pref);
		else
			snprintf(path, n, "%s%s", plat_app_dir(), pref);
		if (valid_rom_size(file_size(path)))
			return true;
	}

	static const char *const rom_suffixes[] = { ".rom", NULL };
	DIR *d = opendir(plat_app_dir());
	if (d == NULL)
		return false;
	bool found = false;
	struct dirent *e;
	while (!found && (e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.')
			continue;
		if (strcasecmp(e->d_name, "ROM") != 0 && !has_suffix(e->d_name, rom_suffixes))
			continue;
		snprintf(path, n, "%s%s", plat_app_dir(), e->d_name);
		found = valid_rom_size(file_size(path));
	}
	closedir(d);
	return found;
}

/* No drives in the prefs: use the images in the app folder */
static void add_app_dir_images(void)
{
	static const char *const cd_suffixes[] = { ".iso", ".toast", ".cdr", NULL };
	static const char *const disk_suffixes[] = {
		".dsk", ".img", ".image", ".hfv", ".hda", ".hfs", ".disk", ".dc42", NULL
	};

	if (PrefsFindString("disk") || PrefsFindString("floppy") || PrefsFindString("cdrom"))
		return;

	DIR *d = opendir(plat_app_dir());
	if (d == NULL)
		return;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.')
			continue;
		char path[768];
		snprintf(path, sizeof(path), "%s%s", plat_app_dir(), e->d_name);
		loff_t size = file_size(path);
		if (size <= 0)
			continue;
		if (has_suffix(e->d_name, cd_suffixes))
			PrefsAddString("cdrom", path);
		else if (has_suffix(e->d_name, disk_suffixes))
			PrefsAddString(size <= 1474560 + 84 ? "floppy" : "disk", path);
	}
	closedir(d);
}

/* Empty drives, for images inserted from the HOME menu */
static void add_empty_drives(void)
{
	PrefsAddString("floppy", "<floppy>");
	PrefsAddString("disk", "<disk>");
	if (!PrefsFindBool("nocdrom"))
		PrefsAddString("cdrom", "<cdrom>");
}


#ifdef TEST_UI
/* Emulator test runs without a ROM: a test pattern as the Mac screen,
   the Mac key overlay, then the libwiiosk text keyboard */
extern void ui_test(int step);

static void test_ui(void)
{
	if (!VideoInit(false))
		QuitEmulator();
	uint8 pal[256 * 3];
	for (int i = 0; i < 256; i++) {
		pal[i * 3] = (i & 7) * 36;
		pal[i * 3 + 1] = ((i >> 3) & 7) * 36;
		pal[i * 3 + 2] = (i >> 6) * 85;
	}
	VideoMonitors[0]->set_palette(pal, 256);
	int w = video_width(), h = video_height();
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			MacFrameBaseHost[y * w + x] = (uint8)((x / 20) + (y / 60) * 32);

	ui_test(0);
	for (int f = 0; f < 600; f++) {
		if (f == 180)
			ui_test(1);
		ui_tick(true);
		uint64 next = GetTicks_usec() + 16667;
		while (GetTicks_usec() < next)
			Delay_usec(1000);
	}
	QuitEmulator();
}
#endif

/*
 *  Main program
 */

int main(int argc, char **argv)
{
	if (!plat_init(argc, argv))
		plat_quit(false);

	PrefsInit(NULL, argc, argv);
	ui_init();

	add_app_dir_images();
	add_empty_drives();

	SysInit();

	// RAM: whole megabytes, as much as the console has room for
	RAMSize = PrefsFindInt32("ramsize") & 0xfff00000;
	if (RAMSize < 1024 * 1024) {
		WarningAlert(GetString(STR_SMALL_RAM_WARN));
		RAMSize = 1024 * 1024;
	}
	while ((ram_rom_area = (uint8 *)memalign(32, RAMSize + 0x100000)) == NULL) {
		if (RAMSize <= 4 * 1024 * 1024) {
			ErrorAlert(STR_NO_MEM_ERR);
			QuitEmulator();
		}
		RAMSize /= 2;
	}
	memset(ram_rom_area, 0, RAMSize + 0x100000);
	RAMBaseHost = ram_rom_area;
	ROMBaseHost = RAMBaseHost + RAMSize;

#ifdef TEST_UI
	test_ui();
#endif

	// The ROM
	char rom_path[768];
	if (!find_rom(rom_path, sizeof(rom_path))) {
		char msg[512];
		char shown[256];
		ui_display_path(plat_app_dir(), shown, sizeof(shown));
		snprintf(msg, sizeof(msg),
			"Basilisk II needs the ROM of a 68020/030/040 Macintosh "
			"(Mac II, IIci, Quadra 650, 700, 900...).\n\n"
			"Copy it into %s as \"ROM\" or name.rom, or choose it now.", shown);
		for (;;) {
			if (!ChoiceAlert(msg, "Choose ROM", "Quit"))
				QuitEmulator();
			if (ui_choose_file("Choose a Macintosh ROM", rom_path, sizeof(rom_path))) {
				if (valid_rom_size(file_size(rom_path))) {
					SavePrefsLine("rom", rom_path);
					break;
				}
				ErrorAlert(STR_ROM_SIZE_ERR);
			}
		}
	}

	int rom_fd = open(rom_path, O_RDONLY);
	if (rom_fd < 0) {
		ErrorAlert(STR_NO_ROM_FILE_ERR);
		QuitEmulator();
	}
	ROMSize = lseek(rom_fd, 0, SEEK_END);
	if (!valid_rom_size(ROMSize)) {
		ErrorAlert(STR_ROM_SIZE_ERR);
		close(rom_fd);
		QuitEmulator();
	}
	lseek(rom_fd, 0, SEEK_SET);
	if (read(rom_fd, ROMBaseHost, ROMSize) != (ssize_t)ROMSize) {
		ErrorAlert(STR_ROM_FILE_READ_ERR);
		close(rom_fd);
		QuitEmulator();
	}
	close(rom_fd);

	// Initialize everything
	if (!InitAll(NULL))
		QuitEmulator();
	D(bug("Initialization complete\n"));

	memcpy(last_xpram, XPRAM, XPRAM_SIZE);

	// Start 68k and jump to ROM boot routine
	Start680x0();

	QuitEmulator();
	return 0;
}


/*
 *  Quit emulator
 */

void QuitEmulator(void)
{
	static bool quitting = false;
	if (quitting)
		plat_quit(false);
	quitting = true;

	Exit680x0();
	ExitAll();

	if (ram_rom_area) {
		free(ram_rom_area);
		ram_rom_area = NULL;
		RAMBaseHost = ROMBaseHost = NULL;
	}

	SysExit();
	PrefsExit();
	ui_exit();
	plat_quit(false);
}


/*
 *  Code was patched, flush caches if neccessary (no JIT here)
 */

void FlushCodeCache(void *start, uint32 size)
{
}


/*
 *  Mutexes: one thread, nothing to lock
 */

struct B2_mutex {
	int dummy;
};

B2_mutex *B2_create_mutex(void)
{
	return new B2_mutex;
}

void B2_lock_mutex(B2_mutex *mutex)
{
}

void B2_unlock_mutex(B2_mutex *mutex)
{
}

void B2_delete_mutex(B2_mutex *mutex)
{
	delete mutex;
}


/*
 *  Interrupt flags
 */

uint32 InterruptFlags = 0;

void SetInterruptFlag(uint32 flag)
{
	InterruptFlags |= flag;
}

void ClearInterruptFlag(uint32 flag)
{
	InterruptFlags &= ~flag;
}


/*
 *  60Hz tick (really 60.15Hz)
 */

static void xpram_watchdog(void)
{
	if (memcmp(last_xpram, XPRAM, XPRAM_SIZE)) {
		memcpy(last_xpram, XPRAM, XPRAM_SIZE);
		SaveXPRAM();
	}
}

static void one_second(void)
{
	// Pseudo Mac 1Hz interrupt, update local time
	WriteMacInt32(0x20c, TimerDateTime());

	SetInterruptFlag(INTFLAG_1HZ);
	TriggerInterrupt();

	static int second_counter = 0;
	if (++second_counter > 60) {
		second_counter = 0;
		xpram_watchdog();
	}
}

static void one_tick(void)
{
	static int tick_counter = 0;
	if (++tick_counter > 60) {
		tick_counter = 0;
		one_second();
	}

	// Controllers, menus and the screen
	VideoRefresh();

	// Sound
	audio_tick();

	// Trigger 60Hz interrupt
	if (ROMVersion != ROM_VERSION_CLASSIC || HasMacStarted()) {
		SetInterruptFlag(INTFLAG_60HZ);
		TriggerInterrupt();
	}
}


/*
 *  Alerts
 */

void ErrorAlert(const char *text)
{
	ui_alert(GetString(STR_ERROR_ALERT_TITLE), text, GetString(STR_OK_BUTTON), NULL);
}

void WarningAlert(const char *text)
{
	ui_alert(GetString(STR_WARNING_ALERT_TITLE), text, GetString(STR_OK_BUTTON), NULL);
}

bool ChoiceAlert(const char *text, const char *pos, const char *neg)
{
	return ui_alert("Basilisk II", text, pos, neg);
}
