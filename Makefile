# Micra remote — convenience wrapper over PlatformIO + the sim.
#
#   make flash            build + flash the connected board (auto-detect; see below)
#   make flash BOARD=7b   flash a specific board (2inch | 7b | 4-3b | p4)
#   make flash-2inch / make flash-7b / make flash-4-3b / make flash-p4
#   make build            compile the default (2-inch) firmware
#   make build-7b         compile the 7" (1024x600) firmware
#   make build-4-3b       compile the 4.3B (800x480) firmware
#   make build-p4         compile the ESP32-P4-WIFI6 4.3" (DSI 800x480) firmware
#   make monitor          open the serial monitor
#   make sim              build + run the host simulator (writes renders/*.png)
#   make lmtoken          build the cloud token tool (tools/lmtoken)
#   make lmtoken-release  cross-compile + zip lmtoken for all OSes (tools/lmtoken/dist/)
#   make lmtoken-publish VERSION=1.0.0   tag lmtoken-v1.0.0 + push it -> CI builds the release
#   make clean

PIO ?= pio
BOARD ?=

.DEFAULT_GOAL := build
.PHONY: flash flash-2inch flash-7b flash-4-3b flash-p4 build build-7b build-4-3b build-p4 monitor sim lmtoken lmtoken-release lmtoken-publish clean

flash:
	@tools/flash.sh $(BOARD)

flash-2inch:
	@tools/flash.sh 2inch

flash-7b:
	@tools/flash.sh 7b

flash-4-3b:
	@tools/flash.sh 4-3b

flash-p4:
	@tools/flash.sh p4

build:
	$(PIO) run -e esp32-s3-micra

build-7b:
	$(PIO) run -e esp32-s3-micra-7b

build-4-3b:
	$(PIO) run -e esp32-s3-micra-4-3b

build-p4:
	$(PIO) run -e esp32-p4-micra-43

monitor:
	$(PIO) device monitor

sim:
	$(PIO) run -e sim && ./.pio/build/sim/program

lmtoken:
	$(MAKE) -C tools/lmtoken build

lmtoken-release:
	$(MAKE) -C tools/lmtoken package  # writes tools/lmtoken/dist/*.zip

# Cut an lmtoken release: tag the current commit `lmtoken-v$(VERSION)` and push
# just that tag, which triggers the lmtoken-release workflow (it builds + publishes
# the binaries). The tag points at the whole repo commit; the `lmtoken-` prefix is
# what scopes it to this tool — firmware releases will use a different prefix.
lmtoken-publish:
	@test -n "$(VERSION)" || { echo "usage: make lmtoken-publish VERSION=1.0.0"; exit 1; }
	git tag lmtoken-v$(VERSION)
	git push origin lmtoken-v$(VERSION)

clean:
	$(PIO) run -t clean ; rm -rf .pio/build/sim
