# lmtoken

A single, dependency-free binary that logs into your La Marzocco cloud account
and prints a machine's **BLE auth token** (the `bleAuthToken` used for local
Bluetooth control). Written in Go using only the standard library — no
`pylamarzocco`, no Python runtime, no external modules.

## Build

```sh
go build -o lmtoken .
```

Cross-compile a binary for any platform (no C toolchain needed):

```sh
GOOS=darwin  GOARCH=arm64 go build -o lmtoken-macos-arm64 .   # Apple Silicon
GOOS=darwin  GOARCH=amd64 go build -o lmtoken-macos-amd64 .   # Intel Mac
GOOS=linux   GOARCH=amd64 go build -o lmtoken-linux-amd64 .
GOOS=windows GOARCH=amd64 go build -o lmtoken.exe .
```

Ship the resulting file as-is; it has no runtime dependencies.

## Usage

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
./lmtoken -serial MR002018      # pick a device by serial, no prompt
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
the reference Python implementation in `main_test.go` (`go test`).
