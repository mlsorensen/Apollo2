//go:build !darwin && !linux && !windows

package lmtoken

import "time"

// BLESupported reports whether this build can do the Bluetooth handshake.
const BLESupported = false

func FindMachine(serial string, timeout time.Duration) (MachineMode, string, error) {
	return ModeUnknown, "", ErrBLEUnsupported
}

func ProvisionOverBLE(serial, addr, seed string, timeout time.Duration, progress func(string)) (string, error) {
	return "", ErrBLEUnsupported
}

func ScanAll(timeout time.Duration, report func(name, addr string, mode MachineMode)) error {
	return ErrBLEUnsupported
}
