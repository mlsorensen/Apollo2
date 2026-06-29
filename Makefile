# Micra remote — convenience wrapper over PlatformIO + the sim.
#
#   make flash            build + flash the connected board (auto-detect; see below)
#   make flash BOARD=7b   flash a specific board (2inch | 7b)
#   make flash-2inch / make flash-7b
#   make build            compile the default (2-inch) firmware
#   make build-7b         compile the 7" firmware
#   make monitor          open the serial monitor
#   make sim              build + run the host simulator (writes renders/*.png)
#   make lmtoken          build the cloud token tool (tools/lmtoken)
#   make clean

PIO ?= pio
BOARD ?=

.DEFAULT_GOAL := build
.PHONY: flash flash-2inch flash-7b build build-7b monitor sim lmtoken clean

flash:
	@tools/flash.sh $(BOARD)

flash-2inch:
	@tools/flash.sh 2inch

flash-7b:
	@tools/flash.sh 7b

build:
	$(PIO) run -e esp32-s3-micra

build-7b:
	$(PIO) run -e esp32-s3-micra-7b

monitor:
	$(PIO) device monitor

sim:
	$(PIO) run -e sim && ./.pio/build/sim/program

lmtoken:
	$(MAKE) -C tools/lmtoken build

clean:
	$(PIO) run -t clean ; rm -rf .pio/build/sim
