// Package lmtoken fetches a La Marzocco machine's BLE auth token from the
// cloud, given an account email + password. It mints a fresh installation key
// each run (so it never collides with a previously registered one), registers
// it, signs in, lists the account's devices, and exposes each device's
// bleAuthToken.
//
// The package itself has zero third-party dependencies (Go standard library
// only). Frontends live under cmd/: a terminal CLI (cmd/lmtoken) and a Fyne
// GUI (cmd/lmtoken-gui).
package lmtoken

import (
	"bytes"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
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

// Thing is one device on the La Marzocco account.
type Thing struct {
	SerialNumber string `json:"serialNumber"`
	Name         string `json:"name"`
	ModelName    string `json:"modelName"`
	BleAuthToken string `json:"bleAuthToken"`
}

// ErrBadCredentials is returned by FetchThings when the cloud rejects the
// username/password (HTTP 401).
var ErrBadCredentials = fmt.Errorf("invalid username or password")

type client struct {
	http  *http.Client
	key   *installationKey
	debug io.Writer // when set, raw request/response bodies are logged here
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
	if c.debug != nil {
		fmt.Fprintf(c.debug, "\n[debug] %s %s -> HTTP %d\n%s\n", method, url, resp.StatusCode,
			strings.TrimSpace(string(data)))
	}
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
		return "", ErrBadCredentials
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

func (c *client) listThings(accessToken string) ([]Thing, error) {
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
	var things []Thing
	if err := json.Unmarshal(data, &things); err != nil {
		return nil, fmt.Errorf("parsing things: %w", err)
	}
	return things, nil
}

// ---------------------------------------------------------------- public API

// FetchThings runs the whole cloud flow — mint + register a fresh
// installation key, sign in, list devices — and returns the account's
// devices. progress (may be nil) is called with a short human-readable label
// as each stage starts.
func FetchThings(username, password string, progress func(stage string)) ([]Thing, error) {
	report := func(s string) {
		if progress != nil {
			progress(s)
		}
	}
	key, err := newInstallationKey()
	if err != nil {
		return nil, err
	}
	c := &client{http: &http.Client{Timeout: 30 * time.Second}, key: key}

	report("registering client...")
	if err := c.register(); err != nil {
		return nil, err
	}
	report("signing in...")
	accessToken, err := c.signIn(username, password)
	if err != nil {
		return nil, err
	}
	report("listing devices...")
	return c.listThings(accessToken)
}
