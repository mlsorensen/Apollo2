//go:build windows

package lmtoken

// Windows BLE backend (tinygo/bluetooth over WinRT). go-ble has no Windows
// support; tinygo lacks characteristic Read on macOS but has it on Windows, so
// each OS uses whichever library actually works there. The orchestration in
// provision.go is shared. Compile-checked cross-platform; runtime-tested on
// Windows separately.

import (
	"errors"
	"fmt"
	"strings"
	"sync"
	"time"

	"tinygo.org/x/bluetooth"
)

// BLESupported reports whether this build can do the Bluetooth handshake.
const BLESupported = true

// Canonical (dashed) UUIDs — tinygo's ParseUUID wants that form.
const (
	seedCharDashed  = "0c0b7847-e12b-09a8-b04b-8e0922a9abab" // seed read/write (config mode)
	configSvcDashed = "d10a7847-e12b-09a8-b04b-8e0922a9abab" // CONFIGURATION mode service
	operSvcDashed   = "d30a7847-e12b-09a8-b04b-8e0922a9abab" // OPERATIVE mode service
)

var (
	winAdapter = bluetooth.DefaultAdapter
	winOnce    sync.Once
	winInitErr error
)

func bleReady() error {
	winOnce.Do(func() {
		if err := winAdapter.Enable(); err != nil {
			winInitErr = fmt.Errorf("%w (is it on?): %v", ErrBluetoothUnavailable, err)
		}
	})
	return winInitErr
}

func mustUUID(s string) bluetooth.UUID {
	u, err := bluetooth.ParseUUID(s)
	if err != nil {
		panic(err)
	}
	return u
}

// scanWindows runs the WinRT advertisement watcher until deadline, RESTARTING it
// whenever it self-stops early (a real WinRT quirk — it doesn't reliably run
// until StopScan). Each advertisement goes to cb; cb returns true to stop early.
func scanWindows(deadline time.Time, cb func(sr bluetooth.ScanResult) bool) error {
	var (
		mu      sync.Mutex
		stopped bool
	)
	for time.Now().Before(deadline) {
		timer := time.AfterFunc(time.Until(deadline), func() { _ = winAdapter.StopScan() })
		err := winAdapter.Scan(func(a *bluetooth.Adapter, sr bluetooth.ScanResult) {
			mu.Lock()
			defer mu.Unlock()
			if stopped {
				return
			}
			if cb(sr) {
				stopped = true
				_ = a.StopScan()
			}
		})
		timer.Stop()
		mu.Lock()
		done := stopped
		mu.Unlock()
		if done {
			return nil
		}
		if err != nil {
			return err
		}
		vlogf("[ble] WinRT scan window ended early; re-scanning\n")
		time.Sleep(150 * time.Millisecond)
	}
	return nil
}

// FindMachine scans for the Micra and reports its advertised mode + address.
func FindMachine(serial string, timeout time.Duration) (MachineMode, string, error) {
	if err := bleReady(); err != nil {
		return ModeUnknown, "", err
	}
	cfg := mustUUID(configSvcDashed)
	op := mustUUID(operSvcDashed)
	mode := ModeNotFound
	var addr string
	err := scanWindows(time.Now().Add(timeout), func(sr bluetooth.ScanResult) bool {
		svcMode := ModeUnknown
		if sr.HasServiceUUID(cfg) {
			svcMode = ModeConfig
		} else if sr.HasServiceUUID(op) {
			svcMode = ModeOperative
		}
		ok, m := micraMatch(sr.LocalName(), serial, svcMode)
		if !ok {
			return false
		}
		mode = m
		addr = sr.Address.String()
		return true
	})
	if err != nil {
		return ModeUnknown, "", err
	}
	return mode, addr, nil
}

// ScanAll reports every distinct device seen within the timeout (a diagnostic).
// It MERGES a device's advertisement (services) and scan-response (name), which
// WinRT delivers as separate events — otherwise the name is lost to dedup.
func ScanAll(timeout time.Duration, report func(name, addr string, mode MachineMode)) error {
	if err := bleReady(); err != nil {
		return err
	}
	cfg := mustUUID(configSvcDashed)
	op := mustUUID(operSvcDashed)
	type info struct {
		name string
		mode MachineMode
	}
	acc := map[string]*info{}
	var order []string
	err := scanWindows(time.Now().Add(timeout), func(sr bluetooth.ScanResult) bool {
		addr := sr.Address.String()
		e := acc[addr]
		if e == nil {
			e = &info{mode: ModeUnknown}
			acc[addr] = e
			order = append(order, addr)
		}
		if n := sr.LocalName(); n != "" {
			e.name = n
		}
		if sr.HasServiceUUID(cfg) {
			e.mode = ModeConfig
		} else if sr.HasServiceUUID(op) && e.mode != ModeConfig {
			e.mode = ModeOperative
		}
		return false // keep scanning; report everything at the end
	})
	for _, addr := range order {
		report(acc[addr].name, addr, acc[addr].mode)
	}
	return err
}

// ProvisionOverBLE connects to the machine at addr, writes the seed, and reads
// back the derived token. See the darwin/linux twin in ble.go for the contract.
func ProvisionOverBLE(serial, addr, seed string, timeout time.Duration, progress func(string)) (string, error) {
	report := func(s string) {
		if progress != nil {
			progress(s)
		}
	}
	if err := bleReady(); err != nil {
		return "", err
	}

	report("connecting to the machine over Bluetooth…")
	var a bluetooth.Address
	a.Set(addr)
	dev, err := winAdapter.Connect(a, bluetooth.ConnectionParams{})
	if err != nil {
		return "", fmt.Errorf("connecting to the machine over Bluetooth "+
			"(is another device — e.g. your Apollo remote or phone — connected to it?): %w", err)
	}
	defer dev.Disconnect()

	report("reading the machine's Bluetooth services…")
	svcs, err := dev.DiscoverServices(nil)
	if err != nil {
		return "", fmt.Errorf("reading the machine's Bluetooth services: %w", err)
	}
	var seedChar *bluetooth.DeviceCharacteristic
	sawOperative := false
	for _, s := range svcs {
		if strings.EqualFold(s.UUID().String(), operSvcDashed) {
			sawOperative = true
		}
		chars, e := s.DiscoverCharacteristics(nil)
		if e != nil {
			continue
		}
		for i := range chars {
			if strings.EqualFold(chars[i].UUID().String(), seedCharDashed) {
				c := chars[i]
				seedChar = &c
			}
		}
	}
	if seedChar == nil {
		if sawOperative {
			return "", ErrMachineOperative
		}
		return "", errors.New("the machine isn't exposing the pairing characteristic (is it in pairing mode?)")
	}

	report(fmt.Sprintf("writing the pairing seed (%d bytes)…", len(seed)))
	if _, err := seedChar.Write([]byte(seed)); err != nil {
		return "", fmt.Errorf("writing the pairing seed: %w", err)
	}

	report("reading the token back…")
	buf := make([]byte, 128)
	for i := 0; i < 12; i++ {
		time.Sleep(500 * time.Millisecond)
		n, err := seedChar.Read(buf)
		if err == nil && looksLikeToken(buf[:n]) {
			return string(buf[:n]), nil
		}
	}
	return "", errors.New("the machine did not return a token after the seed was written")
}
