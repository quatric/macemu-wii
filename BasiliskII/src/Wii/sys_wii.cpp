/*
 *  sys_wii.cpp - System dependent routines, Wii / Wii U (disk image files)
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Disks, floppies and CD-ROMs are image files on the SD card or USB
 *  drive. A CD-ROM image (any "cdrom" prefs entry, or a file inserted as
 *  one) is a plain 2048 byte per sector ISO; its table of contents is one
 *  data track.
 */

#include "sysdeps.h"
#include "main.h"
#include "macos_util.h"
#include "prefs.h"
#include "user_strings.h"
#include "sys.h"
#include "wii_glue.h"

#include <sys/stat.h>

#define DEBUG 0
#include "debug.h"

struct mac_file_handle {
	char *name;
	int fd;
	bool read_only;
	bool is_floppy;
	bool is_cdrom;
	bool inserted;
	bool removable;		// an empty drive images can be inserted into
	loff_t start_byte;	// Size of file header (if any)
	loff_t file_size;	// Size of the disk contents
};

// Every open handle, to find an empty drive for SysInsertImage()
static const int MAX_HANDLES = 16;
static mac_file_handle *handles[MAX_HANDLES];

static void track_handle(mac_file_handle *fh, bool add)
{
	for (int i = 0; i < MAX_HANDLES; i++) {
		if (add && handles[i] == NULL) {
			handles[i] = fh;
			return;
		}
		if (!add && handles[i] == fh) {
			handles[i] = NULL;
			return;
		}
	}
}

void SysInit(void)
{
}

void SysExit(void)
{
}

void SysAddFloppyPrefs(void)
{
}

void SysAddDiskPrefs(void)
{
}

void SysAddCDROMPrefs(void)
{
	// "cdrom" entries come from the prefs file or the app folder scan
}

void SysAddSerialPrefs(void)
{
}

/* Is this file one of the CD-ROM images the prefs name, or does its name
   say it is one? */
static bool is_cdrom_name(const char *name)
{
	const char *s;
	for (int i = 0; (s = PrefsFindString("cdrom", i)) != NULL; i++)
		if (strcmp(s, name) == 0)
			return true;
	const char *dot = strrchr(name, '.');
	return dot && (strcasecmp(dot, ".iso") == 0 || strcasecmp(dot, ".toast") == 0
		|| strcasecmp(dot, ".cdr") == 0);
}

void *Sys_open(const char *name, bool read_only)
{
	if (name == NULL || *name == 0)
		return NULL;

	// "<floppy>", "<disk>" and "<cdrom>" are empty drives (main_wii.cpp
	// adds one of each) that the file browser inserts images into
	if (name[0] == '<') {
		mac_file_handle *fh = new mac_file_handle;
		fh->name = strdup(name);
		fh->fd = -1;
		fh->is_cdrom = strcmp(name, "<cdrom>") == 0;
		fh->is_floppy = strcmp(name, "<floppy>") == 0;
		fh->read_only = fh->is_cdrom;
		fh->inserted = false;
		fh->removable = true;
		fh->start_byte = 0;
		fh->file_size = 0;
		track_handle(fh, true);
		return fh;
	}

	bool is_cdrom = is_cdrom_name(name);
	if (is_cdrom)
		read_only = true;

	int fd = open(name, read_only ? O_RDONLY : O_RDWR);
	if (fd < 0 && !read_only) {
		read_only = true;
		fd = open(name, O_RDONLY);
	}
	if (fd < 0)
		return NULL;

	mac_file_handle *fh = new mac_file_handle;
	fh->name = strdup(name);
	fh->fd = fd;
	fh->read_only = read_only;
	fh->is_cdrom = is_cdrom;
	fh->inserted = true;

	loff_t size = lseek(fd, 0, SEEK_END);
	if (is_cdrom) {
		fh->start_byte = 0;
		fh->file_size = size;
	} else {
		uint8 data[256];
		lseek(fd, 0, SEEK_SET);
		memset(data, 0, sizeof(data));
		if (read(fd, data, 256) < 0)
			memset(data, 0, sizeof(data));
		FileDiskLayout(size, data, fh->start_byte, fh->file_size);
	}
	// up to 1.44MB (and a DiskCopy header) is a floppy
	fh->is_floppy = !is_cdrom && fh->file_size <= 1474560;
	// ejected floppies and CDs leave an empty drive behind
	fh->removable = fh->is_floppy || fh->is_cdrom;

	D(bug("Sys_open(%s): size %lld, floppy %d, cd %d\n", name, (long long)fh->file_size, fh->is_floppy, fh->is_cdrom));
	track_handle(fh, true);
	return fh;
}

/* Open the image file `path` into the empty drive `fh` */
static bool load_image(mac_file_handle *fh, const char *path)
{
	bool read_only = fh->is_cdrom;
	int fd = open(path, read_only ? O_RDONLY : O_RDWR);
	if (fd < 0 && !read_only) {
		read_only = true;
		fd = open(path, O_RDONLY);
	}
	if (fd < 0)
		return false;

	loff_t size = lseek(fd, 0, SEEK_END);
	if (fh->is_cdrom) {
		fh->start_byte = 0;
		fh->file_size = size;
	} else {
		uint8 data[256];
		memset(data, 0, sizeof(data));
		lseek(fd, 0, SEEK_SET);
		if (read(fd, data, 256) < 0)
			memset(data, 0, sizeof(data));
		FileDiskLayout(size, data, fh->start_byte, fh->file_size);
	}
	free(fh->name);
	fh->name = strdup(path);
	fh->fd = fd;
	fh->read_only = read_only;
	fh->inserted = true;
	return true;
}

