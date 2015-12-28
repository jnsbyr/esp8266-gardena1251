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
TARGET		= sleeper-0.9.0.7

# source subdirectories
MODULES		= user driver

# libraries, mainly provided by the SDK
LIBS			= c gcc hal phy pp net80211 lwip wpa json main

# ESP8266 flash parameters (dependent on ESP8266 hardware)
FLASHPARMS	= --flash_freq 40m --flash_mode qio --flash_size 4m

# flash tool parameters (dependent on PC UART hardware)
ESPPORT		= COM4
ESPBAUD		= 74880

# Xtensa LX compiler path
XTENSA_TOOLS_ROOT	?= c:/Espressif/xtensa-lx106-elf/bin

# Espressif SDK paths
SDK_BASE		?= c:/Espressif/ESP8266_SDK
SDK_TOOLS	?= c:/Espressif/utils
ESPTOOL		?= $(SDK_TOOLS)/esptool.exe


#
# Normally nothing to configure south of here.
#

# Espressif SDK subdirectories
SDK_LIBDIR	= lib
SDK_LDDIR	= ld
SDK_INCDIR	= include include/json

# compiler flags
CFLAGS		= -Os -O2 \
               -std=gnu99 \
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
LDFLAGS		= -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static

# linker script 
LD_SCRIPT	= eagle.app.v6.ld

# select compiler, librarian and linker
CC			:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-gcc
AR			:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-ar
LD			:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-gcc
OBJCOPY		:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-objcopy
OBJDUMP		:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-objdump

# setup input and output paths as well as build objects
INCDIR		= $(MODULES) include
BUILD_BASE	= build
FW_BASE		= firmware
SDK_LIBDIR	:= $(addprefix $(SDK_BASE)/, $(SDK_LIBDIR))
SDK_INCDIR	:= $(addprefix -I$(SDK_BASE)/, $(SDK_INCDIR))
INCDIR		:= $(addprefix -I, $(INCDIR))
MODULE_INCDIR	:= $(addsuffix /include, $(INCDIR))
LD_SCRIPT	:= $(addprefix -T$(SDK_BASE)/$(SDK_LDDIR)/, $(LD_SCRIPT))
LIBS			:= $(addprefix -l, $(LIBS))
APP_AR		:= $(addprefix $(BUILD_BASE)/, $(TARGET)_app.a)
TARGET_OUT	:= $(addprefix $(BUILD_BASE)/, $(TARGET).out)
BUILD_DIR	= $(addprefix $(BUILD_BASE)/, $(MODULES)) $(FW_BASE)
SRC			= $(foreach moduleDir, $(MODULES), $(wildcard $(moduleDir)/*.c))
OBJS			= $(patsubst %.c, $(BUILD_BASE)/%.o, $(SRC))

all: checkdirs $(TARGET_OUT)
	@echo "ESP8266 firmware files for $(TARGET) built!"

# create project archive
$(APP_AR): $(OBJS)
	$(AR) -cru $(APP_AR) $(OBJS)

# compile C source files
$(BUILD_BASE)/%.o : %.c
	$(CC) $(INCDIR) $(MODULE_INCDIR) $(SDK_INCDIR) $(CFLAGS) -c $< -o $@

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
	$(SDK_TOOLS)/gen_appbin.exe $@ 0 0 0 0
	mv eagle.app.flash.bin $(FW_BASE)/eagle.flash.bin
	mv eagle.app.v6.irom0text.bin $(FW_BASE)/eagle.irom0text.bin
	rm eagle.app.v6.*

flash: all
	$(ESPTOOL) --port $(ESPPORT) --baud $(ESPBAUD) write_flash $(FLASHPARMS) \
               0x00000 $(FW_BASE)/eagle.flash.bin \
               0x40000 $(FW_BASE)/eagle.irom0text.bin

clean:
	rm -f $(APP_AR)
	rm -f $(TARGET_OUT)
	rm -rf $(BUILD_DIR)
	rm -rf $(BUILD_BASE)
	rm -rf $(FW_BASE)

flashId:
	$(ESPTOOL) --port $(ESPPORT) --baud $(ESPBAUD) flash_id

readMac:
	$(ESPTOOL) --port $(ESPPORT) --baud $(ESPBAUD) read_mac

imageInfo:
	$(ESPTOOL) image_info $(FW_BASE)/eagle.flash.bin      