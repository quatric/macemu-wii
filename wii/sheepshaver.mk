#---------------------------------------------------------------------------------
# SheepShaver for the Nintendo Wii
#
#   make -f sheepshaver.mk          SheepShaverWii.dol
#   make -f sheepshaver.mk dist     Homebrew Channel folder and zip in dist/
#
# (make and make dist in this folder build Basilisk II and SheepShaver.)
# Needs devkitPPC and libogc, and perl: the PowerPC interpreter's
# instruction table is generated at build time.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

VERSION       ?= 1.0.0
UPDATE_OWNER  ?= quatric
UPDATE_REPO   ?= macemu-wii

APP_FOLDER    := SheepShaverWii
APP_TITLE     := SheepShaver Wii
TARGET        := $(APP_FOLDER)
BUILD         := build-ss
GEN           := $(BUILD)/gen

SS            := ../SheepShaver/src
B2            := ../BasiliskII/src
KPX           := $(SS)/kpx_cpu
WIIINPUT      := libwiiinput
WIIUPDATE     := libwiiupdate
WIIOSK        := libwiiosk

PERL          ?= perl

#---------------------------------------------------------------------------------
# sources
#---------------------------------------------------------------------------------
CORE_SRCS     := main.cpp prefs.cpp prefs_items.cpp rom_patches.cpp rsrc_patches.cpp \
                 emul_op.cpp name_registry.cpp macos_util.cpp timer.cpp xpram.cpp \
                 adb.cpp sony.cpp disk.cpp cdrom.cpp scsi.cpp gfxaccel.cpp video.cpp \
                 audio.cpp ether.cpp thunks.cpp serial.cpp user_strings.cpp \
                 dummy/clip_dummy.cpp dummy/ether_dummy.cpp dummy/serial_dummy.cpp \
                 dummy/scsi_dummy.cpp dummy/prefs_editor_dummy.cpp \
                 kpx_cpu/sheepshaver_glue.cpp \
                 kpx_cpu/src/mathlib/ieeefp.cpp kpx_cpu/src/mathlib/mathlib.cpp \
                 kpx_cpu/src/cpu/ppc/ppc-cpu.cpp kpx_cpu/src/cpu/ppc/ppc-decode.cpp \
                 kpx_cpu/src/cpu/ppc/ppc-execute.cpp kpx_cpu/src/cpu/ppc/ppc-translate.cpp \
                 kpx_cpu/src/utils/utils-cpuinfo.cpp
SSPLAT_SRCS   := main_wii.cpp video_wii.cpp
PLAT_SRCS     := prefs_wii.cpp sys_wii.cpp timer_wii.cpp xpram_wii.cpp \
                 user_strings_wii.cpp audio_wii.cpp ui_wii.cpp plat_wii.cpp

