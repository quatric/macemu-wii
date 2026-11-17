# Basilisk II and SheepShaver for the Wii and Wii U

The two Mac emulators of this repository, running on the Nintendo Wii
(Homebrew Channel) and the Wii U (Aroma):

* **Basilisk II** emulates a 68040 Mac with an FPU, colour video up to
  800x600 (1024x768 on the Wii U), 16 bit stereo sound, floppy, hard disk
  and CD-ROM images. With the ROM of a Mac II, IIci, Quadra 650, 700, 900
  and the like it runs System 7 and Mac OS 8.0 and 8.1; with a Mac Plus or
  Classic ROM it runs the older systems in black and white.
* **SheepShaver** emulates a PowerPC Mac (a PowerPC 7400 G4, AltiVec
  included) and runs Mac OS 7.5.2 to 9.0.4, with the same screen sizes, sound and disk
  support. Read [how fast it is](#sheepshaver-speed) first.

The code shared by both consoles and both emulators is in
[`BasiliskII/src/Wii`](../BasiliskII/src/Wii); `plat_wii.cpp` (libogc, GX,
ASND) and `plat_wiiu.cpp` (wut, SDL2) are the only parts that differ.
SheepShaver's own part (its memory map, the PowerPC interpreter's clock and
its video driver) is in [`SheepShaver/src/Wii`](../SheepShaver/src/Wii).

## Installing

| | Wii | Wii U (Aroma) |
| --- | --- | --- |
| Basilisk II | `BasiliskIIWii-<version>-hbc.zip` → `sd:/apps/BasiliskIIWii/` | `BasiliskIIWiiU-<version>-wiiu.zip` → `sd:/wiiu/apps/BasiliskIIWiiU/` |
| SheepShaver | `SheepShaverWii-<version>-hbc.zip` → `sd:/apps/SheepShaverWii/` | `SheepShaverWiiU-<version>-wiiu.zip` → `sd:/wiiu/apps/SheepShaverWiiU/` |

Extract the zip to the root of your SD card (on the Wii a USB drive works
too). Then put into the app's folder:

* the **ROM**. If none is found, the emulator asks you to choose one with
  the file browser and remembers it.
  * Basilisk II: a 68k Mac ROM named `ROM` or `something.rom` (64 KB to
    1 MB).
  * SheepShaver: the `Mac OS ROM` file from the System Folder of Mac OS 8.6
    to 9.0.4 (copy it from an install CD or a Mac), or the 4 MB ROM of an
    old world PCI Power Mac, named `ROM`, `Mac OS ROM` or `something.rom`.
* your **disk images**: `.dsk`, `.img`, `.image`, `.hfv`, `.hda`, `.dc42`
  (images up to 1.44 MB are floppies, bigger ones hard disks) and CD-ROM
  images: `.iso`, `.toast`, `.cdr`. They are all mounted at start, as long
  as the prefs file names no disks itself.

Each emulator writes its settings to `prefs` in its folder (the usual
Basilisk II and SheepShaver prefs file: `ramsize`, `screen`,
`displaycolordepth`, `frameskip`, `disk`, `floppy`, `cdrom`, `rom`...) and
the PRAM to `xpram`.

* Basilisk II defaults to a Quadra 900 with a 68040, 32 MB of RAM on the
  Wii and 64 MB on the Wii U, a 640x480 screen in 256 colours.
* SheepShaver defaults to as much RAM as the Wii has room for (47 MB) and
  128 MB on the Wii U, a 640x480 screen in 256 colours. Networking is off.
  With 32 MB Mac OS 9 says there is not enough memory and starts without
  its extensions; a `ramsize` the Wii has no room for is lowered to what
  fits, so a prefs file from an older version can just say `ramsize
  67108864`.

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
switch, Basilisk II only) and quits. The Mac is paused while the menu is
open.

## SheepShaver speed

