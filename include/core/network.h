#pragma once

// WiFi station + NTP port. Optionally joins the customer's home WiFi (station
// mode), gets a DHCP address, and keeps the clock correct via NTP. Credentials
// are entered over the AP setup portal (see begin_setup_portal) — typed on a
// phone, not the panel — so the device screen is the source of truth for status
// and the customer can never be locked out if their WiFi changes.
//
// The host fakes this for the simulator.

namespace core {

// Station connection state, driving the Home WiFi glyph + the Settings status
// line. Disabled = WiFi off; Connecting = associating/awaiting DHCP;
// Connected = has an IP; Failed = last attempt failed (bad creds / out of range).
enum class NetState { Disabled, Connecting, Connected, Failed };

class INetwork {
 public:
  virtual ~INetwork() = default;

  virtual NetState status() const = 0;
  virtual const char* ssid() const = 0;  // configured SSID ("" if none)
  virtual const char* ip() const = 0;    // DHCP address when Connected, else ""

  // Master on/off. Disabling drops the station; enabling (re)connects from the
  // saved credentials. Persisted.
  virtual bool enabled() const = 0;
  virtual void set_enabled(bool on) = 0;

  // Raise the SoftAP + web form so the user can (re)enter WiFi credentials from a
  // phone. Always available — the recovery path when the saved network changes.
  virtual void begin_setup_portal() = 0;
  virtual void stop_setup_portal() = 0;  // tear the AP down (user cancelled)

  // The setup AP's network name + URL, for the on-screen "join this / open that"
  // instructions while the portal is up.
  virtual const char* setup_ssid() const = 0;
  virtual const char* setup_url() const = 0;

  // Clear the saved credentials (and drop the station).
  virtual void forget() = 0;

  // POSIX TZ string (e.g. "AEST-10AEDT,M10.1.0,M4.1.0"); applied to NTP so local
  // time is correct with automatic DST. Persisted.
  virtual const char* timezone() const = 0;
  virtual void set_timezone(const char* tz) = 0;

  // NTP server hostname (defaults to a public pool). Persisted.
  virtual const char* ntp_server() const = 0;
  virtual void set_ntp_server(const char* host) = 0;

  // Whether to sync the clock from NTP while connected (default on). Off leaves the
  // clock on whatever was set manually / last synced. Persisted.
  virtual bool ntp_enabled() const = 0;
  virtual void set_ntp_enabled(bool on) = 0;
};

}  // namespace core
