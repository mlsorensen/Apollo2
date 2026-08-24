//go:build darwin || linux

package lmtoken

// BLE half of provisioning: read the machine's advertised mode, and drive the
// seed->token handshake in configuration mode. Uses go-ble/ble (CoreBluetooth on
// macOS, BlueZ on Linux). The machine derives the token from the cloud seed we
// write, so we must read it back — this is the one step that needs Bluetooth and
// physical proximity.

import (
	"context"
	"errors"
	"fmt"
	"strings"
	"sync"
	"time"

	"github.com/go-ble/ble"
)

// BLESupported reports whether this build can do the Bluetooth handshake.
const BLESupported = true

const (
	seedCharUUID  = "0c0b7847e12b09a8b04b8e0922a9abab" // seed read/write (config mode only)
	configSvcUUID = "d10a7847e12b09a8b04b8e0922a9abab" // CONFIGURATION mode service
	operSvcUUID   = "d30a7847e12b09a8b04b8e0922a9abab" // OPERATIVE mode service
)

var (
	bleMu     sync.Mutex
	bleInited bool
)

func bleReady() error {
	bleMu.Lock()
	defer bleMu.Unlock()
	if bleInited {
		return nil
	}
	// The first attempt often fails waiting for CoreBluetooth to reach the
	// "powered on" state: go-ble waits only ~1s, which the first-run Bluetooth
	// permission prompt easily exceeds (a bundled .app prompts; the CLI in an
	// already-authorized terminal doesn't). Retry — creating a fresh manager each
	// time — so the user has time to grant access and a clean manager can settle.
	var lastErr error
	for i := 0; i < 15; i++ {
		d, err := newBLEDevice()
		if err == nil {
			ble.SetDefaultDevice(d)
			bleInited = true
			return nil
		}
		lastErr = err
		vlogf("[ble] adapter not ready yet (%v); retrying\n", err)
		time.Sleep(time.Second)
	}
	return fmt.Errorf("%w (is it on and permitted?): %v", ErrBluetoothUnavailable, lastErr)
}

func modeFromServices(svcs []ble.UUID) MachineMode {
	for _, s := range svcs {
		switch strings.ToLower(s.String()) {
		case configSvcUUID:
			return ModeConfig
		case operSvcUUID:
			return ModeOperative
		}
	}
	return ModeUnknown
}

// FindMachine scans for the Micra (by serial, or any MICRA when serial is "")
// and reports the mode it's advertising plus its Bluetooth address, without
// connecting. ModeNotFound (addr "") if it isn't seen within the timeout. The
// address lets ProvisionOverBLE dial directly instead of scanning again (go-ble's
// Connect re-scans, which wedges CoreBluetooth right after this scan).
func FindMachine(serial string, timeout time.Duration) (MachineMode, string, error) {
	if err := bleReady(); err != nil {
		return ModeUnknown, "", err
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	mode := ModeNotFound
	var addr string
	err := ble.Scan(ctx, false, func(a ble.Advertisement) {
		ok, m := micraMatch(a.LocalName(), serial, modeFromServices(a.Services()))
		if !ok {
			return
		}
		mode = m
		addr = a.Addr().String()
		cancel() // found it; stop scanning
	}, nil)
	// A cancel/deadline is the normal way a scan ends here.
	if err != nil && !errors.Is(err, context.Canceled) && !errors.Is(err, context.DeadlineExceeded) {
		return ModeUnknown, "", err
	}
	return mode, addr, nil
}

// ScanAll reports every distinct device seen within the timeout (a diagnostic).
func ScanAll(timeout time.Duration, report func(name, addr string, mode MachineMode)) error {
	if err := bleReady(); err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	var mu sync.Mutex
	seen := map[string]bool{}
	err := ble.Scan(ctx, false, func(a ble.Advertisement) {
		mu.Lock()
		defer mu.Unlock()
		addr := a.Addr().String()
		if seen[addr] {
			return
		}
		seen[addr] = true
		report(a.LocalName(), addr, modeFromServices(a.Services()))
	}, nil)
	if err != nil && !errors.Is(err, context.Canceled) && !errors.Is(err, context.DeadlineExceeded) {
		return err
	}
	return nil
}

// ProvisionOverBLE connects to the Micra (which must be in configuration mode),
// writes the cloud pairSeed to the seed characteristic, and returns the token the
// machine derives from it. addr is the address from FindMachine — dialing it
// directly avoids a second scan (go-ble's Connect re-scans, which wedges the OS
// stack right after FindMachine); with addr "" it falls back to scan+connect.
// ErrMachineOperative if the machine is running (not in pairing mode) so the
// caller can tell the user to restart it. progress (may be nil) reports each step.
func ProvisionOverBLE(serial, addr, seed string, timeout time.Duration, progress func(string)) (string, error) {
	report := func(s string) {
		if progress != nil {
			progress(s)
		}
	}
	if err := bleReady(); err != nil {
		return "", err
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	report("connecting to the machine over Bluetooth…")
	var (
		client ble.Client
		err    error
	)
	if addr != "" {
		client, err = ble.Dial(ctx, ble.NewAddr(addr))
	} else {
		client, err = ble.Connect(ctx, func(a ble.Advertisement) bool {
			return isMicraName(a.LocalName(), serial)
		})
	}
	if err != nil {
		return "", fmt.Errorf("connecting to the machine over Bluetooth "+
			"(is another device — e.g. your Apollo remote or phone — connected to it?): %w", err)
	}
	defer client.CancelConnection()

	report("reading the machine's Bluetooth services…")
	p, err := client.DiscoverProfile(true)
	if err != nil {
		return "", fmt.Errorf("reading the machine's Bluetooth services: %w", err)
	}

	seedChar := p.FindCharacteristic(ble.NewCharacteristic(ble.MustParse(seedCharUUID)))
	if seedChar == nil {
		if p.FindService(ble.NewService(ble.MustParse(operSvcUUID))) != nil {
			return "", ErrMachineOperative
		}
		return "", errors.New("the machine isn't exposing the pairing characteristic (is it in pairing mode?)")
	}

	// Ask for a larger ATT MTU where the stack honors it (a no-op on macOS, where
	// CoreBluetooth still does a long write for the ~344-byte seed).
	_, _ = client.ExchangeMTU(517)

	report(fmt.Sprintf("writing the pairing seed (%d bytes)…", len(seed)))
	if err := client.WriteCharacteristic(seedChar, []byte(seed), false); err != nil {
		return "", fmt.Errorf("writing the pairing seed: %w", err)
	}

	// The machine derives the token from the seed and exposes it on the same
	// characteristic; give it a moment and read back (retry a few times).
	report("reading the token back…")
	for i := 0; i < 12; i++ {
		time.Sleep(500 * time.Millisecond)
		b, err := client.ReadCharacteristic(seedChar)
		if err == nil && looksLikeToken(b) {
			return string(b), nil
		}
	}
	return "", errors.New("the machine did not return a token after the seed was written")
}
