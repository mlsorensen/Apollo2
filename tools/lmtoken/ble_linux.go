//go:build linux

package lmtoken

import (
	"github.com/go-ble/ble"
	"github.com/go-ble/ble/linux"
)

func newBLEDevice() (ble.Device, error) { return linux.NewDevice() }
