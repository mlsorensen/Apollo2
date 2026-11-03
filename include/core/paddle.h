#pragma once

namespace core {

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
};

}  // namespace core
