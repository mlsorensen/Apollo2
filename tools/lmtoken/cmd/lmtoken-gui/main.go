// Command lmtoken-gui is the point-and-click frontend: a small Fyne window with
// a login form, a device picker (when the account has several machines), and the
// resulting BLE token with a copy-to-clipboard button. When the cloud has no
// token for a machine (or the user forces a new one), it can provision one over
// Bluetooth. Same cloud + BLE flow as the CLI — everything lives in the lmtoken
// package.
package main

import (
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/widget"

	"lmtoken"
)

const prefEmailKey = "lastEmail"

func main() {
	a := app.NewWithID("com.turboio.lmtoken")
	// All goroutine UI updates below go through fyne.Do; declare that so Fyne
	// skips its legacy thread-check warning. Merged (not replaced) so metadata
	// injected by `fyne package` survives.
	if m := a.Metadata(); !m.Migrations["fyneDo"] {
		if m.Migrations == nil {
			m.Migrations = map[string]bool{}
		}
		m.Migrations["fyneDo"] = true
		app.SetMetadata(m)
	}
	w := a.NewWindow("La Marzocco BLE Token")
	w.Resize(fyne.NewSize(460, 420))
	showLogin(a, w)
	w.ShowAndRun()
}

// debugLog is a thread-safe io.Writer that accumulates raw response bodies for
// the "debug" checkbox to display.
type debugLog struct {
	mu sync.Mutex
	b  strings.Builder
}

func (d *debugLog) Write(p []byte) (int, error) {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.b.Write(p)
}

func (d *debugLog) String() string {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.b.String()
}

// showLogin renders the credentials form. On success it advances to the device
// picker (or straight to the token when the account has one machine).
func showLogin(a fyne.App, w fyne.Window) {
	email := widget.NewEntry()
	email.SetPlaceHolder("you@example.com")
	email.SetText(a.Preferences().String(prefEmailKey))

	password := widget.NewPasswordEntry()
	password.SetPlaceHolder("password")

	debug := widget.NewCheck("Show raw server responses (debug)", nil)

	status := widget.NewLabel("")
	status.Wrapping = fyne.TextWrapWord

	var signIn *widget.Button
	submit := func() {
		if email.Text == "" || password.Text == "" {
			status.Importance = widget.DangerImportance
			status.SetText("Email and password are required.")
			return
		}
		signIn.Disable()
		email.Disable()
		password.Disable()
		status.Importance = widget.MediumImportance
		status.SetText("connecting…")
		a.Preferences().SetString(prefEmailKey, email.Text)

		dl := &debugLog{}
		var dbg io.Writer
		if debug.Checked {
			dbg = dl
		}
		go func() {
			sess, err := lmtoken.NewSession(email.Text, password.Text, dbg)
			var things []lmtoken.Thing
			if err == nil {
				things, err = sess.Things()
			}
			fyne.Do(func() {
				if err == nil && len(things) == 0 {
					err = errors.New("no devices found on this account")
				}
				if err != nil {
					signIn.Enable()
					email.Enable()
					password.Enable()
					status.Importance = widget.DangerImportance
					status.SetText(err.Error())
					return
				}
				if len(things) == 1 {
					showToken(a, w, sess, dl, things[0], "")
				} else {
					showPicker(a, w, sess, dl, things)
				}
			})
		}()
	}
	signIn = widget.NewButton("Sign in", submit)
	signIn.Importance = widget.HighImportance
	password.OnSubmitted = func(string) { submit() }
	email.OnSubmitted = func(string) { w.Canvas().Focus(password) }

	heading := widget.NewLabelWithStyle("Sign in with your La Marzocco account",
		fyne.TextAlignCenter, fyne.TextStyle{Bold: true})
	sub := widget.NewLabel("Fetches your machine's Bluetooth token from the La Marzocco cloud. " +
		"If none is stored, it can set one up over Bluetooth. Credentials are sent only to lamarzocco.io.")
	sub.Wrapping = fyne.TextWrapWord

	form := widget.NewForm(
		widget.NewFormItem("Email", email),
		widget.NewFormItem("Password", password),
	)
	w.SetContent(container.NewVBox(heading, sub, form, debug, signIn, status))
	w.Canvas().Focus(email)
}

// showPicker lists the account's machines; tapping one shows its token.
func showPicker(a fyne.App, w fyne.Window, sess *lmtoken.Session, dl *debugLog, things []lmtoken.Thing) {
	heading := widget.NewLabelWithStyle("Choose a machine",
		fyne.TextAlignCenter, fyne.TextStyle{Bold: true})

	rows := make([]fyne.CanvasObject, 0, len(things))
	for _, t := range things {
		t := t
		b := widget.NewButton(fmt.Sprintf("%s — %s (%s)", t.Name, t.ModelName, t.SerialNumber),
			func() { showToken(a, w, sess, dl, t, "") })
		rows = append(rows, b)
	}
	back := widget.NewButton("Back", func() { showLogin(a, w) })
	w.SetContent(container.NewBorder(heading, back, nil, nil,
		container.NewVScroll(container.NewVBox(rows...))))
}

