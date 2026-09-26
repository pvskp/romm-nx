#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# Frontend flavor: which frontend this build serves. Each flavor is a separate
# app (own NRO, install folder, config and sync state) so Tico and RetroArch
# builds can coexist on the same SD card.
#   make              -> tico flavor (romm-nx-tico.nro)
#   make FRONTEND=retroarch (or `make retroarch`) -> romm-nx-retroarch.nro
#---------------------------------------------------------------------------------
FRONTEND	?=	tico
ifeq ($(FRONTEND),tico)
	TARGET	:=	romm-nx-tico
else ifeq ($(FRONTEND),retroarch)
	TARGET	:=	romm-nx-retroarch
else
$(error "FRONTEND must be 'tico' or 'retroarch' (got '$(FRONTEND)')")
endif

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
#---------------------------------------------------------------------------------
BUILD		:=	build-$(FRONTEND)
SOURCES		:=	source source/ui source/model source/navigation source/i18n
DATA		:=	data
INCLUDES	:=	include temp_plutonium/Plutonium/include
ROMFS		:=	romfs

APP_AUTHOR	:=	pvskp
# Single source of truth: source/Version.hpp
# Extract version string and code at build time so NRO nacp metadata stays in sync.
APP_VERSION	:=	$(shell grep 'ROMM_NX_VERSION ' $(TOPDIR)/source/Version.hpp | grep -o '[0-9][0-9.]*')
APP_VERSION_CODE := $(shell grep 'ROMM_NX_VERSION_CODE' $(TOPDIR)/source/Version.hpp | grep -o '[0-9][0-9]*')

# The app title shown in the Homebrew Menu carries the flavor and the exact
# version it was built from, so the two NROs (and their update channel) are
# distinguishable at a glance.
APP_TITLE	:=	Romm-NX ($(if $(filter retroarch,$(FRONTEND)),RetroArch,Tico)) v$(APP_VERSION)

DEFINES		:=	-DAPP_VERSION_STR=\"$(APP_VERSION)\" -DAPP_VERSION_CODE=$(APP_VERSION_CODE) \
			-DROMM_FRONTEND_$(shell echo $(FRONTEND) | tr a-z A-Z)

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__

# Plutonium works with exceptions and RTTI disabled.
CXXFLAGS	:= $(CFLAGS) -std=gnu++20 -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lpu -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lSDL2_mixer -lopusfile -lopus -lmodplug -lmpg123 -lvorbisidec -logg -lSDL2_ttf -lSDL2_gfx -lSDL2_image -lSDL2 -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lwebp -lpng -ljpeg -lfreetype -lharfbuzz -lz -lbz2 -lnx

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX) $(TOPDIR)/temp_plutonium/Plutonium

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

# Each flavor compiles only its own frontend profile + catalog translation
# units; the other flavor's files are excluded so no dual-frontend code or
# catalog table ever ships in the build.
EXCLUDED_CPP := $(if $(filter tico,$(FRONTEND)),FrontendRetroArch.cpp RetroArchCatalog.cpp,FrontendTico.cpp TicoCatalog.cpp)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(filter-out $(EXCLUDED_CPP),$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp))))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard *.json)
	ifneq (,$(findstring $(TARGET).json,$(jsons)))
		export APP_JSON := $(TOPDIR)/$(TARGET).json
	else
		ifneq (,$(findstring config.json,$(jsons)))
			export APP_JSON := $(TOPDIR)/config.json
		endif
	endif
else
	export APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	ifneq ($(APP_ICON),)
		export NROFLAGS += --icon=$(APP_ICON)
	endif
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all tico retroarch clean-tico clean-retroarch

#---------------------------------------------------------------------------------
# The default flavor is tico; the convenience targets build (or clean) one
# specific flavor regardless of the FRONTEND variable.
#---------------------------------------------------------------------------------
all: $(BUILD)

tico:
	@$(MAKE) --no-print-directory FRONTEND=tico

retroarch:
	@$(MAKE) --no-print-directory FRONTEND=retroarch

clean-tico:
	@rm -fr build-tico romm-nx-tico.nro romm-nx-tico.nacp romm-nx-tico.elf

clean-retroarch:
	@rm -fr build-retroarch romm-nx-retroarch.nro romm-nx-retroarch.nacp romm-nx-retroarch.elf

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean: clean-tico clean-retroarch
	@echo clean ...

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all	:	$(OUTPUT).nro

ifeq ($(strip $(NO_NACP)),)
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
else
$(OUTPUT).nro	:	$(OUTPUT).elf
endif

$(OUTPUT).elf	:	$(OFILES)

# APP_VERSION is scraped out of Version.hpp above, but libnx's stock
# `%.nacp: $(MAKEFILE_LIST)` rule only knows about the Makefile — so bumping the
# version alone used to leave the previous version baked into the NRO's nacp
# metadata. Name the real source as a prerequisite so it regenerates.
$(OUTPUT).nacp	:	$(TOPDIR)/source/Version.hpp

$(OFILES_SRC)	: $(HFILES_BIN)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
