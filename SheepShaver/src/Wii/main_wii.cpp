/*
 *  main_wii.cpp - Startup code, Wii / Wii U
 *
 *  SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  One thread does everything: the PowerPC interpreter checks the clock
 *  between blocks (PPC_CHECK_TICKS) and runs the 60Hz tick: Mac
 *  interrupts, the screen, controllers and menus (ui_wii.cpp), sound.
 *
 *  Mac memory map (all through vm_page_diff[], in 64 KB pages):
 *    0x00000000  RAM, low memory globals at the bottom
 *    0x40800000  ROM area (5 MB)
 *    0x5fff0000  kernel data (second address; the same memory as below)
 *    0x60000000  SheepShaver data and the interrupt stack
 *    0x68070000  DR emulator
 *    0x68ff0000  kernel data (at 0x68ffe000)
 *    0x69000000  DR cache
 *    0xa0000000  frame buffer (video_wii.cpp)
 *  Anything else reads as zeros and ignores writes, as in SheepShaver's
 *  reserved 4 GB address space on 64-bit hosts.
 */

#include "sysdeps.h"
#include "main.h"
#include "version.h"
#include "prefs.h"
#include "prefs_editor.h"
#include "cpu_emulation.h"
#include "emul_op.h"
#include "xlowmem.h"
#include "xpram.h"
#include "timer.h"
#include "adb.h"
#include "sony.h"
#include "disk.h"
#include "cdrom.h"
#include "scsi.h"
#include "video.h"
#include "audio.h"
#include "ether.h"
#include "serial.h"
#include "clip.h"
#include "extfs.h"
#include "sys.h"
#include "macos_util.h"
#include "rom_patches.h"
#include "user_strings.h"
#include "vm_alloc.h"
#include "thunks.h"
#include "plat.h"
#include "wii_glue.h"

// libogc names a register PVR; SheepShaver has a variable of that name
#undef PVR

#include <dirent.h>
#include <malloc.h>
#include <sys/stat.h>

#define DEBUG 0
#include "debug.h"

// Constants
const uint32 ROM_BASE = 0x40800000;		// Base address of ROM
const uint32 SIG_STACK_SIZE = 0x10000;	// Size of the interrupt stack
const uint32 FRAME_BUFFER_BASE = 0xa0000000;

// Global variables (exported)
uint32 RAMBase;			// Base address of Mac RAM
uint32 RAMSize;			// Size of Mac RAM
uint32 ROMBase;			// Base address of Mac ROM
uint32 KernelDataAddr;	// Address of Kernel Data
uint32 BootGlobsAddr;	// Address of BootGlobs structure at top of Mac RAM
uint32 DRCacheAddr;		// Address of DR Cache
uint32 PVR;				// Theoretical PVR
int64 CPUClockSpeed;	// Processor clock speed (Hz)
int64 BusClockSpeed;	// Bus clock speed (Hz)
int64 TimebaseSpeed;	// Timebase clock speed (Hz)
uint8 *RAMBaseHost;		// Base address of Mac RAM (host address space)
uint8 *ROMBaseHost;		// Base address of Mac ROM (host address space)

uint32  SheepMem::page_size;			// Size of a native page
uintptr SheepMem::zero_page = 0;		// Address of ro page filled in with zeros
uintptr SheepMem::base = 0x60000000;	// Address of SheepShaver data
uintptr SheepMem::proc;					// Bottom address of SheepShave procedures
uintptr SheepMem::data;					// Top of SheepShaver data (stack like storage)

static uintptr sig_stack = 0;			// Stack for PowerPC interrupt routine
static uint8 last_xpram[XPRAM_SIZE];	// Buffer for monitoring XPRAM changes

extern void VideoRefresh(void);
extern void emul_ppc(uint32 start);
extern void init_emul_ppc(void);
extern void exit_emul_ppc(void);


/*
 *  The Mac address space: 64 KB pages, each pointing into a host block
 */

uintptr vm_page_diff[1 << (32 - VM_PAGE_BITS)];		// reads
uintptr vm_page_diff_w[1 << (32 - VM_PAGE_BITS)];	// writes

