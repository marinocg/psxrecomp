# PSn00bSDK memorytest demo project for PSXRecomp.
# Requires PSN00BSDK and mkpsxiso in PATH.

PROJECT_NAME := MEMTEST

ifndef PSN00BSDK
$(error PSN00BSDK is not set. Set PSN00BSDK to your PSn00bSDK install root.)
endif

TARGET_EXE := $(PROJECT_NAME).EXE
TARGET_ISO := $(PROJECT_NAME).iso
TARGET_CUE := $(PROJECT_NAME).cue

SRC_DIR := src
BUILD_DIR := build

CC := mipsel-none-elf-gcc
CFLAGS := -O2 -G0 -Wall -Wextra -Werror -I$(PSN00BSDK)/include
LDFLAGS := -T$(PSN00BSDK)/lib/psx.ld -L$(PSN00BSDK)/lib -lpsxgte -lpsxgpu -lpsxapi -lpsxetc -lpsxpress

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

all: $(TARGET_ISO)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_EXE): $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

$(TARGET_ISO): $(TARGET_EXE) system.cnf mkpsxiso.xml
	mkpsxiso -y mkpsxiso.xml

clean:
	rm -rf $(BUILD_DIR) $(TARGET_EXE) $(TARGET_ISO) $(TARGET_CUE)

.PHONY: all clean
