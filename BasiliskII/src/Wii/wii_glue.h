/*
 *  wii_glue.h - shared declarations of the Wii / Wii U platform code
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef WII_GLUE_H
#define WII_GLUE_H

/* audio_wii.cpp */
extern void audio_tick(void);

/* sys_wii.cpp: put an image file into an empty drive and mount it */
enum { INSERT_FAILED = 0, INSERT_NO_DRIVE, INSERT_DISK, INSERT_CDROM };
extern int SysInsertImage(const char *path);

/* prefs_wii.cpp: change single settings in the prefs file */
extern void SavePrefsLine(const char *name, const char *value);
extern void SaveDisplayPrefs(void);

/* video_wii.cpp: the Mac screen as the UI draws it */
extern int video_width(void);
extern int video_height(void);
/* convert what changed since the last call; true if anything did */
extern bool video_convert(void);
extern void video_mark_dirty(void);

/* ui_wii.cpp */
extern void ui_init(void);
extern void ui_exit(void);
/* once per 60Hz tick from the CPU loop: input, menus, drawing */
extern void ui_tick(bool draw_mac);
/* modal message; returns true for the first button */
extern bool ui_alert(const char *title, const char *text,
	const char *yes, const char *no);
/* pick a file in the browser (for the ROM before the Mac starts);
   false if cancelled */
extern bool ui_choose_file(const char *title, char *path, size_t n);
/* a path as the user knows it ("sd:/..." on the Wii U too) */
extern void ui_display_path(const char *path, char *out, size_t n);
#endif
