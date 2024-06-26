#
# Makefile for ESP8266 projects
#
# Based on the example in the book 'Kolban's Book on ESP8266'
# from Neil Koban (http://neilkolban.com/tech/esp8266/)
#
# dependencies:
#
# (1) Espressif SDK
# (2) ESP8266 toolchain with Xtensa LX compiler
# (3) Bourne Shell (for Windows consider using MinGW MSYS)
#

# project name
VERSION = 0.9.3.2
TARGET = sleeper-${VERSION}

# source subdirectories
MODULES = user

# libraries, mainly provided by the SDK
LIBS = c m gcc phy pp net80211 lwip wpa crypto main json

# ESP8266 flash parameters (dependent on ESP8266 hardware)
#FLASHSIZE = 4m
#FLASHMODE = qio
FLASHSIZE = 8m
#FLASHMODE = dio
#FLASHSIZE = detect
#FLASHPARMS = --flash_freq 40m --flash_mode ${FLASHMODE} --flash_size ${FLASHSIZE}
FLASHPARMS = --flash_size ${FLASHSIZE}

# flash tool parameters (dependent on PC UART hardware)
ESPPORT = COM5
#ESPBAUD = 74880

# Xtensa LX compiler path (4.5.0 for SDK < 3.0)
XTENSA_TOOLS_ROOT ?= c:/Espressif/xtensa-lx106-elf/bin

# Espressif SDK
# SDK 1.5.3 - 18.04.2016
# SDK 2.2.0 - 26.12.2018
# SDK 2.2.1 - 27.11.2019
SDK_BASE  ?= C:/Espressif/ESP8266_NONOS_SDK-2.2.1
SDK_TOOLS ?= C:/Espressif/utils/ESP8266
#ESPTOOL   ?= ${SDK_TOOLS}/esptool.exe
ESPTOOL   ?= python C:/Espressif/esptool/esptool.py
#ESPINITDATA = $(SDK_BINDIR)/esp_init_data_default.bin
#ESPINITDATA = $(SDK_BINDIR)/esp_init_data_default_v08.bin
ESPINITDATA = bin/esp_init_data_setting.bin

#
# Normally nothing to configure south of here.
#

# Espressif SDK subdirectories
SDK_BINDIR = bin
SDK_LIBDIR = lib
SDK_LDDIR  = ld
SDK_INCDIR = include

# compiler flags
CFLAGS = -Os -O2 \
         -std=c99 \
         -g \
         -Wpointer-arith \
         -Wundef \
         -Werror \
         -Wl,-EL \
         -fno-inline-functions \
         -nostdlib \
         -mlongcalls \
         -mtext-section-literals \
         -ffunction-sections \
         -fdata-sections \
         -Wno-implicit-function-declaration \
         -mno-serialize-volatile \
         -D__ets__ \
         -DICACHE_FLASH

# linker flags
LDFLAGS = -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static

# linker script
LD_SCRIPT = eagle.app.v6.ld

# select compiler, librarian and linker
CC      := $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-gcc
AR      := $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-ar
LD      := $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-gcc
OBJCOPY := $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-objcopy
OBJDUMP := $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-objdump