// doneNote is the reminder shown after a fresh provision.
const doneNote = "Done! Restart your machine (power off, then on) to bring it out of pairing mode, " +
	"then paste this token into your remote's setup page."

// showToken shows the device's token (with a copy button) or, when there's no
// token, an offer to provision one. A non-empty note (e.g. doneNote) is shown
// above the token — used right after provisioning for the restart reminder.
func showToken(a fyne.App, w fyne.Window, sess *lmtoken.Session, dl *debugLog, t lmtoken.Thing, note string) {
	heading := widget.NewLabelWithStyle(fmt.Sprintf("%s — %s", t.Name, t.ModelName),
		fyne.TextAlignCenter, fyne.TextStyle{Bold: true})
	serial := widget.NewLabelWithStyle("serial "+t.SerialNumber, fyne.TextAlignCenter, fyne.TextStyle{})
	content := []fyne.CanvasObject{heading, serial}

	if t.BleAuthToken == "" {
		msg := widget.NewLabel("No Bluetooth token is saved for this machine. Set one up over " +
			"Bluetooth (you'll put the machine in pairing mode).")
		msg.Wrapping = fyne.TextWrapWord
		setup := widget.NewButton("Set up token over Bluetooth", func() {
			showProvision(a, w, sess, dl, t)
		})
		setup.Importance = widget.HighImportance
		content = append(content, msg, setup)
	} else {
		if note != "" {
			done := widget.NewLabel(note)
			done.Wrapping = fyne.TextWrapWord
			content = append(content, done)
		}
		token := widget.NewEntry()
		token.SetText(t.BleAuthToken)
		token.MultiLine = true
		token.Wrapping = fyne.TextWrapBreak
		token.TextStyle = fyne.TextStyle{Monospace: true}
		token.OnChanged = func(s string) {
			if s != t.BleAuthToken {
				token.SetText(t.BleAuthToken)
			}
		}
		var copyBtn *widget.Button
		copyBtn = widget.NewButton("Copy token", func() {
			a.Clipboard().SetContent(t.BleAuthToken)
			copyBtn.SetText("Copied ✓")
		})
		copyBtn.Importance = widget.HighImportance
		hint := widget.NewLabel("Paste this into the remote's setup page (join its Micra-Setup Wi-Fi and open http://192.168.4.1).")
		hint.Wrapping = fyne.TextWrapWord
		force := widget.NewButton("Force new token…", func() { showProvision(a, w, sess, dl, t) })
		content = append(content, token, copyBtn, hint, force)
	}

	if dl != nil && dl.String() != "" {
		content = append(content, debugView(dl))
	}
	back := widget.NewButton("Start over", func() { showLogin(a, w) })
	content = append(content, back)
	w.SetContent(container.NewVScroll(container.NewVBox(content...)))
}