INPUT_SRCS    := $(wildcard $(WIIINPUT)/source/*.c)

CORE_OBJS     := $(addprefix $(BUILD)/ss/,$(CORE_SRCS:.cpp=.o))
SSPLAT_OBJS   := $(addprefix $(BUILD)/sswii/,$(SSPLAT_SRCS:.cpp=.o))
PLAT_OBJS     := $(addprefix $(BUILD)/wii/,$(PLAT_SRCS:.cpp=.o))
INPUT_OBJS    := $(patsubst $(WIIINPUT)/source/%.c,$(BUILD)/wiiinput/%.o,$(INPUT_SRCS))
OBJS          := $(CORE_OBJS) $(SSPLAT_OBJS) $(PLAT_OBJS) $(INPUT_OBJS)

INCLUDE       := -I$(SS)/Wii -I$(B2)/Wii -I$(GEN) -I$(SS)/include -I$(SS) \
                 -I$(KPX)/include -I$(KPX)/src \
                 -I$(WIIINPUT)/include -I$(WIIUPDATE)/include -I$(WIIOSK)/include \
                 -I$(LIBOGC_INC) $(foreach dir,$(PORTLIBS),-I$(dir)/include)

DEFS          := -DSHEEPSHAVER_BUILD -DAPP_VERSION=\"$(VERSION)\" -DUPDATE_OWNER=\"$(UPDATE_OWNER)\" \
                 -DUPDATE_REPO=\"$(UPDATE_REPO)\" -DAPP_FOLDER=\"$(APP_FOLDER)\" \
                 -DAPP_TITLE="\"$(APP_TITLE)\"" -DUPDATE_ASSET=\"SheepShaver\"
CFLAGS        := -O2 -Wall -Wno-unused -Wno-sign-compare -fno-strict-aliasing \
                 -ffunction-sections $(MACHDEP) $(INCLUDE) $(DEFS) $(EXTRA_CFLAGS)
CXXFLAGS      := $(CFLAGS) -Wno-narrowing -Wno-register -std=gnu++14
LDFLAGS       := $(MACHDEP) -Wl,--gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS          := -L$(WIIUPDATE)/lib -L$(WIIOSK)/lib -L$(LIBOGC_LIB) \
                 $(foreach dir,$(PORTLIBS),-L$(dir)/lib) \
                 -lwiiupdate -lwiiosk -lwiikeyboard -lasnd -lfat -lwiiuse -lbte -logc -lm

all: $(TARGET).dol

$(TARGET).dol: $(BUILD)/$(TARGET).elf
	@echo output ... $@
	@elf2dol $< $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(WIIUPDATE)/lib/libwiiupdate.a $(WIIOSK)/lib/libwiiosk.a
	@echo linking ... $(notdir $@)
	@$(CXX) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

$(WIIUPDATE)/lib/libwiiupdate.a:
	@$(MAKE) --no-print-directory -C $(WIIUPDATE)

$(WIIOSK)/lib/libwiiosk.a:
	@$(MAKE) --no-print-directory -C $(WIIOSK)

#---------------------------------------------------------------------------------
# the PowerPC interpreter's instruction table
#---------------------------------------------------------------------------------
$(GEN)/ppc-execute-impl.cpp: $(KPX)/src/cpu/ppc/ppc-decode.cpp $(KPX)/src/cpu/ppc/genexec.pl
	@mkdir -p $(GEN)
	@echo generating ppc-execute-impl.cpp
	@$(CXX) -E $(CXXFLAGS) -DGENEXEC $< | $(PERL) $(KPX)/src/cpu/ppc/genexec.pl > $@.tmp
	@mv $@.tmp $@

#---------------------------------------------------------------------------------
# SheepShaver, the shared Wii code and libwiiinput
#---------------------------------------------------------------------------------
# Rebuild the Wii code when VERSION, the update repository or EXTRA_CFLAGS change
FLAGSTAMP := $(BUILD)/flags
$(shell mkdir -p $(BUILD); echo '$(CXXFLAGS)' | cmp -s - $(FLAGSTAMP) \
	|| echo '$(CXXFLAGS)' > $(FLAGSTAMP))

$(CORE_OBJS): $(BUILD)/ss/%.o: $(SS)/%.cpp $(GEN)/ppc-execute-impl.cpp
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(SSPLAT_OBJS): $(BUILD)/sswii/%.o: $(SS)/Wii/%.cpp sheepshaver.mk $(FLAGSTAMP)
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(PLAT_OBJS): $(BUILD)/wii/%.o: $(B2)/Wii/%.cpp sheepshaver.mk $(FLAGSTAMP)
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(INPUT_OBJS): $(BUILD)/wiiinput/%.o: $(WIIINPUT)/source/%.c
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -w -c $< -o $@

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

#---------------------------------------------------------------------------------
DISTDIR := dist/apps/$(APP_FOLDER)

dist: $(TARGET).dol
	@rm -rf $(DISTDIR)
	@mkdir -p $(DISTDIR)
	@cp $(TARGET).dol $(DISTDIR)/boot.dol
	@cp sheepshaver-icon.png $(DISTDIR)/icon.png
	@sed -e 's/@NAME@/$(APP_TITLE)/' -e 's/@VERSION@/$(VERSION)/' \
	     -e 's/@FOLDER@/$(APP_FOLDER)/' \
	     -e "s/@DATE@/$$(date +%Y%m%d%H%M%S)/" sheepshaver-meta.xml.in > $(DISTDIR)/meta.xml
	@cd dist && rm -f $(APP_FOLDER)-$(VERSION)-hbc.zip \
		&& zip -qr $(APP_FOLDER)-$(VERSION)-hbc.zip apps/$(APP_FOLDER)
	@echo built dist/$(APP_FOLDER)-$(VERSION)-hbc.zip

clean:
	@rm -rf $(BUILD) dist/apps/$(APP_FOLDER) dist/$(APP_FOLDER)-*.zip $(TARGET).dol

.PHONY: all dist clean
