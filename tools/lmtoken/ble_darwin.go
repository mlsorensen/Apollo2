//go:build darwin

package lmtoken

import (
	"github.com/go-ble/ble"
	"github.com/go-ble/ble/darwin"
)

func newBLEDevice() (ble.Device, error) { return darwin.NewDevice() }
