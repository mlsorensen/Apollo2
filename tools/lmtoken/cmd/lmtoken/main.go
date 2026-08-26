// Command lmtoken is the terminal frontend: prompts for credentials (or reads
// $LAMARZOCCO_USERNAME / $LAMARZOCCO_PASSWORD), fetches the account's devices
// via the lmtoken package, and prints the selected device's BLE token. The
// token is the only line on stdout; prompts and the device list go to stderr,
// so `TOKEN=$(lmtoken)` works.
package main

import (
	"bufio"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
	"time"

	"golang.org/x/term"

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

// promptHidden reads a line from the terminal with echo disabled. golang.org/x/term
// handles this natively on Unix and Windows (the old stty approach left the
// password visible on Windows).
func promptHidden(prompt string) (string, error) {
	fmt.Fprint(os.Stderr, prompt)
	b, err := term.ReadPassword(int(os.Stdin.Fd()))
	fmt.Fprintln(os.Stderr)
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(b)), nil
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
	flagDebug := flag.Bool("debug", false, "print raw cloud response bodies + BLE diagnostics")
	flagForce := flag.Bool("force", false, "re-provision a new token even if one already exists (re-mints)")
	flagScan := flag.Bool("scan", false, "list nearby Bluetooth devices and exit (diagnostic; no sign-in)")
	flag.Parse()

	if *flagDebug {
		lmtoken.Verbose = os.Stderr
	}

	var err error
	if *flagScan {
		err = scanDiag()
	} else {
		err = run(*flagUser, *flagSerial, *flagDebug, *flagForce)
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
	}
	pauseOnExit() // keep the window open if launched by double-click (Windows)
	if err != nil {
		os.Exit(1)
	}
}

// scanDiag lists every Bluetooth device seen for a fixed window — a no-sign-in
// diagnostic to confirm Bluetooth works and whether the Micra is visible.
func scanDiag() error {
	if !lmtoken.BLESupported {
		return fmt.Errorf("Bluetooth isn't supported on this build")
	}
	const dur = 15 * time.Second
	fmt.Fprintf(os.Stderr, "Scanning for Bluetooth devices for %s...\n\n", dur)
	var total, micra int
	err := lmtoken.ScanAll(dur, func(name, addr string, mode lmtoken.MachineMode) {
		total++
		label := name
		if label == "" {
			label = "(no name)"
		}
		tag := ""
		isMicra := mode == lmtoken.ModeConfig || mode == lmtoken.ModeOperative ||
			strings.HasPrefix(strings.ToUpper(name), "MICRA")
		if isMicra {
			micra++
			switch mode {
			case lmtoken.ModeConfig:
				tag = "  <-- MICRA, in PAIRING mode"
			case lmtoken.ModeOperative:
				tag = "  <-- MICRA, in normal mode (restart it into pairing mode)"
			default:
				tag = "  <-- MICRA (mode unknown — name/services split across packets)"
			}
		}
		fmt.Fprintf(os.Stderr, "  %-30s %s%s\n", label, addr, tag)
	})
	fmt.Fprintf(os.Stderr, "\nDone: %d device(s) seen, %d Micra.\n", total, micra)
	if total == 0 {
		fmt.Fprintln(os.Stderr, "No devices at all — Bluetooth may be off, not permitted for this app, or there's no adapter.")
	}
	return err
}

func run(flagUser, serial string, debug, force bool) error {
	username, password, err := readCredentials(flagUser)
	if err != nil {
		return err
	}

	var dbg io.Writer
	if debug {
		dbg = os.Stderr
	}
	fmt.Fprintln(os.Stderr, "signing in...")
	sess, err := lmtoken.NewSession(username, password, dbg)
	if err != nil {
		return err
	}
	things, err := sess.Things()
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

	// A stored token, and not forcing a re-mint: print it and we're done.
	if t.BleAuthToken != "" && !force {
		fmt.Println(t.BleAuthToken) // the one machine-readable line on stdout
		return nil
	}

	// No token (or -force): offer to provision one over Bluetooth.
	token, err := provision(sess, t, force)
	if err != nil {
		return err
	}
	fmt.Println(token)
	return nil
}

