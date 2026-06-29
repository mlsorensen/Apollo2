// Command lmtoken fetches a La Marzocco machine's BLE auth token from the
// cloud, given an account email + password. It mints a fresh installation key
// each run (so it never collides with a previously registered one), registers
// it, signs in, lists the account's devices, and prints the selected device's
// bleAuthToken.
//
// Zero third-party dependencies: only the Go standard library. Build a single
// distributable binary with `go build`, or cross-compile, e.g.
//
//	GOOS=darwin  GOARCH=arm64 go build -o lmtoken-macos-arm64 .
//	GOOS=linux   GOARCH=amd64 go build -o lmtoken-linux-amd64 .
//	GOOS=windows GOARCH=amd64 go build -o lmtoken.exe .
//
// Credentials: read from $LAMARZOCCO_USERNAME / $LAMARZOCCO_PASSWORD if set,
// otherwise prompted (password input is hidden on Unix via stty). The BLE token
// is printed to stdout; everything else (prompts, device list) goes to stderr,
// so `TOKEN=$(lmtoken)` works.
package main

import (
	"bufio"
	"bytes"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"time"
)

const customerAppURL = "https://lion.lamarzocco.io/api/customer-app"

func b64(b []byte) string { return base64.StdEncoding.EncodeToString(b) }

// installationKey holds the per-run identity registered with the cloud.
type installationKey struct {
	id     string
	priv   *ecdsa.PrivateKey
	pubDER []byte // SubjectPublicKeyInfo DER
	secret []byte // 32 bytes, derived from id + pubDER
}

// newInstallationKey mints a fresh P-256 key and derives the shared secret
// exactly as pylamarzocco's generate_installation_key does.
func newInstallationKey() (*installationKey, error) {
	id, err := uuidV4()
	if err != nil {
		return nil, err
	}
	priv, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return nil, err
	}
	pubDER, err := x509.MarshalPKIXPublicKey(&priv.PublicKey)
	if err != nil {
		return nil, err
	}
	// secret = sha256( id "." b64(pubDER) "." b64(sha256(id)) )
	instHash := sha256.Sum256([]byte(id))
	triple := id + "." + b64(pubDER) + "." + b64(instHash[:])
	secret := sha256.Sum256([]byte(triple))
	return &installationKey{id: id, priv: priv, pubDER: pubDER, secret: secret[:]}, nil
}

func (k *installationKey) pubB64() string { return b64(k.pubDER) }

// baseString = id "." b64(sha256(pubDER)) — used for the registration proof.
func (k *installationKey) baseString() string {
	h := sha256.Sum256(k.pubDER)
	return k.id + "." + b64(h[:])
}

// requestProof reimplements La Marzocco's custom byte-rotation proof (Y5.e).
func requestProof(baseString string, secret32 []byte) string {
	work := make([]byte, len(secret32))
	copy(work, secret32)
	for _, bv := range []byte(baseString) {
		idx := int(bv) % 32
		shiftIdx := (idx + 1) % 32
		shift := int(work[shiftIdx] & 7) // 0-7
		xr := int(bv) ^ int(work[idx])
		rotated := ((xr << shift) | (xr >> (8 - shift))) & 0xFF
		work[idx] = byte(rotated)
	}
	sum := sha256.Sum256(work)
	return b64(sum[:])
}

// extraHeaders builds the signed headers required for signin and API calls.
func (k *installationKey) extraHeaders() (map[string]string, error) {
	nonce, err := uuidV4()
	if err != nil {
		return nil, err
	}
	timestamp := strconv.FormatInt(time.Now().UnixMilli(), 10)
	proofInput := k.id + "." + nonce + "." + timestamp
	proof := requestProof(proofInput, k.secret)
	signatureData := proofInput + "." + proof
	digest := sha256.Sum256([]byte(signatureData))
	sig, err := ecdsa.SignASN1(rand.Reader, k.priv, digest[:]) // DER, matches Python
	if err != nil {
		return nil, err
	}
	return map[string]string{
		"X-App-Installation-Id": k.id,
		"X-Timestamp":           timestamp,
		"X-Nonce":               nonce,
		"X-Request-Signature":   b64(sig),
	}, nil
}

