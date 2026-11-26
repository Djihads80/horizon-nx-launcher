#---------------------------------------------------------------------------------
# The OpenGL probe: a separate, tiny .nro (no Borealis) that answers one question — does this
# console's GL stack start? Writes each step to sdmc:/switch/horizon/gl-probe.txt.
#
#   packaging/switch-launcher/build.sh probe
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. Export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := gl-probe
BUILD       := build-probe
SOURCES     := .
INCLUDES    := .

APP_TITLE   := Horizon GL probe
APP_AUTHOR  := Horizon
APP_VERSION := 1.0.0

ARCH      := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS    := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS    += $(INCLUDE) -D__SWITCH__
CXXFLAGS  := $(CFLAGS) -std=c++17
LDFLAGS   := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH)

LIBS      := -lglfw3 -lEGL -lglapi -ldrm_nouveau -lGLESv2 -lm -lnx
LIBDIRS   := $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(CURDIR)
export DEPSDIR  := $(CURDIR)/$(BUILD)

CPPFILES    := $(notdir $(wildcard $(TOPDIR)/*.cpp))
CFILES      := $(notdir $(wildcard $(TOPDIR)/*.c))

export LD       := $(CXX)
export OFILES   := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp

.PHONY: all clean

all: | $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro
$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf : $(OFILES)

-include $(DEPENDS)
endif
#---------------------------------------------------------------------------------
