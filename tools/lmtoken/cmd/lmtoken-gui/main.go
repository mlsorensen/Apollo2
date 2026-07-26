// Command lmtoken-gui is the point-and-click frontend: a small Fyne window
// with a login form, a device picker (when the account has several machines),
// and the resulting BLE token with a copy-to-clipboard button. Same cloud flow
// as the CLI — everything lives in the lmtoken package.
package main

import (
	"errors"
	"fmt"

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
	w.Resize(fyne.NewSize(440, 340))
	showLogin(a, w)
	w.ShowAndRun()
}

// showLogin renders the credentials form. On success it advances to the
// device picker (or straight to the token when the account has one machine).
func showLogin(a fyne.App, w fyne.Window) {
	email := widget.NewEntry()
	email.SetPlaceHolder("you@example.com")
	email.SetText(a.Preferences().String(prefEmailKey))

	password := widget.NewPasswordEntry()
	password.SetPlaceHolder("password")

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
		status.SetText("connecting...")
		a.Preferences().SetString(prefEmailKey, email.Text)

		go func() {
			things, err := lmtoken.FetchThings(email.Text, password.Text, func(stage string) {
				fyne.Do(func() { status.SetText(stage) })
			})
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
					showToken(a, w, things[0])
				} else {
					showPicker(a, w, things)
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
	sub := widget.NewLabel("Fetches your machine's Bluetooth auth token from the La Marzocco cloud. Credentials are sent only to lamarzocco.io.")
	sub.Wrapping = fyne.TextWrapWord

	form := widget.NewForm(
		widget.NewFormItem("Email", email),
		widget.NewFormItem("Password", password),
	)
	w.SetContent(container.NewVBox(
		heading, sub, form, signIn, status,
	))
	w.Canvas().Focus(email)
}

// showPicker lists the account's machines; tapping one shows its token.
func showPicker(a fyne.App, w fyne.Window, things []lmtoken.Thing) {
	heading := widget.NewLabelWithStyle("Choose a machine",
		fyne.TextAlignCenter, fyne.TextStyle{Bold: true})

	rows := make([]fyne.CanvasObject, 0, len(things))
	for _, t := range things {
		t := t
		b := widget.NewButton(fmt.Sprintf("%s — %s (%s)", t.Name, t.ModelName, t.SerialNumber),
			func() { showToken(a, w, t) })
		rows = append(rows, b)
	}
	back := widget.NewButton("Back", func() { showLogin(a, w) })
	w.SetContent(container.NewBorder(heading, back, nil, nil,
		container.NewVScroll(container.NewVBox(rows...))))
}

// showToken displays the device's BLE token with a copy button.
func showToken(a fyne.App, w fyne.Window, t lmtoken.Thing) {
	if t.BleAuthToken == "" {
		showError(a, w, fmt.Sprintf("The cloud returned an empty BLE token for %s (serial %s).",
			t.Name, t.SerialNumber))
		return
	}
	heading := widget.NewLabelWithStyle(fmt.Sprintf("%s — %s", t.Name, t.ModelName),
		fyne.TextAlignCenter, fyne.TextStyle{Bold: true})
	serial := widget.NewLabelWithStyle("serial "+t.SerialNumber, fyne.TextAlignCenter, fyne.TextStyle{})

	token := widget.NewEntry()
	token.SetText(t.BleAuthToken)
	token.MultiLine = true
	token.Wrapping = fyne.TextWrapBreak
	token.TextStyle = fyne.TextStyle{Monospace: true}
	// Keep the field showing the real token even if the user types into it.
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

	hint := widget.NewLabel("Paste this token into the remote's setup page (join its Micra-Setup Wi-Fi and open http://192.168.4.1).")
	hint.Wrapping = fyne.TextWrapWord

	back := widget.NewButton("Start over", func() { showLogin(a, w) })
	w.SetContent(container.NewVBox(
		heading, serial, token, copyBtn, hint, back,
	))
}

func showError(a fyne.App, w fyne.Window, msg string) {
	status := widget.NewLabel(msg)
	status.Wrapping = fyne.TextWrapWord
	status.Importance = widget.DangerImportance
	back := widget.NewButton("Back", func() { showLogin(a, w) })
	w.SetContent(container.NewVBox(status, back))
}
