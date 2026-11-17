/*
 *  user_strings_wii.cpp - Wii / Wii U specific localizable strings
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "sysdeps.h"
#include "user_strings.h"

// Platform-specific string definitions
user_string_def platform_strings[] = {
	{STR_NO_AUDIO_WARN, "No sound output could be opened. Basilisk II runs without sound."},
	{STR_NO_ROM_FOUND_ERR, "No Macintosh ROM was found."},
	{STR_NO_XVISUAL_ERR, "No usable video mode."},
	{-1, NULL}	// End marker
};

/*
 *  Fetch pointer to string, given the string number
 */
const char *GetString(int num)
{
	// First search for platform-specific string
	int i = 0;
	while (platform_strings[i].num >= 0) {
		if (platform_strings[i].num == num)
			return platform_strings[i].str;
		i++;
	}

	// Not found, search for common string
	i = 0;
	while (common_strings[i].num >= 0) {
		if (common_strings[i].num == num)
			return common_strings[i].str;
		i++;
	}
	return NULL;
}
