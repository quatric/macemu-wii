/*
 *  prefs_wii.cpp - Preferences handling, Wii / Wii U specific stuff
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "sysdeps.h"
#include "prefs.h"
#include "plat.h"

// Platform-specific preferences items
prefs_desc platform_prefs_items[] = {
	{"displaymode", TYPE_INT32, false,  "0 = fill the screen, 1 = keep aspect, 2 = integer scale"},
	{"smoothing", TYPE_BOOLEAN, false,  "smooth the scaled Mac screen"},
	{NULL, TYPE_END, false, NULL} // End of list
};

// Prefs file, in the app folder ("prefs", like Basilisk II's vmdir)
static char prefs_path[512];

void LoadPrefs(const char *vmdir)
{
	(void)vmdir;
	snprintf(prefs_path, sizeof(prefs_path), "%sprefs", plat_app_dir());

	FILE *f = fopen(prefs_path, "r");
	if (f != NULL) {
		LoadPrefsFromStream(f);
		fclose(f);
	} else {
		// No prefs file, save the defaults so they can be edited
		SavePrefs();
	}
}

void SavePrefs(void)
{
	FILE *f = fopen(prefs_path, "w");
	if (f != NULL) {
		SavePrefsToStream(f);
		fclose(f);
	}
}

/* Set `name` to `value` in the prefs file, leaving the other lines as they
   are, so that what main_wii.cpp added for this run only (disks found in
   the app folder, the empty drives) is not saved */
void SavePrefsLine(const char *name, const char *value)
{
	char *text = NULL;
	size_t name_len = strlen(name);

	FILE *f = fopen(prefs_path, "r");
	if (f != NULL) {
		fseek(f, 0, SEEK_END);
		long len = ftell(f);
		fseek(f, 0, SEEK_SET);
		text = (char *)malloc(len + 1);
		if (text == NULL) {
			fclose(f);
			return;
		}
		len = fread(text, 1, len, f);
		text[len < 0 ? 0 : len] = 0;
		fclose(f);
	}

	f = fopen(prefs_path, "w");
	if (f == NULL) {
		free(text);
		return;
	}
	char *line = text;
	while (line != NULL && *line) {
		char *next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		if (!(strncmp(line, name, name_len) == 0 && line[name_len] == ' '))
			fprintf(f, "%s\n", line);
		line = next;
	}
	fprintf(f, "%s %s\n", name, value);
	fclose(f);
	free(text);
}

void SaveDisplayPrefs(void)
{
	char v[16];
	snprintf(v, sizeof(v), "%d", (int)PrefsFindInt32("displaymode"));
	SavePrefsLine("displaymode", v);
	SavePrefsLine("smoothing", PrefsFindBool("smoothing") ? "true" : "false");
}

void AddPlatformPrefsDefaults(void)
{
	// A Quadra-class Mac: most ROMs people have (Quadra 650/700/900, IIci)
	// boot Mac OS 7.x to 8.1 like this
	PrefsReplaceInt32("ramsize", plat_default_ram_mb() * 1024 * 1024);
	PrefsReplaceInt32("modelid", 14);	// Quadra 900
	PrefsReplaceInt32("cpu", 4);		// 68040
	PrefsReplaceBool("fpu", true);
	PrefsReplaceInt32("frameskip", 2);	// 30 screen updates a second
	PrefsReplaceString("screen", "win/640/480");
	PrefsReplaceInt32("displaycolordepth", 8);
	PrefsAddInt32("displaymode", 1);
	PrefsAddBool("smoothing", true);
}
