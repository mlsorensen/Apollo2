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
};

}  // namespace core
