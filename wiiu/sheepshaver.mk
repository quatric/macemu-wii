#---------------------------------------------------------------------------------
# SheepShaver for the Nintendo Wii U (Aroma)
#
#   make -f sheepshaver.mk          SheepShaverWiiU.wuhb
#   make -f sheepshaver.mk dist     wiiu/apps/... folder and zip in dist/
#
# (make and make dist in this folder build Basilisk II and SheepShaver.)
# Needs devkitPPC, wut and wiiu-sdl2, and perl: the PowerPC interpreter's
# instruction table is generated at build time.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitPro")
endif
export DEVKITPPC ?= $(DEVKITPRO)/devkitPPC

include $(DEVKITPRO)/wut/share/wut_rules

VERSION       ?= 1.0.0
UPDATE_OWNER  ?= quatric
UPDATE_REPO   ?= macemu-wii

APP_FOLDER    := SheepShaverWiiU
APP_TITLE     := SheepShaver Wii U
TARGET        := $(APP_FOLDER)
BUILD         := build-ss
GEN           := $(BUILD)/gen

SS            := ../SheepShaver/src
B2            := ../BasiliskII/src
KPX           := $(SS)/kpx_cpu
WIIUINPUT     := libwiiuinput
WIIUUPDATE    := libwiiuupdate
WIIOSK        := ../wii/libwiiosk

# the controller library without the microphones and pen tablets
INPUT_OPTS    := WITH_MIC=0 WITH_TABLET=0

PERL          ?= perl

APP_NAME      := $(APP_TITLE)
APP_SHORTNAME := SheepShaver
APP_AUTHOR    := quatric
APP_ICON      := sheepshaver-icon.png
APP_TV_SPLASH := sheepshaver-splash-tv.png
APP_DRC_SPLASH := sheepshaver-splash-drc.png
# wut_rules read these when it was included; set the options again here
WUHB_OPTIONS  := --name "$(APP_NAME)" --short-name "$(APP_SHORTNAME)" \
                 --author "$(APP_AUTHOR)" --icon=$(APP_ICON) \
                 --tv-image=$(APP_TV_SPLASH) --drc-image=$(APP_DRC_SPLASH)

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
                 user_strings_wii.cpp audio_wii.cpp ui_wii.cpp plat_wiiu.cpp

CORE_OBJS     := $(addprefix $(BUILD)/ss/,$(CORE_SRCS:.cpp=.o))
SSPLAT_OBJS   := $(addprefix $(BUILD)/sswiiu/,$(SSPLAT_SRCS:.cpp=.o))
PLAT_OBJS     := $(addprefix $(BUILD)/wiiu/,$(PLAT_SRCS:.cpp=.o))
OBJS          := $(CORE_OBJS) $(SSPLAT_OBJS) $(PLAT_OBJS)

INCLUDE       := -I$(SS)/Wii -I$(B2)/Wii -I$(GEN) -I$(SS)/include -I$(SS) \
                 -I$(KPX)/include -I$(KPX)/src \
                 -I$(WIIUINPUT)/include -I$(WIIUUPDATE)/include -I$(WIIOSK)/include \
                 -I$(WUT_ROOT)/include $(foreach dir,$(PORTLIBS),-I$(dir)/include)

DEFS          := -DSHEEPSHAVER_BUILD -D__WIIU__ -D__WUT__ -DAPP_VERSION=\"$(VERSION)\" \
                 -DUPDATE_OWNER=\"$(UPDATE_OWNER)\" -DUPDATE_REPO=\"$(UPDATE_REPO)\" \
                 -DAPP_FOLDER=\"$(APP_FOLDER)\" -DAPP_TITLE="\"$(APP_TITLE)\"" \
                 -DUPDATE_ASSET=\"SheepShaver\"
CFLAGS        := -O2 -Wall -Wno-unused -Wno-sign-compare -fno-strict-aliasing \
                 -ffunction-sections $(MACHDEP) $(INCLUDE) $(DEFS) $(EXTRA_CFLAGS)
