BINARY := lmtoken
GO     := go
BIN    := bin
DIST   := dist

# The fyne packaging tool, installed on demand into bin/ (no global install).
# Built for the HOST arch explicitly so `GOARCH=amd64 make package-gui`
# cross-builds only the app, not the tool itself.
FYNE := $(CURDIR)/$(BIN)/fyne-tool

# macOS code signing: "-" = ad-hoc (default, what CI uses). Set CODESIGN_ID
# to a Developer ID (substring match, e.g. "Developer ID Application") for a
# real signature — that also turns on the hardened runtime + secure timestamp
# notarization requires.
CODESIGN_ID ?= -
ifneq ($(CODESIGN_ID),-)
CODESIGN_FLAGS := --options runtime --timestamp
endif
# Keychain profile created once via: xcrun notarytool store-credentials lmtoken ...
NOTARY_PROFILE ?= lmtoken

# Strip debug info + symbol table (-s -w) and drop absolute build paths (-trimpath)
# for smaller, reproducible binaries. Quoted in the recipes so -s -w stay one flag.
LDFLAGS := -s -w

# GOOS/GOARCH pairs to cross-compile the CLI (linux arm/x86, macOS arm/intel,
# windows x86). The GUI needs CGO (OpenGL), so it is packaged per-OS instead —
# see package-gui / package-gui-cross.
PLATFORMS := linux/arm64 linux/amd64 darwin/arm64 darwin/amd64 windows/amd64

.DEFAULT_GOAL := build

.PHONY: build build-all package package-gui package-gui-cross release-mac format test vet clean

# Host-native CLI + GUI binaries.
build:
	@mkdir -p $(BIN)
	$(GO) build -trimpath -ldflags="$(LDFLAGS)" -o $(BIN)/$(BINARY) ./cmd/lmtoken
	$(GO) build -trimpath -ldflags="$(LDFLAGS)" -o $(BIN)/$(BINARY)-gui ./cmd/lmtoken-gui

build-all:
	@mkdir -p $(BIN)
	@for p in $(PLATFORMS); do \
		os=$${p%/*}; arch=$${p#*/}; \
		out=$(BIN)/$(BINARY)-$$os-$$arch; \
		if [ "$$os" = "windows" ]; then out=$$out.exe; fi; \
		echo "building $$out"; \
		GOOS=$$os GOARCH=$$arch CGO_ENABLED=0 $(GO) build -trimpath -ldflags="$(LDFLAGS)" -o $$out ./cmd/lmtoken || exit 1; \
	done

# Zip each cross-compiled CLI binary together with the README into dist/.
# The binary is named plainly (lmtoken / lmtoken.exe) inside each archive.
package: build-all
	@mkdir -p $(DIST)
	@for p in $(PLATFORMS); do \
		os=$${p%/*}; arch=$${p#*/}; \
		name=$(BINARY); src=$(BIN)/$(BINARY)-$$os-$$arch; \
		if [ "$$os" = "windows" ]; then name=$(BINARY).exe; src=$$src.exe; fi; \
		zip=$(CURDIR)/$(DIST)/$(BINARY)-$$os-$$arch.zip; \
		stage=$$(mktemp -d); \
		cp $$src $$stage/$$name; cp README.md $$stage/; \
		rm -f $$zip; \
		( cd $$stage && zip -q $$zip $$name README.md ); \
		rm -rf $$stage; \
		echo "packaged $(DIST)/$(BINARY)-$$os-$$arch.zip"; \
	done

# Package the GUI as a clickable app FOR THE HOST OS (Fyne needs CGO, so each
# OS builds its own). On macOS this yields "LM Token.app", zipped into dist/
# with the host arch in the name; on Linux a .tar.xz; on Windows an .exe.
$(FYNE):
	@mkdir -p $(BIN)
	GOARCH=$$($(GO) env GOHOSTARCH) GOBIN=$(CURDIR)/$(BIN) $(GO) install fyne.io/tools/cmd/fyne@latest
	mv $(CURDIR)/$(BIN)/fyne $(FYNE)

