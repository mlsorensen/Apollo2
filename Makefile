# Micra remote — convenience wrapper over PlatformIO + the sim.
#
# Board targets are named <chip>-<panel>, mirroring the Waveshare product names
# (ESP32-S3-Touch-LCD-4.3B -> s3-4-3b, ESP32-P4-WIFI6-Touch-LCD-4.3 -> p4-4-3):
#
#   make flash              build + flash the connected board (auto-detect; see below)
#   make flash BOARD=s3-7b  flash a specific board (s3-2 | s3-7b | s3-4-3b | s3-4-3c | p4-4-3 | p4-5)
#   make flash-s3-2 / flash-s3-7b / flash-s3-4-3b / flash-s3-4-3c / flash-p4-4-3 / flash-p4-5
#   make build              compile the default (s3-2, 2" 320x240) firmware
#   make build-s3-7b        compile the S3 7" (1024x600) firmware
#   make build-s3-4-3b      compile the S3 4.3B (800x480) firmware
#   make build-s3-4-3c      compile the S3 4.3C (800x480, dimmable + battery) firmware
#   make build-p4-4-3       compile the P4-WIFI6 4.3" (DSI 800x480) firmware
#   make build-p4-5         compile the P4-WIFI6 5" (DSI 1280x720) firmware
#   make monitor            open the serial monitor
#   make sim                build + run the host simulator (writes renders/*.png)
#   make webapp             rebuild the embedded web app (needs node; every
#                           device build/flash target does this for you)
#   make lmtoken            build the cloud token tool (tools/lmtoken)
#   make lmtoken-release    cross-compile + zip lmtoken for all OSes (tools/lmtoken/dist/)
#   make lmtoken-publish VERSION=1.0.0   tag lmtoken-v1.0.0 + push it -> CI builds the release
#   make clean
#
# The pre-rename target names (flash-2inch, build-7b, build-p4, ...) still work
# as aliases.

PIO ?= pio
BOARD ?=

# --- Embedded web app --------------------------------------------------------
# The firmware serves the shot-history browser from a gzipped C array. That
# header is GENERATED, never committed: every device build (and flash) depends
# on it, so the page the device serves can't drift from tools/webapp/src.
# Building it needs node (see README "Developer documentation"); `make sim`
# does not — the simulator has no web server.
WEBAPP_HDR := include/platform_esp32/webapp_dist.h
WEBAPP_SRC := $(wildcard tools/webapp/src/*) \
              tools/webapp/index.html tools/webapp/vite.config.js \
              tools/webapp/package.json tools/webapp/package-lock.json \
              tools/webapp/build.sh tools/webapp/gen_header.py

$(WEBAPP_HDR): $(WEBAPP_SRC)
	@tools/webapp/build.sh

webapp: $(WEBAPP_HDR)

.DEFAULT_GOAL := build
.PHONY: flash flash-s3-2 flash-s3-7b flash-s3-4-3b flash-s3-4-3c flash-p4-4-3 \
        flash-p4-5 flash-2inch flash-7b flash-4-3b flash-4-3c flash-p4 \
        build build-s3-7b build-s3-4-3b build-s3-4-3c build-p4-4-3 build-p4-5 \
        build-7b build-4-3b build-4-3c build-p4 \
        webapp monitor sim lmtoken lmtoken-release lmtoken-publish clean

flash: $(WEBAPP_HDR)
	@tools/flash.sh $(BOARD)

flash-s3-2: $(WEBAPP_HDR)
	@tools/flash.sh s3-2

flash-s3-7b: $(WEBAPP_HDR)
	@tools/flash.sh s3-7b

flash-s3-4-3b: $(WEBAPP_HDR)
	@tools/flash.sh s3-4-3b

flash-s3-4-3c: $(WEBAPP_HDR)
	@tools/flash.sh s3-4-3c

flash-p4-4-3: $(WEBAPP_HDR)
	@tools/flash.sh p4-4-3

flash-p4-5: $(WEBAPP_HDR)
	@tools/flash.sh p4-5

build: $(WEBAPP_HDR)
	$(PIO) run -e esp32-s3-micra

build-s3-7b: $(WEBAPP_HDR)
	$(PIO) run -e esp32-s3-micra-7b

build-s3-4-3b: $(WEBAPP_HDR)
	$(PIO) run -e esp32-s3-micra-4-3b

build-s3-4-3c: $(WEBAPP_HDR)
	$(PIO) run -e esp32-s3-micra-4-3c

build-p4-4-3: $(WEBAPP_HDR)
	$(PIO) run -e esp32-p4-micra-43

build-p4-5: $(WEBAPP_HDR)
	$(PIO) run -e esp32-p4-micra-5

# --- Pre-rename aliases (muscle memory + older docs) -------------------------
flash-2inch: flash-s3-2
flash-7b: flash-s3-7b
flash-4-3b: flash-s3-4-3b
flash-4-3c: flash-s3-4-3c
flash-p4: flash-p4-4-3
build-7b: build-s3-7b
build-4-3b: build-s3-4-3b
build-4-3c: build-s3-4-3c
build-p4: build-p4-4-3

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
# what scopes it to this tool — firmware releases use the bare `v` prefix below.
lmtoken-publish:
	@test -n "$(VERSION)" || { echo "usage: make lmtoken-publish VERSION=1.0.0"; exit 1; }
	git tag lmtoken-v$(VERSION)
	git push origin lmtoken-v$(VERSION)

# Cut a FIRMWARE release: tag `v$(VERSION)` + push it -> the firmware-release
# workflow builds every board env and publishes the flashable images to a
# GitHub Release + the web flasher (GitHub Pages). Bump include/version.h's
# fw::kVersion to match in the same commit.
release-publish:
	@test -n "$(VERSION)" || { echo "usage: make release-publish VERSION=0.1.0"; exit 1; }
	git tag v$(VERSION)
	git push origin v$(VERSION)

clean:
	$(PIO) run -t clean ; rm -rf .pio/build/sim $(WEBAPP_HDR)