CXXFLAGS      := $(CFLAGS) -Wno-narrowing -Wno-register -std=gnu++14
LDFLAGS       := $(MACHDEP) $(RPXSPECS) -Wl,--gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS          := -L$(WIIUINPUT)/lib -L$(WIIUUPDATE)/lib -L$(WIIOSK)/lib \
                 $(foreach dir,$(PORTLIBS),-L$(dir)/lib) -L$(WUT_ROOT)/lib \
                 -lwiiuupdate -lwiiuosk -linput-wiiu -lSDL2 -lwut -lm

INPUT_LIB     := $(WIIUINPUT)/lib/libinput-wiiu.a
UPDATE_LIB    := $(WIIUUPDATE)/lib/libwiiuupdate.a
OSK_LIB       := $(WIIOSK)/lib/libwiiuosk.a

all: $(TARGET).wuhb

$(TARGET).wuhb: $(TARGET).rpx $(APP_ICON) $(APP_TV_SPLASH) $(APP_DRC_SPLASH)
	@wuhbtool $< $@ $(WUHB_OPTIONS)
	@echo built ... $@

$(TARGET).rpx: $(BUILD)/$(TARGET).elf
	@elf2rpl $< $@
	@echo built ... $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(INPUT_LIB) $(UPDATE_LIB) $(OSK_LIB)
	@echo linking ... $(notdir $@)
	@$(CXX) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

$(INPUT_LIB):
	@$(MAKE) --no-print-directory -C $(WIIUINPUT) $(INPUT_OPTS)

$(UPDATE_LIB):
	@$(MAKE) --no-print-directory -C $(WIIUUPDATE)

$(OSK_LIB):
	@$(MAKE) --no-print-directory -C $(WIIOSK) -f Makefile.wiiu

#---------------------------------------------------------------------------------
# the PowerPC interpreter's instruction table
#---------------------------------------------------------------------------------
$(GEN)/ppc-execute-impl.cpp: $(KPX)/src/cpu/ppc/ppc-decode.cpp $(KPX)/src/cpu/ppc/genexec.pl
	@mkdir -p $(GEN)
	@echo generating ppc-execute-impl.cpp
	@$(CXX) -E $(CXXFLAGS) -DGENEXEC $< | $(PERL) $(KPX)/src/cpu/ppc/genexec.pl > $@.tmp
	@mv $@.tmp $@

#---------------------------------------------------------------------------------
# SheepShaver and the shared Wii / Wii U code
#---------------------------------------------------------------------------------
# Rebuild the Wii U code when VERSION, the update repository or EXTRA_CFLAGS change
FLAGSTAMP := $(BUILD)/flags
$(shell mkdir -p $(BUILD); echo '$(CXXFLAGS)' | cmp -s - $(FLAGSTAMP) \
	|| echo '$(CXXFLAGS)' > $(FLAGSTAMP))

$(CORE_OBJS): $(BUILD)/ss/%.o: $(SS)/%.cpp $(GEN)/ppc-execute-impl.cpp
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(SSPLAT_OBJS): $(BUILD)/sswiiu/%.o: $(SS)/Wii/%.cpp sheepshaver.mk $(FLAGSTAMP)
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(PLAT_OBJS): $(BUILD)/wiiu/%.o: $(B2)/Wii/%.cpp sheepshaver.mk $(FLAGSTAMP)
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

#---------------------------------------------------------------------------------
DISTDIR := dist/wiiu/apps/$(APP_FOLDER)

dist: $(TARGET).wuhb
	@rm -rf $(DISTDIR)
	@mkdir -p $(DISTDIR)
	@cp $(TARGET).wuhb $(DISTDIR)/
	@cd dist && rm -f $(APP_FOLDER)-$(VERSION)-wiiu.zip \
		&& zip -qr $(APP_FOLDER)-$(VERSION)-wiiu.zip wiiu/apps/$(APP_FOLDER)
	@echo built dist/$(APP_FOLDER)-$(VERSION)-wiiu.zip

clean:
	@rm -rf $(BUILD) $(DISTDIR) dist/$(APP_FOLDER)-*.zip $(TARGET).rpx $(TARGET).wuhb

.PHONY: all dist clean
