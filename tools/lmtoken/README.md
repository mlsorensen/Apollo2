# lmtoken

Logs into your La Marzocco cloud account and fetches a machine's **BLE auth
token** (the `bleAuthToken` used for local Bluetooth control). Two frontends
share the same Go implementation — no `pylamarzocco`, no Python runtime:

- **LM Token** (`cmd/lmtoken-gui`) — a small [Fyne](https://fyne.io) desktop
  app: double-click, sign in, pick your machine, hit **Copy token**. No
  terminal needed.
- **lmtoken** (`cmd/lmtoken`) — the original dependency-free CLI, unchanged
  behavior, still ideal for scripting.

The cloud protocol lives in the root `lmtoken` package (standard library
only); the Fyne dependency is confined to the GUI binary.

## GUI app

```sh
make build          # builds bin/lmtoken and bin/lmtoken-gui for this machine
make package-gui    # clickable app for THIS OS (macOS: "LM Token.app" → dist/)
```

Fyne renders with OpenGL through CGO, so the GUI is built **per-OS** (each OS
packages its own; a C toolchain is required — Xcode CLT on macOS, gcc + X11/GL
dev headers on Linux, MinGW on Windows). To cross-package Linux/Windows apps
from one machine use [fyne-cross](https://github.com/fyne-io/fyne-cross)
(needs Docker): `make package-gui-cross`.

macOS signing: `package-gui` ad-hoc signs the bundle by default (without
*any* bundle signature Gatekeeper reports downloaded copies as "damaged" —
the Go linker only signs the binary). Releases go one step further:
`make release-mac TAG=lmtoken-vX.Y.Z` rebuilds both arches signed with a
Developer ID (`CODESIGN_ID`), notarizes them (`notarytool`, keychain
profile `lmtoken`), staples, and replaces the darwin zips + checksums on
the GitHub release — so downloads open with no Gatekeeper prompt at all.

Sign in with your La Marzocco account email + password; if the account has
several machines you'll get a picker. The token screen shows the token with a
**Copy token** button — paste it into the remote's setup page
(`Micra-Setup` Wi-Fi → http://192.168.4.1). The app remembers your email (not
your password) for next time.

## CLI

```sh
go build -o lmtoken ./cmd/lmtoken
```

Cross-compile for any platform (no C toolchain needed):

```sh
GOOS=darwin  GOARCH=arm64 go build -o lmtoken-macos-arm64 ./cmd/lmtoken
GOOS=linux   GOARCH=amd64 go build -o lmtoken-linux-amd64 ./cmd/lmtoken
GOOS=windows GOARCH=amd64 go build -o lmtoken.exe ./cmd/lmtoken
```

or `make build-all` / `make package` for the full release matrix. Ship the
resulting file as-is; it has no runtime dependencies.

### Usage

```sh
./lmtoken
```

Prompts for your account email and password (password input is hidden), then:

- if the account has **one** device, prints its BLE token;
- if it has **several**, lists them and asks you to pick one.

The token is the only thing written to **stdout** — prompts and the device list
go to **stderr** — so you can capture it directly:

```sh
TOKEN=$(./lmtoken)
```

### Non-interactive

```sh
export LAMARZOCCO_USERNAME='you@example.com'
export LAMARZOCCO_PASSWORD='…'
./lmtoken -serial MR000000      # pick a device by serial, no prompt
```

Flags / env:

| Input                   | Purpose                                            |
| ----------------------- | -------------------------------------------------- |
| `-u <email>`            | account email (else `$LAMARZOCCO_USERNAME`/prompt) |
| `$LAMARZOCCO_USERNAME`  | account email                                      |
| `$LAMARZOCCO_PASSWORD`  | account password (else hidden prompt)              |
| `-serial <serial>`      | select a device by serial, skipping the picker     |

## How it works

Each run mints a fresh installation key (P-256), registers it with the cloud
(`/auth/init`), signs in (`/auth/signin`), and lists devices (`/things`). All
requests carry La Marzocco's custom byte-rotation "proof" plus an ECDSA-signed
header. Because the installation key is fresh every run, it never collides with
a previously registered one (the cause of the intermittent HTTP 400s when an id
is reused).

The proof algorithm and key derivation are cross-checked byte-for-byte against
the reference Python implementation in `token_test.go` (`go test`).