// showProvision walks the Bluetooth handshake: instructions, a Start button that
// finds the machine in pairing mode and runs the provision, and a status line.
func showProvision(a fyne.App, w fyne.Window, sess *lmtoken.Session, dl *debugLog, t lmtoken.Thing) {
	heading := widget.NewLabelWithStyle("Set up token", fyne.TextAlignCenter, fyne.TextStyle{Bold: true})

	if !lmtoken.BLESupported {
		msg := widget.NewLabel("This build can't set up a token over Bluetooth on this operating system.")
		msg.Wrapping = fyne.TextWrapWord
		back := widget.NewButton("Back", func() { showToken(a, w, sess, dl, t, "") })
		w.SetContent(container.NewVBox(heading, msg, back))
		return
	}

	instr := widget.NewLabel("Put the machine in pairing mode:\n" +
		"  1. Turn the machine OFF.\n" +
		"  2. Push the brew paddle LEFT and hold it.\n" +
		"  3. Turn the machine ON.\n" +
		"  4. Wait about 5 seconds.\n" +
		"  5. Push the paddle back RIGHT.\n\n" +
		"If you wired an Apollo remote to the paddle, keep it powered ON and in Manual mode, but " +
		"Disconnect it from the machine (Settings > Micra > Disconnect) so it doesn't hold the " +
		"machine's single Bluetooth connection. Then press Start.")
	instr.Wrapping = fyne.TextWrapWord

	status := widget.NewLabel("")
	status.Wrapping = fyne.TextWrapWord

	items := []fyne.CanvasObject{heading, instr}
	if t.BleAuthToken != "" {
		warn := widget.NewLabel("This replaces the current token — your app re-syncs automatically, " +
			"but any Apollo remote will need the new one.")
		warn.Wrapping = fyne.TextWrapWord
		warn.Importance = widget.WarningImportance
		items = append(items, warn)
	}

	var startBtn *widget.Button
	start := func() {
		startBtn.Disable()
		status.Importance = widget.MediumImportance
		status.SetText("Looking for your machine…")
		go func() {
			mode, addr, err := lmtoken.FindMachine(t.SerialNumber, 12*time.Second)
			if err != nil {
				fyne.Do(func() {
					if errors.Is(err, lmtoken.ErrBluetoothUnavailable) {
						showBluetoothRestart(a, w)
						return
					}
					status.Importance = widget.DangerImportance
					status.SetText(err.Error())
					startBtn.Enable()
				})
				return
			}
			// ModeConfig or ModeUnknown: proceed and let the connect decide the mode
			// (some Bluetooth stacks don't expose it in the advertisement). Only a
			// clear ModeOperative / not-found sends the user back to the steps.
			if mode == lmtoken.ModeOperative || mode == lmtoken.ModeNotFound {
				fyne.Do(func() {
					status.Importance = widget.WarningImportance
					if mode == lmtoken.ModeOperative {
						status.SetText("Found your machine, but it's still in normal mode. Do the steps above, then press Start again.")
					} else {
						status.SetText("Can't find your machine over Bluetooth. Make sure it's on and nearby, then press Start again.")
					}
					startBtn.Enable()
				})
				return
			}
			token, err := lmtoken.Provision(sess, t, addr, func(stage string) {
				fyne.Do(func() { status.SetText(stage) })
			})
			fyne.Do(func() {
				if errors.Is(err, lmtoken.ErrMachineOperative) {
					status.Importance = widget.WarningImportance
					status.SetText("The machine is in normal mode, not pairing mode. Do the steps above, then press Start again.")
					startBtn.Enable()
					return
				}
				if err != nil && token == "" {
					status.Importance = widget.DangerImportance
					status.SetText("Failed: " + err.Error())
					startBtn.Enable()
					return
				}
				t.BleAuthToken = token
				note := doneNote
				if err != nil {
					// The machine derived and is using the token; only the cloud save
					// failed. Show the token anyway, with the warning — the remote works
					// with it; the La Marzocco app may need re-syncing later.
					note = "Saved to your machine, but NOT to your La Marzocco account: " + err.Error() +
						"\n\nThe token below is live on your machine — copy it. Your app may need re-syncing, " +
						"but your remote will connect with this token. " + doneNote
				}
				showToken(a, w, sess, dl, t, note)
			})
		}()
	}
	startBtn = widget.NewButton("Start", start)
	startBtn.Importance = widget.HighImportance
	cancel := widget.NewButton("Cancel", func() { showToken(a, w, sess, dl, t, "") })

	items = append(items, startBtn, status, cancel)
	w.SetContent(container.NewVScroll(container.NewVBox(items...)))
}

// showBluetoothRestart handles the macOS first-run case: the user just granted
// Bluetooth access, but the process cached the pre-grant authorization, so the app
// must restart before it can use Bluetooth. Offers a one-click Quit & Reopen.
func showBluetoothRestart(a fyne.App, w fyne.Window) {
	heading := widget.NewLabelWithStyle("One more step", fyne.TextAlignCenter, fyne.TextStyle{Bold: true})
	msg := widget.NewLabel("Thanks for allowing Bluetooth. macOS needs LM Token to restart once before it " +
		"can use it. Click Quit & Reopen, then try Set up / Force again — you won't be asked again.")
	msg.Wrapping = fyne.TextWrapWord
	reopen := widget.NewButton("Quit & Reopen", func() { relaunchApp(a) })
	reopen.Importance = widget.HighImportance
	quit := widget.NewButton("Quit", func() { a.Quit() })
	w.SetContent(container.NewVBox(heading, msg, reopen, quit))
}

// relaunchApp starts a fresh instance of the .app bundle (macOS) and quits this
// one; elsewhere it just quits so the user reopens manually.
func relaunchApp(a fyne.App) {
	if exe, err := os.Executable(); err == nil {
		// exe = .../LM Token.app/Contents/MacOS/lmtoken-gui — the bundle is 3 up.
		bundle := filepath.Dir(filepath.Dir(filepath.Dir(exe)))
		if strings.HasSuffix(bundle, ".app") {
			_ = exec.Command("open", "-n", bundle).Start()
		}
	}
	a.Quit()
}

// debugView returns a read-only, scrollable view of the captured raw responses.
func debugView(dl *debugLog) fyne.CanvasObject {
	txt := widget.NewMultiLineEntry()
	txt.SetText(dl.String())
	txt.TextStyle = fyne.TextStyle{Monospace: true}
	txt.Disable()
	return container.NewVBox(
		widget.NewLabelWithStyle("Raw server responses", fyne.TextAlignLeading, fyne.TextStyle{Bold: true}),
		container.NewVScroll(txt),
	)
}
