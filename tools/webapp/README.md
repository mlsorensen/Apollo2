# Apollo 2 web app

The shot-history page the device serves at `http://<device-ip>/` — a
single-file React + Material UI app, gzipped and embedded in the firmware
as `include/platform_esp32/webapp_dist.h`.

It styles itself from the DEVICE'S ACTIVE THEME at load (`/api/summary`
carries the palette), so the page always matches the screen.

## Building

You don't, normally: the embedded header is a **generated build artifact, not
committed**, and every device target depends on it —

    make build      # or make flash, make build-p4-5, ...

rebuilds it whenever anything in this directory changes, and the release
workflow does the same. That's what keeps the page a board serves from drifting
from the source here. To build it on its own:

    make webapp     # == tools/webapp/build.sh

**This makes node a dev dependency of any device build** (`brew install node`;
any recent LTS). `make sim` doesn't need it — the simulator has no web server.
If you invoke `pio run -e <env>` directly instead of going through `make`,
build the header first or the firmware won't compile.

`node_modules/`, `dist/` and the generated header are all git-ignored.

## Development

    npm install
    npm run dev     # vite dev server on localhost

The dev server has no device behind it, so `/api/*` calls fail — proxy them to a
real board in `vite.config.js` when working on data-driven parts.
