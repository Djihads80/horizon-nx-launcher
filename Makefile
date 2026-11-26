#---------------------------------------------------------------------------------
# Horizon launcher for the Nintendo Switch: a homebrew app (.nro) built on Borealis.
#
#   ../borealis-legacy   the UI library, cloned by build.sh (pinned revision)
#   romfs/               fonts, i18n and the launcher's own pictures
#   third_party/         nlohmann/json and the QR encoder
#
# Keep BOREALIS_PATH relative: borealis.mk prefixes it with TOPDIR.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. Export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := horizon-launcher
BUILD       := build
SOURCES     := source
INCLUDES    := source third_party
ROMFS       := romfs

APP_TITLE   := Horizon Launcher
APP_AUTHOR  := Horizon
APP_VERSION := 1.0.0

BOREALIS_PATH ?= borealis
ICON        ?= $(TOPDIR)/icon.jpg

ARCH      := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS    := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS    += $(INCLUDE) -D__SWITCH__

CXXFLAGS  := $(CFLAGS) -std=c++17 -Wno-volatile

LDFLAGS   := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS      := -lnx

LIBDIRS   := $(PORTLIBS) $(LIBNX)

include $(TOPDIR)/$(BOREALIS_PATH)/library/borealis.mk

# Borealis reads its fonts and pictures from the app's romfs; the define lives in the demo's
# Makefile upstream, not in borealis.mk. (Legacy spells it BOREALIS_RESOURCES.)
CXXFLAGS := $(CXXFLAGS) -DBOREALIS_RESOURCES="\"romfs:/\""

# The Microsoft client ID is not passed as a -D here: build.sh writes build/build_config.hpp with
# it, which the compiler tracks as a dependency, so a changed ID always triggers a rebuild.

# The launcher's own libraries, after borealis' own list. `-lnx` comes again after curl: curl's
# object files call libnx's sockets (`getaddrinfo`, `inet_pton`), and a static archive only pulls
# what is still missing where it sits.
LIBS      += -lcurl -lnx -lz

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(CURDIR)/third_party

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))) \
               $(notdir $(wildcard $(TOPDIR)/third_party/*.c))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp))) \
               $(notdir $(wildcard $(TOPDIR)/third_party/*.cpp))

export LD   := $(CXX)

export OFILES     := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON := $(ICON)
export NROFLAGS += --icon=$(APP_ICON) --nacp=$(CURDIR)/$(TARGET).nacp
export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)

.PHONY: all clean

all: | $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp

$(OUTPUT).elf : $(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
