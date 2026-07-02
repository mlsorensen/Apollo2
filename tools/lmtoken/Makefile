BINARY := lmtoken
GO     := go
BIN    := bin
DIST   := dist

# GOOS/GOARCH pairs to cross-compile (linux arm/x86, macOS arm/intel, windows x86)
PLATFORMS := linux/arm64 linux/amd64 darwin/arm64 darwin/amd64 windows/amd64

.DEFAULT_GOAL := build

.PHONY: build build-all package format test vet clean

build:
	@mkdir -p $(BIN)
	$(GO) build -o $(BIN)/$(BINARY) .

build-all:
	@mkdir -p $(BIN)
	@for p in $(PLATFORMS); do \
		os=$${p%/*}; arch=$${p#*/}; \
		out=$(BIN)/$(BINARY)-$$os-$$arch; \
		if [ "$$os" = "windows" ]; then out=$$out.exe; fi; \
		echo "building $$out"; \
		GOOS=$$os GOARCH=$$arch CGO_ENABLED=0 $(GO) build -o $$out . || exit 1; \
	done

# Zip each cross-compiled binary together with the README into dist/.
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

format:
	$(GO) fmt ./...

test:
	$(GO) test ./...

vet:
	$(GO) vet ./...

clean:
	rm -rf $(BIN) $(DIST)
