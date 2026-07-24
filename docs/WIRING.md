# Wiring the paddle (Auto shot)

This is the **optional** hardware step that unlocks **Auto shot** (the
firmware stops the shot at your target weight) and **auto‑flush**. Apollo sits
*in the middle* of the Micra's paddle circuit: the physical paddle switch now
feeds Apollo's sense input, and Apollo's isolated output stands in for the
paddle on the Micra's controller. Nothing else about the machine changes, and
**Shot detect** works fine without any of this.

The paddle circuit itself is low‑voltage (the Micra's controller puts ~5 V on
it), but you will have the machine open — **unplug the Micra first**.

> **Disclaimer:** this is a DIY modification. Done properly it is safe and
> fully reversible, but it is not endorsed by La Marzocco and you do it
> entirely at your own risk.

> **Note:** while this wiring is in place, the paddle works **only through
> Apollo** — with the controller off, unplugged, or missing, flipping the
> paddle does nothing. Because the tap is made with pluggable connectors,
> undoing it is easy: open the top and temporarily reconnect the paddle's
> original connectors to run the machine without Apollo.

## Parts

- A **3‑conductor cable** to run between Apollo and the machine — for example
  [this one](https://a.co/d/07cSW1AY), but any 3‑conductor cable works.
- **Bullet connectors** (or your favorite splice) for the in‑machine pigtail,
  so everything stays pluggable and reversible.
- **P4 boards only**: a single‑channel **PC817‑style opto‑isolator module**,
  e.g. [this one](https://a.co/d/03qHqcyM) — anything similar works. (The
  S3‑4.3C has isolators built in; no module needed.) A
  [2.54 mm screw‑terminal block](https://a.co/d/0eVdsD4h) that clips onto the
  P4's GPIO header makes the board‑side connection clean and solder‑free.

Although there are **four** connections at the machine end (Micra white,
Micra black, and the two paddle‑switch wires), one 3‑conductor cable is all
you need — **ground is shared**. It just means the ground conductor gets
**two connectors** at the machine end.

## Inside the Micra

Remove the Micra's top panel. Toward the front you'll find a **black loom**
carrying a thin **white** and a thin **black** wire, ending in connectors that
join the paddle switch:

![Micra top open — the black loom](img/wiring/micra-top-loom.jpg)

Two connectors can be temporarily unplugged for easier access to the loom
(arrows):

![Temporarily disconnect these for access](img/wiring/micra-top-disconnect.jpg)

Prepare your cable with bullet connectors, run it into the machine, unplug the
paddle connectors, and wire Apollo in the middle as described below. What
you're looking at:

- The **thin wires** go to the Micra's controller: **white = +5 V**,
  **black = ground**.
- The **thicker wires** go to the physical paddle switch. The paddle
  connectors have no polarity — either direction works.

![Loom pulled up, tapped with bullet connectors](img/wiring/micra-loom-tap.jpg)

## ESP32‑S3‑Touch‑LCD‑4.3C / 4.3C‑BOX (built‑in isolators)

The 4.3C's isolated DI/DO terminal block already contains the opto‑isolators,
so the 3‑conductor cable is the whole job:

| Board terminal | Connects to |
|----------------|-------------|
| **DO0** | Micra **white** (thin wire, controller +5 V) |
| **DI0** | one paddle‑switch wire |
| **GND** | Micra **black** (thin wire) **and** the other paddle‑switch wire (shared) |

Note: use the **GND** terminal for the paddle return, **not DI COM** — DI COM
is internally biased on this board, and the paddle must close DI0 to ground to
be sensed.

A finished 4.3C cable — bare wires for the screw terminals at the board end,
bullet connectors at the machine end (two on the shared ground conductor):

![Finished 4.3C cable](img/wiring/cable-s3.jpg)

## ESP32‑P4 boards (external opto module)

On the P4‑WIFI6‑Touch‑LCD‑5 (and the 4.3), the paddle uses three native pins
that sit **side by side** on the corner of the GPIO header — **GND, GPIO 52,
GPIO 51**, in that order on the silkscreen — so a
[2.54 mm 3‑pin screw‑terminal block](https://a.co/d/0eVdsD4h) clips straight
onto them, no soldering:

![The P4 header corner — GND, 52, 51 in a row](img/wiring/p4-header-pins.jpg)

![Screw-terminal block on the pins, cable attached](img/wiring/p4-header-terminal.jpg)

The cable can run out through a hole in the 3D‑printed backplate. From there,
the opto module provides the isolation:

![Apollo P4 → opto module → Micra wiring](img/wiring/p4-opto-wiring.svg)

In detail:

- **Drive (Apollo → Micra):** Apollo **GPIO 52** → opto module input **IO**,
  and Apollo **GND** → the module's input **GND**. On the output side leave
  **VCC unconnected** ("VCC suspended") so OUT/GND form an isolated dry
  contact: module **OUT** → Micra **white**, module output **GND** → Micra
  **black**. When Apollo raises GPIO 52, the contact closes — exactly like the
  paddle.
- **Sense (paddle → Apollo):** one paddle‑switch wire → Apollo **GPIO 51**,
  the other → Apollo **GND**. The physical paddle now touches only Apollo,
  never the Micra.

A finished P4 cable with the opto module spliced in near the machine end
(shown before wrapping the module in heat‑shrink — do wrap it, so nothing can
short out inside the machine). Of the cable's conductors, **red + black feed
the opto module's input** — that pair carries Apollo's drive and becomes the
Micra's paddle signal on the isolated side — while **white + the second
black** run to the physical paddle switch:

![Finished P4 cable with inline opto module](img/wiring/cable-p4-opto.jpg)

## Afterwards

Turn on **Settings → Micra → Settings → Wired paddle** so the firmware uses
the harness — the **Auto shot** mode then appears on the Home shot pill, and
**Auto flush** becomes available. See the [manual](../MANUAL.md) for what each
mode does.
