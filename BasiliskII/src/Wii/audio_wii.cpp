/*
 *  audio_wii.cpp - Audio support, Wii / Wii U
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Mac's Sound Manager hands over one block at a time when the audio
 *  interrupt runs. Unlike the SDL back end, the output side never waits
 *  for the emulated Mac: the 60Hz tick asks for a block whenever the ring
 *  buffer has room, AudioInterrupt() copies it in, and the console's
 *  output (an ASND voice on the Wii, SDL on the Wii U) plays from the
 *  ring, or silence when it runs dry (also while the menu pauses the Mac).
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "user_strings.h"
#include "audio.h"
#include "audio_defs.h"
#include "plat.h"
#include "wii_glue.h"

#define DEBUG 0
#include "debug.h"

#define MAC_MAX_VOLUME 0x0100

static const int SampleRate = 22050;
static const int BlockFrames = 1024;
static const int RingFrames = 8192;		// a power of two
static const int TargetFrames = 4096;	// keep this much queued

static int16 ring[RingFrames * 2];
static volatile uint32 ring_in = 0;		// frames written (by the Mac side)
static volatile uint32 ring_out = 0;	// frames played (by the output)
static bool irq_pending = false;
static int volume = MAC_MAX_VOLUME;		// 8.8 fixed point
static bool muted = false;

static void fill(int16 *out, int frames)
{
	uint32 in = ring_in;
	uint32 o = ring_out;
	int avail = (int)(in - o);
	int i;

	if (avail > frames)
		avail = frames;
	for (i = 0; i < avail; i++, o++) {
		const int16 *s = &ring[(o & (RingFrames - 1)) * 2];
		if (muted) {
			out[i * 2] = out[i * 2 + 1] = 0;
		} else {
			out[i * 2] = (int16)((s[0] * volume) >> 8);
			out[i * 2 + 1] = (int16)((s[1] * volume) >> 8);
		}
	}
	for (; i < frames; i++)
		out[i * 2] = out[i * 2 + 1] = 0;
	ring_out = o;
}

static void set_audio_status_format(void)
{
	AudioStatus.sample_rate = audio_sample_rates[0];
	AudioStatus.sample_size = audio_sample_sizes[0];
	AudioStatus.channels = audio_channel_counts[0];
}

void AudioInit(void)
{
	// One format: 22050 Hz, 16 bit, stereo
	if (audio_sample_rates.empty()) {
		audio_sample_rates.push_back(SampleRate << 16);
		audio_sample_sizes.push_back(16);
		audio_channel_counts.push_back(2);
	}

	AudioStatus.mixer = 0;
	AudioStatus.num_sources = 0;
	audio_component_flags = cmpWantsRegisterMessage | kStereoOut | k16BitOut;
	set_audio_status_format();

	if (PrefsFindBool("nosound"))
		return;

	audio_frames_per_block = BlockFrames;
	ring_in = ring_out = 0;
	if (!plat_audio_open(SampleRate, fill)) {
		WarningAlert(GetString(STR_NO_AUDIO_WARN));
		return;
	}
	audio_open = true;
}

void AudioExit(void)
{
	if (audio_open)
		plat_audio_close();
	audio_open = false;
}

void audio_enter_stream()
{
}

void audio_exit_stream()
{
}

/* From the 60Hz tick: ask the Mac for the next block when there is room */
void audio_tick(void)
{
	if (!audio_open || irq_pending || AudioStatus.num_sources == 0)
		return;
	if ((int)(ring_in - ring_out) > TargetFrames)
		return;
	irq_pending = true;
	SetInterruptFlag(INTFLAG_AUDIO);
	TriggerInterrupt();
}

/*
 *  MacOS audio interrupt, read next data block
 */
void AudioInterrupt(void)
{
	irq_pending = false;

	if (AudioStatus.mixer) {
		M68kRegisters r;
		r.a[0] = audio_data + adatStreamInfo;
		r.a[1] = AudioStatus.mixer;
		Execute68k(audio_data + adatGetSourceData, &r);
	} else
		WriteMacInt32(audio_data + adatStreamInfo, 0);

	uint32 info = ReadMacInt32(audio_data + adatStreamInfo);
	if (!info)
		return;

	int frames = ReadMacInt32(info + scd_sampleCount);
	uint32 buf = ReadMacInt32(info + scd_buffer);
	if (frames <= 0 || !buf)
		return;

	int room = RingFrames - (int)(ring_in - ring_out);
	if (frames > room)
		frames = room;

	// 16 bit stereo, big-endian like the host
	const int16 *src = (const int16 *)Mac2HostAddr(buf);
	uint32 in = ring_in;
	for (int i = 0; i < frames; i++, in++) {
		int16 *d = &ring[(in & (RingFrames - 1)) * 2];
		d[0] = src[i * 2];
		d[1] = src[i * 2 + 1];
	}
	ring_in = in;
}

bool audio_set_sample_rate(int index)
{
	return index == 0;
}

bool audio_set_sample_size(int index)
{
	return index == 0;
}

bool audio_set_channels(int index)
{
	return index == 0;
}

bool audio_get_main_mute(void)
{
	return muted;
}

uint32 audio_get_main_volume(void)
{
	return ((uint32)volume << 16) | volume;
}

bool audio_get_speaker_mute(void)
{
	return muted;
}

uint32 audio_get_speaker_volume(void)
{
	return audio_get_main_volume();
}

void audio_set_main_mute(bool mute)
{
	muted = mute;
}

void audio_set_main_volume(uint32 vol)
{
	uint32 avg = ((vol >> 16) + (vol & 0xffff)) / 2;
	if (avg > MAC_MAX_VOLUME)
		avg = MAC_MAX_VOLUME;
	volume = avg;
}

void audio_set_speaker_mute(bool mute)
{
	muted = mute;
}

void audio_set_speaker_volume(uint32 vol)
{
	audio_set_main_volume(vol);
}
