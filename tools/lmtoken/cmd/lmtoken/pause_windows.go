//go:build windows

package main

import (
	"bufio"
	"fmt"
	"os"
	"syscall"
	"unsafe"
)

// pauseOnExit keeps the console open when the program was launched by double-
// clicking the .exe, so the printed token doesn't vanish. When run from an
// existing shell (cmd/PowerShell), the console has more than one process attached
// and this returns immediately, keeping stdout usable for `for /f`-style capture.
func pauseOnExit() {
	kernel32 := syscall.NewLazyDLL("kernel32.dll")
	getList := kernel32.NewProc("GetConsoleProcessList")
	var pids [4]uint32
	n, _, _ := getList.Call(uintptr(unsafe.Pointer(&pids[0])), uintptr(len(pids)))
	if n <= 1 { // only this process owns the console -> it was double-clicked
		fmt.Fprint(os.Stderr, "\nPress Enter to exit...")
		bufio.NewReader(os.Stdin).ReadString('\n')
	}
}
