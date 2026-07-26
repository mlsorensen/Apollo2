BINARY := lmtoken
GO     := go
BIN    := bin
DIST   := dist

# The fyne packaging tool, installed on demand into bin/ (no global install).
# Built for the HOST arch explicitly so `GOARCH=amd64 make package-gui`
# cross-builds only the app, not the tool itself.
FYNE := $(CURDIR)/$(BIN)/fyne-tool

# Strip debug info + symbol table (-s -w) and drop absolute build paths (-trimpath)
# for smaller, reproducible binaries. Quoted in the recipes so -s -w stay one flag.
LDFLAGS := -s -w

# GOOS/GOARCH pairs to cross-compile the CLI (linux arm/x86, macOS arm/intel,
# windows x86). The GUI needs CGO (OpenGL), so it is packaged per-OS instead —
# see package-gui / package-gui-cross.
PLATFORMS := linux/arm64 linux/amd64 darwin/arm64 darwin/amd64 windows/amd64

.DEFAULT_GOAL := build

.PHONY: build build-all package package-gui package-gui-cross format test vet clean

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
		codesign --force --deep --sign - "$(DIST)/LM Token.app"; \
		( cd $(DIST) && zip -qr $(BINARY)-gui-darwin-$$arch.zip "LM Token.app" ); \
		echo "packaged $(DIST)/$(BINARY)-gui-darwin-$$arch.zip"; \
	else \
		mv cmd/lmtoken-gui/LM\ Token* cmd/lmtoken-gui/$(BINARY)-gui* $(DIST)/ 2>/dev/null || true; \
		ls $(DIST); \
	fi

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
