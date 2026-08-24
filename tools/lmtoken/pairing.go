package lmtoken

// Pairing/provisioning calls, layered on the same signed cloud client the token
// fetch uses. The machine's bleAuthToken is DERIVED by the machine, not by the
// cloud: the cloud mints an (encrypted) pairSeed, the app writes it to the
// machine over BLE in configuration mode, the machine derives the token and
// hands it back, and the app uploads it via confirm. These calls cover the two
// cloud halves (start + confirm); the BLE seed->token step happens elsewhere.

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"
)

// Session is a registered + signed-in cloud session, reusable across the
// multiple calls a provision flow needs (list, start, confirm).
type Session struct {
	c           *client
	accessToken string
}

// NewSession mints a fresh installation key, registers it, and signs in. debug
// (may be nil) receives raw request/response bodies for troubleshooting.
func NewSession(username, password string, debug io.Writer) (*Session, error) {
	key, err := newInstallationKey()
	if err != nil {
		return nil, err
	}
	c := &client{http: &http.Client{Timeout: 30 * time.Second}, key: key, debug: debug}
	if err := c.register(); err != nil {
		return nil, err
	}
	tok, err := c.signIn(username, password)
	if err != nil {
		return nil, err
	}
	return &Session{c: c, accessToken: tok}, nil
}

// Things lists the account's devices (same payload as FetchThings).
func (s *Session) Things() ([]Thing, error) {
	return s.c.listThings(s.accessToken)
}

func (s *Session) authHeaders() (map[string]string, error) {
	h, err := s.c.key.extraHeaders()
	if err != nil {
		return nil, err
	}
	h["Authorization"] = "Bearer " + s.accessToken
	return h, nil
}

// StartPairing asks the cloud for a fresh pairing seed for a serial number.
// The returned string is written verbatim to the machine's seed characteristic.
func (s *Session) StartPairing(serial string) (string, error) {
	headers, err := s.authHeaders()
	if err != nil {
		return "", err
	}
	body := map[string]string{"serialNumber": serial}
	data, status, err := s.c.doJSON("POST", customerAppURL+"/thing-pairing/start", headers, body)
	if err != nil {
		return "", err
	}
	if status < 200 || status >= 300 {
		return "", fmt.Errorf("start pairing failed (HTTP %d): %s", status, strings.TrimSpace(string(data)))
	}
	var resp struct {
		PairSeed string `json:"pairSeed"`
	}
	if err := json.Unmarshal(data, &resp); err != nil {
		return "", fmt.Errorf("parsing start response: %w (%s)", err, string(data))
	}
	if resp.PairSeed == "" {
		return "", fmt.Errorf("start returned an empty pairSeed: %s", string(data))
	}
	return resp.PairSeed, nil
}

// Unpair removes the machine's binding from the account (cloud-side). Needed
// before re-pairing an already-paired machine — confirm rejects an already-paired
// thing with THING_ALREADY_PAIRED. Safe for a machine you own: the normal pairing
// flow re-adds it.
func (s *Session) Unpair(serial string) error {
	headers, err := s.authHeaders()
	if err != nil {
		return err
	}
	data, status, err := s.c.doJSON("POST", customerAppURL+"/thing-pairing/"+serial+"/unpair", headers, nil)
	if err != nil {
		return err
	}
	if status < 200 || status >= 300 {
		return fmt.Errorf("unpair failed (HTTP %d): %s", status, strings.TrimSpace(string(data)))
	}
	return nil
}

// ConfirmRequest is the body of thing-pairing/confirm. Field order/names match
// the cloud's serializer. Nullable fields are pointers so they can be sent null.
type ConfirmRequest struct {
	SerialNumber string  `json:"serialNumber"`
	Name         string  `json:"name"`
	PairToken    string  `json:"pairToken"`
	BleAuthToken string  `json:"bleAuthToken"`
	OfflineMode  bool    `json:"offlineMode"`
	RemoveOthers bool    `json:"removeOthers"`
	ZipCode      string  `json:"zipCode"`
	Country      string  `json:"country"`
	Location     *string `json:"location"`
	CompanyID    *string `json:"companyId"`
	SiteID       *string `json:"siteId"`
}

// ConfirmPairing uploads the derived token to the cloud. Returns the raw
// ThingResponse body (which echoes bleAuthToken) for inspection.
func (s *Session) ConfirmPairing(req ConfirmRequest) ([]byte, error) {
	headers, err := s.authHeaders()
	if err != nil {
		return nil, err
	}
	data, status, err := s.c.doJSON("POST", customerAppURL+"/thing-pairing/confirm", headers, req)
	if err != nil {
		return nil, err
	}
	if status < 200 || status >= 300 {
		return data, fmt.Errorf("confirm failed (HTTP %d): %s", status, strings.TrimSpace(string(data)))
	}
	return data, nil
}
