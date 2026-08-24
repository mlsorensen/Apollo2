//go:build !windows

package main

// pauseOnExit is a no-op off Windows: Unix terminals don't close on program exit.
func pauseOnExit() {}
