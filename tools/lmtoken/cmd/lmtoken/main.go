// Command lmtoken is the terminal frontend: prompts for credentials (or reads
// $LAMARZOCCO_USERNAME / $LAMARZOCCO_PASSWORD), fetches the account's devices
// via the lmtoken package, and prints the selected device's BLE token. The
// token is the only line on stdout; prompts and the device list go to stderr,
// so `TOKEN=$(lmtoken)` works.
package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"strings"

	"lmtoken"
)

// ---------------------------------------------------------------- credentials

func readCredentials(flagUser string) (string, string, error) {
	username := strings.TrimSpace(flagUser)
	if username == "" {
		username = strings.TrimSpace(os.Getenv("LAMARZOCCO_USERNAME"))
	}
	if username == "" {
		fmt.Fprint(os.Stderr, "La Marzocco email: ")
		line, err := bufio.NewReader(os.Stdin).ReadString('\n')
		if err != nil && line == "" {
			return "", "", err
		}
		username = strings.TrimSpace(line)
	}
	password := os.Getenv("LAMARZOCCO_PASSWORD")
	if password == "" {
		p, err := promptHidden("La Marzocco password: ")
		if err != nil {
			return "", "", err
		}
		password = p
	}
	if username == "" || password == "" {
		return "", "", fmt.Errorf("username and password are required")
	}
	return username, password, nil
}

// promptHidden reads a line from the terminal with echo disabled (via stty on
// Unix). Falls back to visible input if stty is unavailable (e.g. Windows).
func promptHidden(prompt string) (string, error) {
	fmt.Fprint(os.Stderr, prompt)
	restore := disableEcho()
	line, err := bufio.NewReader(os.Stdin).ReadString('\n')
	if restore != nil {
		restore()
		fmt.Fprintln(os.Stderr)
	}
	if err != nil && line == "" {
		return "", err
	}
	return strings.TrimSpace(line), nil
}

func disableEcho() func() {
	stty, err := exec.LookPath("stty")
	if err != nil {
		return nil
	}
	run := func(arg string) error {
		c := exec.Command(stty, arg)
		c.Stdin = os.Stdin
		return c.Run()
	}
	if run("-echo") != nil {
		return nil
	}
	return func() { _ = run("echo") }
}

// ---------------------------------------------------------------- device pick

func chooseThing(things []lmtoken.Thing) (lmtoken.Thing, error) {
	if len(things) == 0 {
		return lmtoken.Thing{}, fmt.Errorf("no devices found on this account")
	}
	if len(things) == 1 {
		return things[0], nil
	}
	fmt.Fprintln(os.Stderr, "\nMultiple devices on this account:")
	for i, t := range things {
		fmt.Fprintf(os.Stderr, "  [%d] %s  serial=%s  model=%s\n", i+1, t.Name, t.SerialNumber, t.ModelName)
	}
	for {
		fmt.Fprintf(os.Stderr, "Choose a device [1-%d]: ", len(things))
		line, err := bufio.NewReader(os.Stdin).ReadString('\n')
		if err != nil && line == "" {
			return lmtoken.Thing{}, err
		}
		n, err := strconv.Atoi(strings.TrimSpace(line))
		if err == nil && n >= 1 && n <= len(things) {
			return things[n-1], nil
		}
		fmt.Fprintln(os.Stderr, "  invalid selection")
	}
}

// ---------------------------------------------------------------- main

func main() {
	flagUser := flag.String("u", "", "La Marzocco account email (else $LAMARZOCCO_USERNAME or prompt)")
	flagSerial := flag.String("serial", "", "select device by serial number (skip the interactive picker)")
	flag.Parse()

	if err := run(*flagUser, *flagSerial); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func run(flagUser, serial string) error {
	username, password, err := readCredentials(flagUser)
	if err != nil {
		return err
	}

	things, err := lmtoken.FetchThings(username, password, func(stage string) {
		fmt.Fprintln(os.Stderr, stage)
	})
	if err != nil {
		return err
	}

	var t lmtoken.Thing
	if serial != "" {
		found := false
		for _, x := range things {
			if strings.EqualFold(x.SerialNumber, serial) {
				t, found = x, true
				break
			}
		}
		if !found {
			return fmt.Errorf("no device with serial %q on this account", serial)
		}
	} else {
		t, err = chooseThing(things)
		if err != nil {
			return err
		}
	}

	fmt.Fprintf(os.Stderr, "device: %s (serial=%s, model=%s)\n", t.Name, t.SerialNumber, t.ModelName)
	if t.BleAuthToken == "" {
		return fmt.Errorf("cloud returned an empty bleAuthToken for %s", t.SerialNumber)
	}
	fmt.Println(t.BleAuthToken) // the one machine-readable line on stdout
	return nil
}
