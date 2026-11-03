#pragma once

#include <cstdint>

namespace core {

// Which paddle wiring a P4 unit carries. The boards have TWO native sets
// (the corner pins for the opto cable, the 5 V-end pins for the Apollo
// Link); kAuto polls and drives both, which is how every unit shipped and
// needs no setting — but a dead or shorted pin on the unused set then holds
// the paddle closed (the X-8's dead GPIO5 read LOW, 2026-11-03). Picking the
// set in use leaves the other one an open input. Stored per unit ("padwire").
enum class PaddleWiring : uint8_t { kAuto = 0, kLink = 1, kCable = 2 };

// Paddle hardware port. Two lines, both optional per board:
//  - drive: an output that closes the machine's paddle circuit (the Micra's
//    white/black pair) — a solid-state "contact". On the 4.3C this is the
//    isolated DO0 output; on plain-GPIO boards it's a pin driving an external
//    relay. Closing it starts the machine exactly like the physical paddle.
//  - sense: an input reading the real paddle switch (rerouted to the board),
//    so firmware can relay the user's paddle edge-triggered.
// available() == false (line not wired on this board) turns the brew
// controller into a display-only bystander.
class IPaddle {
 public:
  virtual ~IPaddle() = default;
  virtual bool available() const = 0;  // both lines usable on this board
  virtual bool sensed() = 0;           // physical paddle is currently ON
  virtual void drive(bool closed) = 0; // close/open the machine's paddle line
  // Hand the paddle to Apollo (true) or back to the machine (false) where the
  // wiring has a transfer switch — the Apollo Link's CTRL line. Boards without
  // one (the 4.3C's terminals, the plain opto cable) ignore it. The brew
  // controller's paddle breaker releases it so a unit that has stopped
  // trusting its own sense input leaves the machine usable as stock.
  virtual void own(bool /*owned*/) {}
  // Boards with both native sets offer the wiring choice; the 4.3C's terminals
  // and single-set boards have exactly one wiring and report false.
  virtual bool wiring_selectable() const { return false; }
  virtual void set_wiring(PaddleWiring /*w*/) {}
  virtual PaddleWiring wiring() const { return PaddleWiring::kAuto; }
};

}  // namespace core