func uuidV4() (string, error) {
	var b [16]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", err
	}
	b[6] = (b[6] & 0x0f) | 0x40 // version 4
	b[8] = (b[8] & 0x3f) | 0x80 // variant 10
	return fmt.Sprintf("%x-%x-%x-%x-%x", b[0:4], b[4:6], b[6:8], b[8:10], b[10:16]), nil
}

// ---------------------------------------------------------------- HTTP client

type client struct {
	http *http.Client
	key  *installationKey
}

func (c *client) doJSON(method, url string, headers map[string]string, body any) ([]byte, int, error) {
	var rdr io.Reader
	if body != nil {
		buf, err := json.Marshal(body)
		if err != nil {
			return nil, 0, err
		}
		rdr = bytes.NewReader(buf)
	}
	req, err := http.NewRequest(method, url, rdr)
	if err != nil {
		return nil, 0, err
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	resp, err := c.http.Do(req)
	if err != nil {
		return nil, 0, err
	}
	defer resp.Body.Close()
	data, err := io.ReadAll(resp.Body)
	return data, resp.StatusCode, err
}

func (c *client) register() error {
	headers := map[string]string{
		"X-App-Installation-Id": c.key.id,
		"X-Request-Proof":       requestProof(c.key.baseString(), c.key.secret),
	}
	body := map[string]string{"pk": c.key.pubB64()}
	data, status, err := c.doJSON("POST", customerAppURL+"/auth/init", headers, body)
	if err != nil {
		return err
	}
	if status < 200 || status >= 300 {
		return fmt.Errorf("register failed (HTTP %d): %s", status, strings.TrimSpace(string(data)))
	}
	return nil
}

func (c *client) signIn(username, password string) (string, error) {
	headers, err := c.key.extraHeaders()
	if err != nil {
		return "", err
	}
	body := map[string]string{"username": username, "password": password}
	data, status, err := c.doJSON("POST", customerAppURL+"/auth/signin", headers, body)
	if err != nil {
		return "", err
	}
	if status == 401 {
		return "", fmt.Errorf("invalid username or password")
	}
	if status < 200 || status >= 300 {
		return "", fmt.Errorf("signin failed (HTTP %d): %s", status, strings.TrimSpace(string(data)))
	}
	var tok struct {
		AccessToken string `json:"accessToken"`
	}
	if err := json.Unmarshal(data, &tok); err != nil {
		return "", fmt.Errorf("parsing token response: %w", err)
	}
	if tok.AccessToken == "" {
		return "", fmt.Errorf("no accessToken in response: %s", string(data))
	}
	return tok.AccessToken, nil
}

type thing struct {
	SerialNumber string `json:"serialNumber"`
	Name         string `json:"name"`
	ModelName    string `json:"modelName"`
	BleAuthToken string `json:"bleAuthToken"`
}

func (c *client) listThings(accessToken string) ([]thing, error) {
	headers, err := c.key.extraHeaders()
	if err != nil {
		return nil, err
	}
	headers["Authorization"] = "Bearer " + accessToken
	data, status, err := c.doJSON("GET", customerAppURL+"/things", headers, nil)
	if err != nil {
		return nil, err
	}
	if status < 200 || status >= 300 {
		return nil, fmt.Errorf("list things failed (HTTP %d): %s", status, strings.TrimSpace(string(data)))
	}
	var things []thing
	if err := json.Unmarshal(data, &things); err != nil {
		return nil, fmt.Errorf("parsing things: %w", err)
	}
	return things, nil
}

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

func chooseThing(things []thing) (thing, error) {
	if len(things) == 0 {
		return thing{}, fmt.Errorf("no devices found on this account")
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
			return thing{}, err
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

	key, err := newInstallationKey()
	if err != nil {
		return err
	}
	c := &client{http: &http.Client{Timeout: 30 * time.Second}, key: key}

	fmt.Fprintln(os.Stderr, "registering client...")
	if err := c.register(); err != nil {
		return err
	}
	fmt.Fprintln(os.Stderr, "signing in...")
	accessToken, err := c.signIn(username, password)
	if err != nil {
		return err
	}
	fmt.Fprintln(os.Stderr, "listing devices...")
	things, err := c.listThings(accessToken)
	if err != nil {
		return err
	}

	var t thing
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