# setup input and output paths as well as build objects
INCDIR        = include
BUILD_BASE    = build
FW_BASE       = firmware
SDK_BINDIR    := $(addprefix $(SDK_BASE)/, $(SDK_BINDIR))
SDK_LIBDIR    := $(addprefix $(SDK_BASE)/, $(SDK_LIBDIR))
SDK_INCDIR    := $(addprefix -I$(SDK_BASE)/, $(SDK_INCDIR))
INCDIR        := $(addprefix -I, $(INCDIR))
LD_SCRIPT     := $(addprefix -T$(SDK_BASE)/$(SDK_LDDIR)/, $(LD_SCRIPT))
LIBS          := $(addprefix -l, $(LIBS))
APP_AR        := $(addprefix $(BUILD_BASE)/, $(TARGET)_app.a)
TARGET_OUT    := $(addprefix $(BUILD_BASE)/, $(TARGET).out)
BUILD_DIR     = $(addprefix $(BUILD_BASE)/, $(MODULES)) $(FW_BASE)
SRC           = $(foreach moduleDir, $(MODULES), $(wildcard $(moduleDir)/*.c))
OBJS          = $(patsubst %.c, $(BUILD_BASE)/%.o, $(SRC))

all: checkdirs $(TARGET_OUT)
	@echo "ESP8266 firmware files for $(TARGET) built!"

# create project archive
$(APP_AR): $(OBJS)
	$(AR) -cru $(APP_AR) $(OBJS)

# compile C source files
$(BUILD_BASE)/%.o : %.c
	$(CC) $(INCDIR) $(MODULE_INCDIR) $(SDK_INCDIR) $(CFLAGS) -DSLEEPER_VERSION=\"${VERSION}\" -c $< -o $@

# check required build directories
checkdirs: $(BUILD_DIR)

# create build directory structure
$(BUILD_DIR):
	@mkdir --parents --verbose $@

# create ESP8266 firmware files
$(TARGET_OUT): $(APP_AR)
	$(LD) -L$(SDK_LIBDIR) $(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $(LIBS) $(APP_AR) -Wl,--end-group -o $@
	$(OBJDUMP) --headers --section=.data \
                       --section=.rodata \
                       --section=.bss \
                       --section=.text \
                       --section=.irom0.text $@
	$(SDK_TOOLS)/memanalyzer.exe $(OBJDUMP).exe $@
	$(OBJCOPY) --only-section .text --output-target binary $@ eagle.app.v6.text.bin
	$(OBJCOPY) --only-section .data --output-target binary $@ eagle.app.v6.data.bin
	$(OBJCOPY) --only-section .rodata --output-target binary $@ eagle.app.v6.rodata.bin
	$(OBJCOPY) --only-section .irom0.text --output-target binary $@ eagle.app.v6.irom0text.bin
	$(SDK_TOOLS)/gen_appbin.exe $@ 0 0 0 0 0
	mv eagle.app.flash.bin $(FW_BASE)/eagle.flash.bin
	mv eagle.app.v6.irom0text.bin $(FW_BASE)/eagle.irom0text.bin
	rm eagle.app.v6.*

flash: all
# 8Mbit / 1MB
	$(ESPTOOL) --port $(ESPPORT) write_flash $(FLASHPARMS) \
               0x00000 $(FW_BASE)/eagle.flash.bin \
               0x10000 $(FW_BASE)/eagle.irom0text.bin
#	$(ESPTOOL) --port $(ESPPORT) --baud $(ESPBAUD) write_flash $(FLASHPARMS) \
#               0x00000 $(FW_BASE)/eagle.flash.bin \
#               0x10000 $(FW_BASE)/eagle.irom0text.bin

flash-init-data:
ifeq ($(FLASHSIZE), 4m)
	$(ESPTOOL) --port $(ESPPORT) --baud $(ESPBAUD) write_flash $(FLASHPARMS) \
   	         0x7C000 $(ESPINITDATA)
else
# 8Mbit / 1MB
	$(ESPTOOL) --port $(ESPPORT) write_flash $(FLASHPARMS) \
	           0xFC000 $(ESPINITDATA)
#	$(ESPTOOL) --port $(ESPPORT) --baud $(ESPBAUD) write_flash $(FLASHPARMS) \
#             0xFC000 $(ESPINITDATA)
endif

flash-default-params:
ifeq ($(FLASHSIZE), 4m)
	$(ESPTOOL) --port $(ESPPORT) --baud $(ESPBAUD) write_flash $(FLASHPARMS) \
             0x7E000 $(SDK_BINDIR)/blank.bin
else
# 8Mbit / 1MB
	$(ESPTOOL) --port $(ESPPORT) write_flash $(FLASHPARMS) \
	           0xFE000 $(SDK_BINDIR)/blank.bin
#	$(ESPTOOL) --port $(ESPPORT) --baud $(ESPBAUD) write_flash $(FLASHPARMS) \
#             0xFE000 $(SDK_BINDIR)/blank.bin
endif

clean:
	rm -f $(APP_AR)
	rm -f $(TARGET_OUT)
	rm -rf $(BUILD_DIR)
	rm -rf $(BUILD_BASE)
	rm -rf $(FW_BASE)

readFlashId:
	$(ESPTOOL) --port $(ESPPORT) flash_id
#	$(ESPTOOL) --port $(ESPPORT) --baud $(ESPBAUD) flash_id

readMac:
	$(ESPTOOL) --port $(ESPPORT) read_mac
#	$(ESPTOOL) --port $(ESPPORT) --baud $(ESPBAUD) read_mac

imageInfo:
	$(ESPTOOL) image_info $(FW_BASE)/eagle.flash.bin