package lmtoken

// End-to-end token provisioning: mint a cloud seed, derive the token over BLE,
// and store it in the cloud (unpair + re-pair when the machine is already bound).
// The BLE half lives in ble.go / ble_stub.go (per-OS); everything here is
// platform-independent so it compiles everywhere.

import (
	"errors"
	"fmt"
	"io"
	"strings"
	"time"
)

// Verbose, when non-nil, receives low-level BLE diagnostics (scan counts, the
// devices seen, re-scan cycles). The CLI wires this to stderr under -debug.
var Verbose io.Writer

func vlogf(format string, args ...any) {
	if Verbose != nil {
		fmt.Fprintf(Verbose, format, args...)
	}
}

// MachineMode is what a Micra is advertising over Bluetooth.
type MachineMode int

const (
	ModeUnknown   MachineMode = iota
	ModeNotFound              // not seen in the scan window
	ModeOperative             // running (service d30a7847) — must be restarted into pairing mode
	ModeConfig                // pairing/configuration mode (service d10a7847) — provisionable
)

func (m MachineMode) String() string {
	switch m {
	case ModeNotFound:
		return "not found"
	case ModeOperative:
		return "operative"
	case ModeConfig:
		return "pairing"
	default:
		return "unknown"
	}
}

var (
	// ErrMachineOperative means the machine is running rather than in pairing mode.
	ErrMachineOperative = errors.New("the machine is running, not in pairing mode — restart it into pairing mode")
	// ErrBLEUnsupported means this OS build can't do the Bluetooth handshake.
	ErrBLEUnsupported = errors.New("Bluetooth provisioning isn't supported on this build")
	// ErrBluetoothUnavailable means the Bluetooth adapter couldn't be brought up.
	// On macOS this happens on the first run after granting permission — the
	// process cached the pre-grant authorization and must be restarted. Frontends
	// can special-case this (e.g. offer a "Quit & Reopen").
	ErrBluetoothUnavailable = errors.New("Bluetooth is unavailable")
)

func confirmFor(t Thing, token string) ConfirmRequest {
	return ConfirmRequest{
		SerialNumber: t.SerialNumber,
		Name:         t.Name,
		PairToken:    token,
		BleAuthToken: token,
		OfflineMode:  false,
		RemoveOthers: false,
	}
}

func isAlreadyPaired(err error) bool {
	return err != nil && strings.Contains(err.Error(), "THING_ALREADY_PAIRED")
}

// isMicraName matches a Micra's advertised name (prefix "MICRA"), optionally
// requiring the serial. Shared by every platform's BLE backend.
func isMicraName(name, serial string) bool {
	up := strings.ToUpper(name)
	if !strings.HasPrefix(up, "MICRA") {
		return false
	}
	return serial == "" || strings.Contains(up, strings.ToUpper(serial))
}

// micraMatch decides whether an advertised device is the Micra we want, from its
// advertised name and the mode implied by its advertised service UUIDs. Some BLE
// stacks (notably Windows/WinRT) put the name in a separate scan-response packet,
// so the primary advertisement has no name — hence a service-UUID match alone
// qualifies. The serial is only enforced when a name is actually present.
func micraMatch(name, serial string, svcMode MachineMode) (bool, MachineMode) {
	byService := svcMode == ModeConfig || svcMode == ModeOperative
	if !byService && !isMicraName(name, "") {
		return false, ModeNotFound
	}
	if serial != "" && name != "" && !isMicraName(name, serial) {
		return false, ModeNotFound
	}
	if byService {
		return true, svcMode
	}
	return true, ModeUnknown
}

// looksLikeToken reports whether b is a 64-hex-character token (32 bytes).
func looksLikeToken(b []byte) bool {
	if len(b) != 64 {
		return false
	}
	for _, c := range b {
		hex := (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')
		if !hex {
			return false
		}
	}
	return true
}

// Provision runs the full flow for one machine (assumed already in pairing mode —
// callers should confirm with FindMachine first, and pass the address it returns
// as addr so the BLE step dials directly): mint a seed, derive the token over BLE,
// and store it in the cloud. If the machine is already bound to the account
// (confirm returns THING_ALREADY_PAIRED), it unpairs and re-pairs, which re-mints
// once more. Returns the token now held by both the machine and cloud. progress
// (may be nil) reports each step for the UI.
func Provision(sess *Session, t Thing, addr string, progress func(string)) (string, error) {
	report := func(s string) {
		if progress != nil {
			progress(s)
		}
	}
	serial := t.SerialNumber

	derive := func() (string, error) {
		report("requesting a pairing seed…")
		seed, err := sess.StartPairing(serial)
		if err != nil {
			return "", err
		}
		return ProvisionOverBLE(serial, addr, seed, 45*time.Second, progress)
	}

	token, err := derive()
	if err != nil {
		return "", err
	}

	report("saving the token to your account…")
	_, err = sess.ConfirmPairing(confirmFor(t, token))
	if isAlreadyPaired(err) {
		// Re-tokening a machine already on the account: unpair, then a fresh
		// seed→derive→confirm. Safe — this is the normal pairing sequence, and
		// the app keeps working (it re-reads the new token from the cloud).
		report("re-pairing the machine…")
		if e := sess.Unpair(serial); e != nil {
			return "", e
		}
		token, err = derive()
		if err != nil {
			return "", err
		}
		report("saving the token to your account…")
		_, err = sess.ConfirmPairing(confirmFor(t, token))
	}
	if err != nil {
		return "", err
	}
	return token, nil
}
