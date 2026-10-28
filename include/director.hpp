#pragma once
// Autonomous behaviour policy: decides when the robot greets, asks, fills silence or calls
// out, from tracking and interaction state only. It issues no I/O; the caller performs the
// returned action. Portable (no Arduino/ESP-IDF); the caller owns the single clock.
#include <cstdint>

#include "interaction.hpp"

namespace stackchan {

enum class Act { none, greet, ask, filler, lonely };

struct DirectorConfig {
  std::uint64_t greet_gap_ms = 8000;        // face must have been absent this long to greet again
  std::uint64_t greet_cooldown_ms = 20000;
  std::uint64_t stable_ms = 3000;           // face tracked this long before asking
  std::uint64_t ask_cooldown_ms = 30000;
  std::uint64_t filler_first_ms = 1500;     // first filler this long after a request starts
  std::uint64_t filler_interval_ms = 3000;
  unsigned filler_max = 2;
  std::uint64_t lonely_after_ms = 45000;    // nobody seen for this long
  std::uint64_t lonely_cooldown_ms = 60000;
};

class Director {
 public:
  explicit Director(DirectorConfig c = {}) : cfg_(c) {}

  // Call once per loop iteration. `enabled` false keeps the bookkeeping but never acts.
  Act step(std::uint64_t now, bool tracking, State state, bool enabled) {
    if (started_ && now < clock_) return Act::none;   // regressing clock: ignore
    clock_ = now;
    if (!started_) {
      started_ = true;
      lost_since_ = now;
      last_lonely_ = now;
    }
    Act act = Act::none;
    if (tracking && !prev_tracking_) {
      track_since_ = now;
      if (now - lost_since_ >= cfg_.greet_gap_ms &&
          (!greeted_ || now - last_greet_ >= cfg_.greet_cooldown_ms)) {
        greeted_ = true;
        last_greet_ = now;
        act = Act::greet;
      }
    }
    if (!tracking && prev_tracking_) lost_since_ = now;
    prev_tracking_ = tracking;

    const bool waiting = state == State::waiting;
    if (waiting && !was_waiting_) { wait_since_ = now; fillers_ = 0; }
    was_waiting_ = waiting;
    if (!enabled) return Act::none;
    if (act != Act::none) return act;

    if (waiting && fillers_ < cfg_.filler_max &&
        now - wait_since_ >= cfg_.filler_first_ms + fillers_ * cfg_.filler_interval_ms) {
      ++fillers_;
      return Act::filler;
    }
    if (state == State::idle && tracking && now - track_since_ >= cfg_.stable_ms &&
        (!asked_ || now - last_ask_ >= cfg_.ask_cooldown_ms)) {
      asked_ = true;
      last_ask_ = now;
      return Act::ask;
    }
    if (state == State::idle && !tracking && now - lost_since_ >= cfg_.lonely_after_ms &&
        now - last_lonely_ >= cfg_.lonely_cooldown_ms) {
      last_lonely_ = now;
      return Act::lonely;
    }
    return Act::none;
  }

  unsigned fillers_said() const { return fillers_; }   // 1 after the first filler, and so on

 private:
  DirectorConfig cfg_;
  std::uint64_t clock_ = 0, track_since_ = 0, lost_since_ = 0, last_greet_ = 0, last_ask_ = 0,
                last_lonely_ = 0, wait_since_ = 0;
  unsigned fillers_ = 0;
  bool started_ = false, prev_tracking_ = false, was_waiting_ = false, greeted_ = false,
       asked_ = false;
};

}  // namespace stackchan