struct mac_region {
	uint32 mac;
	uint32 size;
	uint8 *host;
};
static const int MAX_REGIONS = 16;
static mac_region regions[MAX_REGIONS];
static int num_regions = 0;
static uint8 *zero_page_host = NULL;	// unmapped memory reads this...
static uint8 *scratch_page = NULL;		// ...and writes this

static void map_pages(uint32 mac, uint32 size, uint8 *host)
{
	const uint32 page_size = 1 << VM_PAGE_BITS;
	uint32 first = mac >> VM_PAGE_BITS;
	uint32 last = (mac + size - 1) >> VM_PAGE_BITS;
	uint32 page_mac = first << VM_PAGE_BITS;
	uint8 *page_host = host - (mac - page_mac);
	for (uint32 p = first; p <= last; p++, page_mac += page_size, page_host += page_size)
		vm_page_diff[p] = vm_page_diff_w[p] = (uintptr)page_host - page_mac;
}

/* Back Mac addresses mac..mac+size with the host memory `host` */
static void map_region(uint32 mac, uint32 size, uint8 *host)
{
	map_pages(mac, size, host);
	if (num_regions < MAX_REGIONS) {
		regions[num_regions].mac = mac;
		regions[num_regions].size = size;
		regions[num_regions].host = host;
		num_regions++;
	}
}

/* Allocate host memory for Mac addresses mac..mac+size (64 KB aligned) */
static uint8 *alloc_region(uint32 mac, uint32 size)
{
	uint8 *host = (uint8 *)vm_acquire(size);
	if (host == VM_MAP_FAILED)
		return NULL;
	map_region(mac, size, host);
	return host;
}

static void unmap_region(uint32 mac, uint32 size)
{
	const uint32 page_size = 1 << VM_PAGE_BITS;
	for (uint32 a = mac & ~(page_size - 1); a < mac + size; a += page_size) {
		vm_page_diff[a >> VM_PAGE_BITS] = (uintptr)zero_page_host - a;
		vm_page_diff_w[a >> VM_PAGE_BITS] = (uintptr)scratch_page - a;
	}
	for (int i = 0; i < num_regions; i++)
		if (regions[i].mac == mac) {
			regions[i] = regions[--num_regions];
			break;
		}
}

static bool vm_table_init(void)
{
	// Unmapped Mac memory reads a page of zeros, writes go to a scratch page
	zero_page_host = (uint8 *)vm_acquire(1 << VM_PAGE_BITS);
	scratch_page = (uint8 *)vm_acquire(1 << VM_PAGE_BITS);
	if (zero_page_host == VM_MAP_FAILED || scratch_page == VM_MAP_FAILED)
		return false;
	for (uint32 p = 0; p < (1u << (32 - VM_PAGE_BITS)); p++) {
		vm_page_diff[p] = (uintptr)zero_page_host - (p << VM_PAGE_BITS);
		vm_page_diff_w[p] = (uintptr)scratch_page - (p << VM_PAGE_BITS);
	}
	return true;
}

/* Host address to Mac address, for the few places that need it */
vm_addr_t vm_do_get_virtual_address(uint8 *addr)
{
	for (int i = 0; i < num_regions; i++) {
		const mac_region &r = regions[i];
		if (addr >= r.host && addr < r.host + r.size)
			return r.mac + (addr - r.host);
	}
	D(bug("vm_do_get_virtual_address: %p is not Mac memory\n", addr));
	return 0;
}

/* The frame buffer (video_wii.cpp) */
uint32 MapFrameBuffer(uint8 *host, uint32 size)
{
	static uint32 mapped_size = 0;
	if (mapped_size)
		unmap_region(FRAME_BUFFER_BASE, mapped_size);
	map_region(FRAME_BUFFER_BASE, size, host);
	mapped_size = size;
	return FRAME_BUFFER_BASE;
}


/*
 *  SheepShaver data (thunks, the zero page) and the interrupt stack
 */

bool SheepMem::Init(void)
{
	page_size = 4096;
	if (!alloc_region(base, size + SIG_STACK_SIZE))
		return false;
	proc = base;
	zero_page = base + (size / 2);
	Mac_memset(zero_page, 0, page_size);
	sig_stack = base + size;
	data = base + size;
	return true;
}

void SheepMem::Exit(void)
{
}


/*
 *  Interrupt stack
 */

