# SPDX-License-Identifier: GPL-3.0
# Copyright (c) 2023 Chriesibaum GmbH


# select the BOARD type here:

# BOARD := nucleo_u385rg_q   #  Nucleo U385RG, STM32U385RG, 2MB flash, 512kB RAM
# BOARD := rpi_pico    #  Raspberry Pi Pico, RP2040, 2MB flash, 264kB RAM
BOARD := rpi_pico2/rp2350a/m33
# BOARD := rpi_pico2/rp2350a/hazard3    #

.phony: all clean flash print release copy_uf2_PRI-PR2 copy_uf2_RP2350 flash.stm32cubeprogrammer

all: build

build:
	@echo get the build process working...
	west build -p auto -b $(BOARD) ./ \
		-DEXTRA_CONF_FILE="prj_log.conf"



clean:
	rm -rf ./build/

mr-proper: clean
	rm -rf ./$(BIN_DIR)/

flash:
	west flash -r jlink

BIN_DIR=bin

BOARD_NAME=$(subst /,_,$(BOARD))
FW_VERSION=$(shell sed -n 's/^#define[[:space:]]\+FW_VERSION_STRING[[:space:]]\+"\([^"]*\)"/\1/p' ./build/cb_info/fw_version.h)
BIN_FILE_NAME=cb_jtag_probe_$(FW_VERSION)_$(BOARD_NAME).uf2

bin: build
	@echo "Creating release package $(BIN_FILE_NAME)..."
	@mkdir -p $(BIN_DIR)
	@cp ./build/zephyr/zephyr.uf2 $(BIN_DIR)/$(BIN_FILE_NAME)



copy_uf2_PRI-PR2:
	# copy the generated .uf2 file to the mounted RPI Pico
	cp ./build/zephyr/zephyr.uf2 /media/$(USER)/RPI-RP2/

copy_uf2_RP2350:
	# copy the generated .uf2 file to the mounted RPI Pico
	cp ./build/zephyr/zephyr.uf2 /media/$(USER)/RP2350/

flash.stm32cubeprogrammer:
	west flash -r stm32cubeprogrammer
