# lmtoken changelog

Notes for each `lmtoken-vX.Y.Z` release. The release workflow extracts the
section whose heading matches the tag (minus the `lmtoken-` prefix, e.g.
`## v0.2.1`) and appends the standard download/install boilerplate. Keep the
section for a version complete before tagging it.

## v0.2.1

- Fixed: when the machine derives a new token but saving it to your La Marzocco
  account fails, the token is now handed to you anyway (with a warning) instead
  of being discarded. The machine starts using that token the moment it's
  written, so you need it even if the account save didn't go through — your
  remote connects with it; your La Marzocco app may just need re-syncing. Both
  the app and the CLI now surface it.

## v0.2.0

- Set up a Bluetooth token when the cloud has none: sign in, and if your machine
  has no token, lmtoken can provision one over Bluetooth (put the machine in
  pairing mode and follow the prompts) — in both the app and the CLI.
- Cross-platform: macOS, Linux, and Windows, for both the app and the CLI.
- `-force` re-mints a fresh token even if one already exists; `-debug` prints
  raw server responses and Bluetooth diagnostics; `-scan` lists nearby
  Bluetooth devices to confirm your machine is visible.
- macOS app and CLI are signed and notarized.
