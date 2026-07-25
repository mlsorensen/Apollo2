# Apollo 2 web app

The shot-history page the device serves at `http://<device-ip>/` — a
single-file React + Material UI app, gzipped and embedded in the firmware
(`include/platform_esp32/webapp_dist.h`, committed).

It styles itself from the DEVICE'S ACTIVE THEME at load (`/api/summary`
carries the palette), so the page always matches the screen.

Rebuild after changing the app (needs node):

    tools/webapp/build.sh

`node_modules/` and `dist/` are git-ignored; the generated header is not.