uintptr SignalStackBase(void)
{
	return sig_stack + SIG_STACK_SIZE;
}


/*
 *  Atomic operations: one thread
 */

int atomic_add(int *var, int v)
{
	int ret = *var;
	*var += v;
	return ret;
}

int atomic_and(int *var, int v)
{
	int ret = *var;
	*var &= v;
	return ret;
}

int atomic_or(int *var, int v)
{
	int ret = *var;
	*var |= v;
	return ret;
}


/*
 *  60Hz tick, from the interpreter loop
 */

int32_t ppc_tick_countdown = 64;
static const int32_t TICK_CHECK_BLOCKS = 64;	// blocks between clock checks
static uint64 next_tick = 0;

static void xpram_watchdog(void)
{
	if (memcmp(last_xpram, XPRAM, XPRAM_SIZE)) {
		memcpy(last_xpram, XPRAM, XPRAM_SIZE);
		SaveXPRAM();
	}
}

#ifdef WII_DEBUG_LOG
/* Debug builds (make -f sheepshaver.mk
   EXTRA_CFLAGS="-DWII_DEBUG_LOG -DPPC_DEBUG_HOOKS") write debug.txt in the
   app folder once a second: where the PowerPC is, the Mac's state and the
   most frequent program counters (68k pc, or PowerPC pc | 1 in native code)
   sampled since the last line, to find where Mac OS spends its time */
extern uint32 ppc_debug_reg(int n);
#if defined(GEKKO) && !defined(__WIIU__)
#include <ogc/system.h>
#endif
struct pc_count { uint32 pc, n; };
static pc_count pc_hist[1024];

static void debug_sample(void)
{
	uint32 pc = ReadMacInt32(XLM_RUN_MODE) == MODE_68K ? ppc_debug_reg(24) : ppc_debug_reg(-1) | 1;
	uint32 h = (pc * 2654435761u) >> 22;
	for (int i = 0; i < 8; i++) {
		pc_count &c = pc_hist[(h + i) & 1023];
		if (c.pc == pc || c.n == 0) {
			c.pc = pc;
			c.n++;
			return;
		}
	}
}

static void debug_log(void)
{
	static int seconds = 0;
	char path[768];
	snprintf(path, sizeof(path), "%sdebug.txt", plat_app_dir());
	// open and close each time: libfat only writes the card on close
	FILE *f = fopen(path, seconds ? "a" : "w");
	if (f == NULL)
		return;
	// CurApName (0x910): the application in front, a Pascal string
	char app[32];
	int len = ReadMacInt8(0x910) < 32 ? ReadMacInt8(0x910) : 0;
	for (int i = 0; i < len; i++) {
		char c = ReadMacInt8(0x911 + i);
		app[i] = c >= 32 && c < 127 ? c : '?';
	}
	app[len] = 0;
	fprintf(f, "%4d pc %08x lr %08x r1 %08x 68k pc %08x mode %u irqnest %d flags %x ticks %u app %s\n",
		seconds++, ppc_debug_reg(-1), ppc_debug_reg(32), ppc_debug_reg(1), ppc_debug_reg(24),
		ReadMacInt32(XLM_RUN_MODE), (int)ReadMacInt32(XLM_IRQ_NEST),
		InterruptFlags, ReadMacInt32(0x16a), app);
	uint32 total = 0;
	for (int i = 0; i < 1024; i++)
		total += pc_hist[i].n;
	fprintf(f, "     %u samples:", total);
	for (int k = 0; k < 10; k++) {
		int best = -1;
		for (int i = 0; i < 1024; i++)
			if (pc_hist[i].n && (best < 0 || pc_hist[i].n > pc_hist[best].n))
				best = i;
		if (best < 0)
			break;
		fprintf(f, " %08x:%u", pc_hist[best].pc, pc_hist[best].n);
		pc_hist[best].n = 0;
	}
	fprintf(f, "\n");
	memset(pc_hist, 0, sizeof(pc_hist));
	if (seconds % 30 == 1) {
		// the largest block still free, to size the RAM and the reserve
		uint32 largest = 0;
		void *at = NULL;
		for (uint32 size = 64 << 20; size >= 256 << 10; size -= 256 << 10) {
			void *p = malloc(size);
			if (p) {
				free(p);
				largest = size;
				at = p;
				break;
			}
		}
		fprintf(f, "     RAM %u KB at %p, largest free block %u KB at %p\n",
			RAMSize >> 10, RAMBaseHost, largest >> 10, at);
#if defined(GEKKO) && !defined(__WIIU__)
		fprintf(f, "     arena1 %p-%p arena2 %p-%p\n", SYS_GetArena1Lo(), SYS_GetArena1Hi(),
			SYS_GetArena2Lo(), SYS_GetArena2Hi());
#endif
	}
	fclose(f);
}
#endif