package-gui: $(FYNE)
	@mkdir -p $(DIST)
	cd cmd/lmtoken-gui && $(FYNE) package --release
	@if [ -d "cmd/lmtoken-gui/LM Token.app" ]; then \
		arch=$$($(GO) env GOARCH); \
		rm -rf "$(DIST)/LM Token.app" $(DIST)/$(BINARY)-gui-darwin-$$arch.zip; \
		mv "cmd/lmtoken-gui/LM Token.app" $(DIST)/; \
		codesign --force --deep --sign "$(CODESIGN_ID)" $(CODESIGN_FLAGS) "$(DIST)/LM Token.app"; \
		( cd $(DIST) && zip -qr $(BINARY)-gui-darwin-$$arch.zip "LM Token.app" ); \
		echo "packaged $(DIST)/$(BINARY)-gui-darwin-$$arch.zip"; \
	else \
		mv cmd/lmtoken-gui/LM\ Token* cmd/lmtoken-gui/$(BINARY)-gui* $(DIST)/ 2>/dev/null || true; \
		ls $(DIST); \
	fi

# Signed + notarized macOS GUI zips, replacing the ad-hoc CI ones on an
# existing GitHub release. Run on a Mac with a Developer ID cert in the
# keychain and notary credentials stored once via
#   xcrun notarytool store-credentials lmtoken --apple-id you@example.com --team-id TEAMID
# Usage: make release-mac TAG=lmtoken-vX.Y.Z
release-mac:
	@test -n "$(TAG)" || { echo "usage: make release-mac TAG=lmtoken-vX.Y.Z"; exit 1; }
	@for arch in arm64 amd64; do \
		GOARCH=$$arch CGO_ENABLED=1 CODESIGN_ID="Developer ID Application" $(MAKE) package-gui || exit 1; \
		echo "notarizing $$arch (takes a minute or two)..."; \
		xcrun notarytool submit $(DIST)/$(BINARY)-gui-darwin-$$arch.zip \
			--keychain-profile $(NOTARY_PROFILE) --wait || exit 1; \
		xcrun stapler staple "$(DIST)/LM Token.app" || exit 1; \
		( cd $(DIST) && rm $(BINARY)-gui-darwin-$$arch.zip && \
			zip -qr $(BINARY)-gui-darwin-$$arch.zip "LM Token.app" ) || exit 1; \
	done
	gh release upload $(TAG) $(DIST)/$(BINARY)-gui-darwin-arm64.zip $(DIST)/$(BINARY)-gui-darwin-amd64.zip --clobber
	@tmp=$$(mktemp -d); \
	gh release download $(TAG) -p SHA256SUMS.txt -D $$tmp && \
	grep -v '$(BINARY)-gui-darwin' $$tmp/SHA256SUMS.txt > $$tmp/keep && \
	( cd $(DIST) && shasum -a 256 $(BINARY)-gui-darwin-*.zip ) >> $$tmp/keep && \
	mv $$tmp/keep $$tmp/SHA256SUMS.txt && \
	gh release upload $(TAG) $$tmp/SHA256SUMS.txt --clobber; \
	rm -rf $$tmp
	@echo "release $(TAG): darwin GUI zips replaced with signed + notarized builds"

# Cross-platform GUI packages (linux/windows from any host) via fyne-cross,
# which drives per-OS docker toolchains: https://github.com/fyne-io/fyne-cross
#   go install github.com/fyne-io/fyne-cross@latest   (docker required)
package-gui-cross:
	fyne-cross linux -arch amd64,arm64 -app-id com.turboio.lmtoken ./cmd/lmtoken-gui
	fyne-cross windows -arch amd64 -app-id com.turboio.lmtoken ./cmd/lmtoken-gui

format:
	$(GO) fmt ./...

test:
	$(GO) test ./...

vet:
	$(GO) vet ./...

clean:
	rm -rf $(BIN) $(DIST)
