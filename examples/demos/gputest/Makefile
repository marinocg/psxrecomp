# PSn00bSDK gputest demo project for PSXRecomp.
# Requires PSN00BSDK and mkpsxiso in PATH.

PROJECT_NAME := GPUTEST

ifndef PSN00BSDK
$(error PSN00BSDK is not set. Set PSN00BSDK to your PSn00bSDK install root.)
endif

TARGET_EXE := $(PROJECT_NAME).EXE
TARGET_ELF = $(BUILD_DIR)/$(PROJECT_NAME).elf
TARGET_MAP = $(BUILD_DIR)/$(PROJECT_NAME).map
TARGET_ISO := $(PROJECT_NAME).iso
TARGET_CUE := $(PROJECT_NAME).cue

SRC_DIR := src
BUILD_DIR := build

CC := mipsel-none-elf-gcc
ELF2X := $(or $(shell command -v elf2x 2>/dev/null),$(firstword $(wildcard $(PSN00BSDK)/bin/elf2x)))

# Support both PSn00bSDK layouts:
# - CMake install prefix: include/ + lib/psx.ld
# - Repo/prebuilt layout: libpsn00b/include + libpsn00b/ldscripts/exe.ld
SDK_INCLUDE := $(PSN00BSDK)/include
SDK_LIB := $(PSN00BSDK)/lib
SDK_LDSCRIPT := $(SDK_LIB)/psx.ld

ifeq ($(wildcard $(SDK_INCLUDE)/psxapi.h),)
  SDK_INCLUDE := $(PSN00BSDK)/include/libpsn00b
endif

ifeq ($(wildcard $(SDK_INCLUDE)/psxapi.h),)
  SDK_INCLUDE := $(PSN00BSDK)/libpsn00b/include
endif

ifeq ($(wildcard $(SDK_LDSCRIPT)),)
  SDK_LIB := $(PSN00BSDK)/lib/libpsn00b/release
  SDK_LDSCRIPT := $(PSN00BSDK)/lib/libpsn00b/ldscripts/exe.ld
endif

ifeq ($(wildcard $(SDK_LDSCRIPT)),)
  SDK_LIB := $(PSN00BSDK)/libpsn00b/release
  SDK_LDSCRIPT := $(PSN00BSDK)/libpsn00b/ldscripts/exe.ld
endif

ifeq ($(wildcard $(SDK_INCLUDE)/psxapi.h),)
  $(error PSn00bSDK headers not found. Expected $(PSN00BSDK)/include/psxapi.h, $(PSN00BSDK)/include/libpsn00b/psxapi.h, or $(PSN00BSDK)/libpsn00b/include/psxapi.h)
endif

ifeq ($(wildcard $(SDK_LDSCRIPT)),)
  $(error PSn00bSDK linker script not found. Expected $(PSN00BSDK)/lib/psx.ld, $(PSN00BSDK)/lib/libpsn00b/ldscripts/exe.ld, or $(PSN00BSDK)/libpsn00b/ldscripts/exe.ld)
endif

ifeq ($(strip $(ELF2X)),)
  $(error elf2x not found. Expected it in PATH or at $(PSN00BSDK)/bin/elf2x)
endif

ARCH_FLAGS := -march=r3000 -mtune=r3000 -msoft-float

BASE_CFLAGS := $(ARCH_FLAGS) -O2 -ffreestanding -fno-builtin -fno-stack-protector -Wall -Wextra -Werror -I$(SDK_INCLUDE)

LIB_VARIANT := exe_nogprel
GPREL_FLAG := -G0

ifeq ($(wildcard $(SDK_LIB)/libpsxapi_$(LIB_VARIANT).a),)
  $(error Required PSn00bSDK nogprel library variant missing at $(SDK_LIB)/libpsxapi_$(LIB_VARIANT).a)
endif

# Force no-GP-relative mode to avoid header GP=0 runtime faults on real PSX emulators.
CFLAGS := $(BASE_CFLAGS) $(GPREL_FLAG)

ifeq ($(LIB_VARIANT),)
  SDK_LIBS := -lpsxgte -lpsxgpu -lpsxetc -lpsxpress -lpsxapi -lc -llzp
else
  SDK_LIBS := -lpsxgte_$(LIB_VARIANT) -lpsxgpu_$(LIB_VARIANT) -lpsxetc_$(LIB_VARIANT) -lpsxpress_$(LIB_VARIANT) -lpsxapi_$(LIB_VARIANT) -lc_$(LIB_VARIANT) -llzp_$(LIB_VARIANT)
endif

LINK_STARTUP_FLAGS := -nostartfiles

LDFLAGS := $(ARCH_FLAGS) $(LINK_STARTUP_FLAGS) -T$(SDK_LDSCRIPT) -L$(SDK_LIB) -Wl,-Map,$(TARGET_MAP) -Wl,--start-group $(SDK_LIBS) -Wl,--end-group

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

all: $(TARGET_ISO)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_ELF): $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

$(TARGET_EXE): $(TARGET_ELF)
	$(ELF2X) $< $@

$(TARGET_ISO): $(TARGET_EXE) system.cnf mkpsxiso.xml
	mkpsxiso -y -o $(TARGET_ISO) -c $(TARGET_CUE) mkpsxiso.xml

clean:
	rm -rf $(BUILD_DIR) $(TARGET_EXE) $(TARGET_ELF) $(TARGET_MAP) $(TARGET_ISO) $(TARGET_CUE) $(PROJECT_NAME)

.PHONY: all clean