int SysInsertImage(const char *path)
{
	bool cd = is_cdrom_name(path);
	bool floppy = false;

	if (!cd) {
		struct stat st;
		if (stat(path, &st) != 0)
			return INSERT_FAILED;
		floppy = st.st_size <= 1474560 + 84;	// DiskCopy 4.2 header
	}

	// Use an empty drive of the right kind; a big image goes into the
	// removable disk drive, a small one into the floppy drive if it is free
	mac_file_handle *target = NULL;
	for (int pass = 0; pass < 2 && target == NULL; pass++) {
		for (int i = 0; i < MAX_HANDLES; i++) {
			mac_file_handle *fh = handles[i];
			if (fh == NULL || !fh->removable || fh->inserted)
				continue;
			bool kind_ok;
			if (cd)
				kind_ok = fh->is_cdrom;
			else if (pass == 0)
				kind_ok = !fh->is_cdrom && fh->is_floppy == floppy;
			else
				kind_ok = !fh->is_cdrom;
			if (kind_ok) {
				target = fh;
				break;
			}
		}
	}
	if (target == NULL)
		return INSERT_NO_DRIVE;
	if (!load_image(target, path))
		return INSERT_FAILED;
	MountVolume(target);
	return cd ? INSERT_CDROM : INSERT_DISK;
}

void Sys_close(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return;
	if (fh->fd >= 0)
		close(fh->fd);
	track_handle(fh, false);
	free(fh->name);
	delete fh;
}

size_t Sys_read(void *arg, void *buffer, loff_t offset, size_t length)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh || fh->fd < 0)
		return 0;
	if (lseek(fh->fd, offset + fh->start_byte, SEEK_SET) < 0)
		return 0;
	ssize_t n = read(fh->fd, buffer, length);
	return n < 0 ? 0 : (size_t)n;
}

size_t Sys_write(void *arg, void *buffer, loff_t offset, size_t length)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh || fh->fd < 0 || fh->read_only)
		return 0;
	if (lseek(fh->fd, offset + fh->start_byte, SEEK_SET) < 0)
		return 0;
	ssize_t n = write(fh->fd, buffer, length);
	return n < 0 ? 0 : (size_t)n;
}

loff_t SysGetFileSize(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	return fh ? fh->file_size : 0;
}

void SysEject(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return;
	// An image file is gone once ejected; inserting it again opens it anew
	if (fh->fd >= 0) {
		fsync(fh->fd);
		close(fh->fd);
		fh->fd = -1;
	}
	fh->inserted = false;
}

bool SysFormat(void *arg)
{
	return false;
}

bool SysIsReadOnly(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	return fh ? fh->read_only : true;
}

bool SysIsFixedDisk(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	return fh ? !fh->is_floppy && !fh->is_cdrom && !fh->removable : true;
}

bool SysIsDiskInserted(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	return fh ? fh->inserted && fh->fd >= 0 : false;
}

void SysPreventRemoval(void *arg)
{
}

void SysAllowRemoval(void *arg)
{
}

/*
 *  CD-ROM images: one data track
 */

static void lba_to_msf(uint32 lba, uint8 *msf)
{
	lba += 150;		// 2 second pregap
	msf[0] = lba / (60 * 75);
	msf[1] = (lba / 75) % 60;
	msf[2] = lba % 75;
}

bool SysCDReadTOC(void *arg, uint8 *toc)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh || !fh->is_cdrom)
		return false;

	uint8 *p = toc + 2;
	*p++ = 1;			// first track
	*p++ = 1;			// last track

	// Track 1: data
	*p++ = 0;
	*p++ = 0x14;		// ADR 1, data track
	*p++ = 1;
	*p++ = 0;
	*p++ = 0;
	lba_to_msf(0, p);
	p += 3;

	// Lead-out
	*p++ = 0;
	*p++ = 0x14;
	*p++ = 0xaa;
	*p++ = 0;
	*p++ = 0;
	lba_to_msf((uint32)(fh->file_size / 2048), p);
	p += 3;

	int toc_size = p - toc;
	toc[0] = toc_size >> 8;
	toc[1] = toc_size & 0xff;
	return true;
}

bool SysCDGetPosition(void *arg, uint8 *pos)
{
	return false;
}

bool SysCDPlay(void *arg, uint8 start_m, uint8 start_s, uint8 start_f, uint8 end_m, uint8 end_s, uint8 end_f)
{
	return false;
}

bool SysCDPause(void *arg)
{
	return false;
}

bool SysCDResume(void *arg)
{
	return false;
}

bool SysCDStop(void *arg, uint8 lead_out_m, uint8 lead_out_s, uint8 lead_out_f)
{
	return false;
}

bool SysCDScan(void *arg, uint8 start_m, uint8 start_s, uint8 start_f, bool reverse)
{
	return false;
}

void SysCDSetVolume(void *arg, uint8 left, uint8 right)
{
}

void SysCDGetVolume(void *arg, uint8 &left, uint8 &right)
{
	left = right = 255;
}