// provision walks the user through the Bluetooth handshake in plain language and
// returns the newly-stored token.
func provision(sess *lmtoken.Session, t lmtoken.Thing, force bool) (string, error) {
	if !lmtoken.BLESupported {
		return "", fmt.Errorf("this build can't set up a token over Bluetooth on this operating system")
	}
	if t.BleAuthToken == "" {
		fmt.Fprintf(os.Stderr, "\nNo Bluetooth token is saved for %s.\n", t.SerialNumber)
	} else {
		fmt.Fprintf(os.Stderr, "\n-force: this replaces the current token for %s (your app re-syncs; "+
			"any Apollo remote needs the new one).\n", t.SerialNumber)
	}
	if !confirm("Set one up now? It needs Bluetooth and your machine in pairing mode") {
		return "", fmt.Errorf("cancelled")
	}

	fmt.Fprintln(os.Stderr, `
Put your machine in pairing mode:
  1. Turn the machine OFF.
  2. Push the brew paddle to the LEFT and hold it.
  3. Turn the machine ON.
  4. Wait about 5 seconds.
  5. Push the paddle back to the RIGHT.
(If you wired an Apollo remote to the paddle, keep it powered ON and in Manual
 mode, but Disconnect it from the machine — Settings > Micra > Disconnect — so it
 doesn't hold the machine's single Bluetooth connection.)`)

	waitEnter("When the machine is in pairing mode, press Enter to continue")

	for {
		// Find the machine and capture its address so the handshake dials it
		// directly instead of scanning again.
		var addr string
		for {
			fmt.Fprintln(os.Stderr, "Looking for your machine...")
			mode, a, err := lmtoken.FindMachine(t.SerialNumber, 12*time.Second)
			if err != nil {
				return "", err
			}
			switch mode {
			case lmtoken.ModeConfig, lmtoken.ModeUnknown:
				// ModeUnknown: it's a Micra but the advertised mode was unclear
				// (some Bluetooth stacks split name/services across packets); the
				// connect below determines config-vs-operative authoritatively.
				addr = a
				fmt.Fprintln(os.Stderr, "Found it — connecting to set up the token...")
			case lmtoken.ModeOperative:
				fmt.Fprintln(os.Stderr, "Found your machine, but it's still in normal mode, not pairing mode.")
				waitEnter("Do the 5 paddle steps above, then press Enter to try again")
				continue
			default: // not found
				fmt.Fprintln(os.Stderr, "I can't find your machine over Bluetooth. Make sure it's on and nearby.")
				waitEnter("Press Enter to look again")
				continue
			}
			break
		}

		token, err := lmtoken.Provision(sess, t, addr, func(stage string) {
			fmt.Fprintln(os.Stderr, " ", stage)
		})
		if errors.Is(err, lmtoken.ErrMachineOperative) {
			// The connect proved it's in normal mode after all — retry from the top.
			fmt.Fprintln(os.Stderr, "\nThe machine is in normal mode, not pairing mode.")
			waitEnter("Do the paddle steps to enter pairing mode, then press Enter to try again")
			continue
		}
		if err != nil && token == "" {
			return "", err
		}
		if err != nil {
			// The machine derived and is using the token below; only the cloud
			// save failed. Give it to the user anyway — their remote works with it.
			fmt.Fprintf(os.Stderr, "\nWarning: %v\n", err)
			fmt.Fprintln(os.Stderr, "The token below is live on your machine — save it now. Your La Marzocco\n"+
				"app may need re-syncing, but your remote will connect with this token.")
		} else {
			// Read back so the user sees it really landed in the cloud.
			if things, e := sess.Things(); e == nil {
				for _, x := range things {
					if strings.EqualFold(x.SerialNumber, t.SerialNumber) && x.BleAuthToken == token {
						fmt.Fprintln(os.Stderr, "Saved to your account.")
					}
				}
			}
		}
		fmt.Fprintln(os.Stderr, "\nDone! Now RESTART your machine (power off, then on) to bring it out of\n"+
			"pairing mode — it can't connect until you do. Then paste the token below\n"+
			"into your remote's setup page.")
		return token, nil
	}
}

// confirm asks a yes/no question on stderr (default no).
func confirm(prompt string) bool {
	fmt.Fprintf(os.Stderr, "%s? [y/N]: ", prompt)
	line, _ := bufio.NewReader(os.Stdin).ReadString('\n')
	line = strings.ToLower(strings.TrimSpace(line))
	return line == "y" || line == "yes"
}

// waitEnter prints a prompt and blocks until the user presses Enter (Ctrl-C aborts).
func waitEnter(prompt string) {
	fmt.Fprintf(os.Stderr, "%s... ", prompt)
	bufio.NewReader(os.Stdin).ReadString('\n')
}