static void one_tick(void)
{
	static int tick_counter = 0;
	if (++tick_counter > 60) {
		tick_counter = 0;
#ifdef WII_DEBUG_LOG
		debug_log();
#endif

		// Pseudo Mac 1Hz interrupt, update local time
		WriteMacInt32(0x20c, TimerDateTime());

		static int second_counter = 0;
		if (++second_counter > 60) {
			second_counter = 0;
			xpram_watchdog();
		}
	}

	// Controllers, menus and the screen
	VideoRefresh();

	// Sound
	audio_tick();

	// Trigger 60Hz interrupt
	if (ReadMacInt32(XLM_IRQ_NEST) == 0) {
		SetInterruptFlag(INTFLAG_VIA);
		TriggerInterrupt();
	}
}

void ppc_do_check_ticks(void)
{
	if (zero_page_host[0] | zero_page_host[0xfffc])
		memset(zero_page_host, 0, 1 << VM_PAGE_BITS);
	ppc_tick_countdown = TICK_CHECK_BLOCKS;
#ifdef WII_DEBUG_LOG
	debug_sample();
#endif
	uint64 now = GetTicks_usec();
	if (next_tick == 0)
		next_tick = now;
	if (now < next_tick)
		return;
	one_tick();
	// a menu may have held the Mac for a while: catch up
	now = GetTicks_usec();
	do {
		next_tick += 16625;
	} while (next_tick < now);
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

static loff_t file_size(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
		return -1;
	return st.st_size;
}

/* A ROM file is a 4 MB old world ROM or a new world "Mac OS ROM" file */
static bool valid_rom_size(loff_t size)
{
	return size >= 1024 * 1024 && size <= 4 * 1024 * 1024;
}

/* The ROM: the "rom" pref, else "Mac OS ROM", "ROM" or *.rom in the app
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

	static const char *const names[] = { "Mac OS ROM", "ROM", NULL };
	for (int i = 0; names[i]; i++) {
		snprintf(path, n, "%s%s", plat_app_dir(), names[i]);
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
		if (e->d_name[0] == '.' || !has_suffix(e->d_name, rom_suffixes))
			continue;
		snprintf(path, n, "%s%s", plat_app_dir(), e->d_name);
		found = valid_rom_size(file_size(path));
	}
	closedir(d);
	return found;
}

static bool load_mac_rom(const char *path)
{
	int rom_fd = open(path, O_RDONLY);
	if (rom_fd < 0) {
		ErrorAlert(GetString(STR_NO_ROM_FILE_ERR));
		return false;
	}
	uint32 rom_size = lseek(rom_fd, 0, SEEK_END);
	lseek(rom_fd, 0, SEEK_SET);
	uint8 *rom_tmp = new uint8[ROM_SIZE];
	ssize_t actual = read(rom_fd, (void *)rom_tmp, ROM_SIZE);
	close(rom_fd);

	// Decode Mac ROM
	bool ok = actual > 0 && DecodeROM(rom_tmp, actual);
	delete[] rom_tmp;
	if (!ok) {
		if (rom_size != 4 * 1024 * 1024)
			ErrorAlert(GetString(STR_ROM_SIZE_ERR));
		else
			ErrorAlert(GetString(STR_ROM_FILE_READ_ERR));
		return false;
	}
	return true;
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


static void Quit(void);

#ifdef TEST_PPC
/*
 *  Interpreter self test (make EXTRA_CFLAGS=-DTEST_PPC): runs a loop of
 *  PowerPC code from Mac RAM, through the page table, and reports the speed
 */

static void test_ppc(void)
{
	const uint32 N = 2000000;
	static const uint32 code[] = {
		0x3c800000 | (N >> 16),		// lis   r4,N@h
		0x60840000 | (N & 0xffff),	// ori   r4,r4,N@l
		0x38600000,					// li    r3,0
		0x38630001,					// loop: addi r3,r3,1
		0x90602000,					// stw   r3,0x2000(0)
		0x80a02000,					// lwz   r5,0x2000(0)
		0x7c032000,					// cmpw  r3,r4
		0x4180fff0,					// blt   loop
		0x7c632a14,					// add   r3,r3,r5
		0x4e800020,					// blr
	};
	const uint32 code_addr = 0x10000, tvect = 0x8000;
	for (unsigned i = 0; i < sizeof(code) / sizeof(code[0]); i++)
		WriteMacInt32(code_addr + i * 4, code[i]);
	WriteMacInt32(tvect, code_addr);
	WriteMacInt32(tvect + 4, 0);

	init_emul_ppc();
	ppc_tick_countdown = 0x7fffffff;	// no Mac to interrupt
	uint64 start = GetTicks_usec();
	uint32 r3 = call_macos(tvect);
	uint64 usec = GetTicks_usec() - start;

	char msg[256];
	double mips = 5.0 * N / (usec ? usec : 1);
	snprintf(msg, sizeof(msg),
		"%u loop iterations (%u instructions) in %u ms: %.1f million PowerPC "
		"instructions a second.\n\nResult %s (r3 = %u, RAM[0x2000] = %u).",
		N, 5 * N, (unsigned)(usec / 1000), mips,
		(r3 == 2 * N && ReadMacInt32(0x2000) == N) ? "correct" : "WRONG",
		r3, ReadMacInt32(0x2000));
	printf("TEST_PPC: %s\n", msg);
	ui_alert("PowerPC interpreter test", msg, "Quit", NULL);
	Quit();
}
#endif


/*
 *  Main program
 */

int main(int argc, char **argv)
{
	char str[256];

	if (!plat_init(argc, argv))
		plat_quit(false);

	PrefsInit(NULL, argc, argv);
	ui_init();

	add_app_dir_images();
	add_empty_drives();

	SysInit();

	// The emulated CPU: a 7400 (G4 with AltiVec), as on other hosts
	PVR = 0x000c0000;
	CPUClockSpeed = 100000000;
	BusClockSpeed = 100000000;
	TimebaseSpeed = 25000000;

	if (!vm_table_init()) {
		ErrorAlert(GetString(STR_NOT_ENOUGH_MEMORY_ERR));
		Quit();
	}

	// Kernel Data, at two addresses (one host block for both)
	uint8 *kernel_area = (uint8 *)vm_acquire(1 << VM_PAGE_BITS);
	if (kernel_area == VM_MAP_FAILED) {
		ErrorAlert(GetString(STR_NOT_ENOUGH_MEMORY_ERR));
		Quit();
	}
	map_region(KERNEL_DATA_BASE & ~0xffff, 1 << VM_PAGE_BITS, kernel_area);
	map_pages(KERNEL_DATA2_BASE & ~0xffff, 1 << VM_PAGE_BITS, kernel_area);
	KernelDataAddr = KERNEL_DATA_BASE;

	// DR emulator and cache
	if (!alloc_region(DR_EMULATOR_BASE, DR_EMULATOR_SIZE)
		|| !alloc_region(DR_CACHE_BASE, DR_CACHE_SIZE)) {
		ErrorAlert(GetString(STR_NOT_ENOUGH_MEMORY_ERR));
		Quit();
	}
	DRCacheAddr = DR_CACHE_BASE;

	// SheepShaver data and the interrupt stack
	if (!SheepMem::Init()) {
		ErrorAlert(GetString(STR_NOT_ENOUGH_MEMORY_ERR));
		Quit();
	}

	// RAM from address 0, as much as the console has room for
	RAMSize = PrefsFindInt32("ramsize") & ~0xfffff;
	if (RAMSize < 8 * 1024 * 1024) {
		WarningAlert(GetString(STR_SMALL_RAM_WARN));
		RAMSize = 8 * 1024 * 1024;
	}
	RAMBase = 0;
	RAMBaseHost = (uint8 *)plat_alloc_ram(RAMSize, 8 << 20, 1 << 20, &RAMSize);
	if (RAMBaseHost == NULL) {
		ErrorAlert(GetString(STR_NOT_ENOUGH_MEMORY_ERR));
		Quit();
	}
	memset(RAMBaseHost, 0, RAMSize);
	map_region(RAMBase, RAMSize, RAMBaseHost);

	// ROM
	ROMBase = ROM_BASE;
	ROMBaseHost = alloc_region(ROMBase, ROM_AREA_SIZE);
	if (ROMBaseHost == NULL) {
		ErrorAlert(GetString(STR_NOT_ENOUGH_MEMORY_ERR));
		Quit();
	}

#ifdef TEST_PPC
	test_ppc();
#endif

	char rom_path[768];
	if (!find_rom(rom_path, sizeof(rom_path))) {
		char shown[256], msg[640];
		ui_display_path(plat_app_dir(), shown, sizeof(shown));
		snprintf(msg, sizeof(msg),
			"SheepShaver needs the ROM of a PCI Power Macintosh: the "
			"\"Mac OS ROM\" file from the System Folder of Mac OS 8.6 to "
			"9.0.4, or a 4 MB old world ROM.\n\n"
			"Copy it into %s, or choose it now.", shown);
		for (;;) {
			if (!ChoiceAlert(msg, "Choose ROM", "Quit"))
				Quit();
			if (ui_choose_file("Choose a Mac OS ROM", rom_path, sizeof(rom_path))) {
				if (valid_rom_size(file_size(rom_path))) {
					SavePrefsLine("rom", rom_path);
					break;
				}
				ErrorAlert(GetString(STR_ROM_SIZE_ERR));
			}
		}
	}
	if (!load_mac_rom(rom_path))
		Quit();

	// Initialize everything
	if (!InitAll(NULL))
		Quit();
	D(bug("Initialization complete\n"));

	memcpy(last_xpram, XPRAM, XPRAM_SIZE);

	// Jump to ROM boot routine
	init_emul_ppc();
	emul_ppc(ROMBase + 0x310000);

	Quit();
	return 0;
}


/*
 *  Cleanup and quit
 */

static void Quit(void)
{
	static bool quitting = false;
	if (quitting)
		plat_quit(false);
	quitting = true;

	exit_emul_ppc();
	ExitAll();
	SysExit();
	PrefsExit();
	ui_exit();
	plat_quit(false);
}

void QuitEmulator(void)
{
	Quit();
}


/*
 *  Dump 68k registers
 */

void Dump68kRegs(M68kRegisters *r)
{
	for (int i = 0; i < 8; i++)
		printf("d%d: %08x%s", i, r->d[i], (i == 3 || i == 7) ? "\n" : ", ");
	for (int i = 0; i < 8; i++)
		printf("a%d: %08x%s", i, r->a[i], (i == 3 || i == 7) ? "\n" : ", ");
}


/*
 *  Make code executable
 */

void MakeExecutable(int dummy, uint32 start, uint32 length)
{
	if ((start >= ROMBase) && (start < (ROMBase + ROM_SIZE)))
		return;
	FlushCodeCache(start, start + length);
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

volatile uint32 InterruptFlags = 0;

void SetInterruptFlag(uint32 flag)
{
	InterruptFlags |= flag;
}

void ClearInterruptFlag(uint32 flag)
{
	InterruptFlags &= ~flag;
}

void DisableInterrupt(void)
{
	WriteMacInt32(XLM_IRQ_NEST, int32(ReadMacInt32(XLM_IRQ_NEST)) + 1);
}

void EnableInterrupt(void)
{
	WriteMacInt32(XLM_IRQ_NEST, int32(ReadMacInt32(XLM_IRQ_NEST)) - 1);
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
	return ui_alert("SheepShaver", text, pos, neg);
}


/*
 *  No host folder sharing (ExtFS) on the consoles
 */

void ExtFSInit(void)
{
}

void ExtFSExit(void)
{
}

void InstallExtFS(void)
{
}

int16 ExtFSComm(uint16 message, uint32 paramBlock, uint32 globalsPtr)
{
	return -50;	// paramErr
}

int16 ExtFSHFS(uint32 vcb, uint16 selectCode, uint32 paramBlock, uint32 globalsPtr, int16 fsid)
{
	return -50;	// paramErr
}