On a real Power Mac and on PCs, SheepShaver runs Mac OS code natively or
through a JIT compiler. Neither works here: running natively needs the
host to map memory at Mac OS's fixed addresses and to pass on memory
faults and illegal instructions, which the Wii and the Wii U do not offer
homebrew, and SheepShaver's JIT only generates x86 code. So the PowerPC is
**interpreted** (SheepShaver's kpx_cpu interpreter with its decode cache),
with the Mac address space mapped through a table of 64 KB pages.

On the Wii's 729 MHz CPU (measured in Dolphin with a simple integer loop)
that is about 20 million PowerPC instructions a second: roughly a 20-25 MHz
PowerPC 601, slower than any real Power Mac. The 68k parts of Mac OS run
through Mac OS's own 68k emulator on top of that. Expect Mac OS 8.6 or 9 to
take minutes to start up and to feel sluggish; Mac OS 7.5.x and 8.x with an
old world ROM and small applications are the most usable. The Wii U's CPU
is clocked about 70% higher (1.24 GHz) and should be faster to match,
but that is untested. For 68k software, Basilisk II is much faster.

Tested so far, on the Wii in Dolphin: with the New World `Mac OS ROM` file
and a Mac OS 9 hard disk image, SheepShaver starts up with all the
extensions and reaches the Finder in about 3 minutes of Wii time (without a
disk, the flashing "?" floppy shows after about 3 seconds). The Wii U build
has not been tried on a console yet.

## Updates

On the Wii, [libwiiupdate](https://github.com/quatric/libwiiupdate) checks
this repository's releases in the background and offers to install a
newer version; on the Wii U,
[libwiiuupdate](https://github.com/quatric/libwiiuupdate) does the same.
Each release carries all four zips; each app only installs its own.

## Building

You need devkitPro with devkitPPC and libogc (Wii), wut and wiiu-sdl2
(Wii U), perl, and a C/C++ compiler for your own computer: the 68k emulator
core (`cpuemu.cpp` and its tables) is generated at build time by Basilisk
II's `build68k` and `gencpu`, which run on the build machine, and the
PowerPC interpreter's instruction table by kpx_cpu's `genexec.pl`.

```sh
git submodule update --init
make -C wii          # BasiliskIIWii.dol and SheepShaverWii.dol
make -C wii dist     # Homebrew Channel zips in wii/dist/
make -C wiiu         # BasiliskIIWiiU.wuhb and SheepShaverWiiU.wuhb
make -C wiiu dist    # Wii U zips in wiiu/dist/
```

`make -f sheepshaver.mk` in either folder builds SheepShaver alone. With
`EXTRA_CFLAGS=-DTEST_PPC` SheepShaver runs a short PowerPC interpreter
test instead of the Mac and shows its speed.

With `EXTRA_CFLAGS="-DWII_DEBUG_LOG -DPPC_DEBUG_HOOKS"` SheepShaver writes
`debug.txt` in its folder once a second: where the PowerPC is, the Mac's
tick count and the most frequent program counters.

**Testing in Dolphin:** set Graphics > Hacks > Texture Cache Accuracy to
*Safe*. At the default (*Fast*) Dolphin only checks a sample of a
texture's pixels for changes and misses small updates of the Mac screen
(the pointer, an icon, a line of text). The real consoles always show them.

libwiiinput, libwiiupdate and libwiiosk (Wii) and libwiiuinput and
libwiiuupdate (Wii U) are git submodules; the Wii U build uses the Wii's
libwiiosk checkout, with its SDL2 back end.

## What is not there

* No JIT in either emulator: the 68k and PowerPC CPUs are interpreted, so
  a Quadra runs slower than the real thing (more so on the Wii) and a
  Power Mac much slower (see above).
* No networking, serial ports, host folder sharing ("Unix" volume) or
  CD audio tracks.
* SheepShaver: no Mac OS 9.1 or later (as on every SheepShaver host).
