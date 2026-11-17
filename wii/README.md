# Basilisk II for the Wii and Wii U

Basilisk II, the 68k Macintosh emulator, running on the Nintendo Wii
(Homebrew Channel) and the Wii U (Aroma). It emulates a 68040 Mac with an
FPU, colour video up to 800x600 (1024x768 on the Wii U), 16 bit stereo
sound, floppy, hard disk and CD-ROM images. With the ROM of a Mac II,
IIci, Quadra 650, 700, 900 and the like it runs System 7 and Mac OS 8.0
and 8.1; with a Mac Plus or Classic ROM it runs the older systems in black
and white.

The code shared by both consoles is in
[`BasiliskII/src/Wii`](../BasiliskII/src/Wii); `plat_wii.cpp` (libogc,
GX, ASND) and `plat_wiiu.cpp` (wut, SDL2) are the only parts that differ.

## Installing

**Wii:** extract `BasiliskIIWii-<version>-hbc.zip` to the root of your SD
card or USB drive. The app is in `sd:/apps/BasiliskIIWii/`.

**Wii U (Aroma):** extract `BasiliskIIWiiU-<version>-wiiu.zip` to the root
of your SD card. Its files go in `sd:/wiiu/apps/BasiliskIIWiiU/`.

Then put into that folder:

* the **Mac ROM**, named `ROM` or `something.rom` (64 KB to 1 MB). If none
  is found, Basilisk II asks you to choose one with the file browser and
  remembers it.
* your **disk images**: `.dsk`, `.img`, `.image`, `.hfv`, `.hda`, `.dc42`
  (images up to 1.44 MB are floppies, bigger ones hard disks) and CD-ROM
  images: `.iso`, `.toast`, `.cdr`. They are all mounted at start, as long
  as the prefs file names no disks itself.

Basilisk II writes its settings to `prefs` in the same folder (the usual
Basilisk II prefs file: `ramsize`, `modelid`, `cpu`, `screen`,
`displaycolordepth`, `frameskip`, `disk`, `floppy`, `cdrom`, `rom`...) and
the PRAM to `xpram`. The defaults are a Quadra 900 with a 68040, 32 MB of
RAM on the Wii and 64 MB on the Wii U, a 640x480 screen in 256 colours.

## Controls

| Wii Remote | Wii U GamePad / Pro / Classic | Mac |
| --- | --- | --- |
| point at the TV | touch the GamePad screen | mouse |
| A, B, Z | A, B | mouse button |
| Nunchuk stick | left stick | move the mouse |
| D-Pad | D-Pad | arrow keys |
| 1 (or C) | Y (or C) | command |
| 2 | X | option |
| **+** | **+** (or R, ZR) | **type text with the keyboard (libwiiosk)** |
| - | - (or L, ZL) | display mode: fill, keep aspect, integer scale |
| HOME | HOME | menu |

USB keyboards and mice work too (on a PC keyboard Alt is command and the
Windows key option). GameCube pads work on the Wii.

**Typing:** + opens a Wii-style software keyboard
([libwiiosk](https://github.com/quatric/libwiiosk)). What you type there is
sent to the Mac key by key when you press OK, as a US Mac keyboard would
type it: shift for capitals, option and option dead keys for accented
letters and the Mac's symbols. Pressing + again while it is still typing
stops it.

The **HOME menu** inserts more disk and CD images (into an empty drive),
changes the display mode and smoothing, opens the text keyboard, shows a
Mac keyboard overlay for special keys (command, option, control, escape,
arrows; click them with the pointer), sends an interrupt (programmer's
switch) and quits. The Mac is paused while the menu is open.

## Updates

On the Wii, [libwiiupdate](https://github.com/quatric/libwiiupdate) checks
this repository's releases in the background and offers to install a
newer version; on the Wii U,
[libwiiuupdate](https://github.com/quatric/libwiiuupdate) does the same.

## Building

You need devkitPro with devkitPPC and libogc (Wii), wut and wiiu-sdl2
(Wii U), and a C/C++ compiler for your own computer: the 68k emulator core
(`cpuemu.cpp` and its tables) is generated at build time by Basilisk II's
`build68k` and `gencpu`, which run on the build machine.

```sh
git submodule update --init
make -C wii          # BasiliskIIWii.dol
make -C wii dist     # Homebrew Channel zip in wii/dist/
make -C wiiu         # BasiliskIIWiiU.wuhb
make -C wiiu dist    # Wii U zip in wiiu/dist/
```

libwiiinput, libwiiupdate and libwiiosk (Wii) and libwiiuinput and
libwiiuupdate (Wii U) are git submodules; the Wii U build uses the Wii's
libwiiosk checkout, with its SDL2 back end.

## What is not there

* **SheepShaver** (the PowerPC Mac emulator in this repository) is not
  ported. Its fast mode runs Mac OS code natively, which needs the host
  OS to map memory at fixed addresses and to hand over segmentation and
  illegal instruction signals: neither console offers that to homebrew.
  Its PowerPC interpreter would leave Mac OS 8.5 and later far too slow
  on these CPUs.
* No JIT: the 68k CPU is interpreted (UAE core), so a Quadra runs slower
  than the real thing, more so on the Wii.
* No networking, serial ports, host folder sharing ("Unix" volume) or
  CD audio tracks.
