#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace stackchan {
enum class State { idle, waiting, answering, cancelled, error };
enum class Face { neutral, thinking, happy, answer, cancelled, error };
struct Config {
  std::uint64_t timeout_ms = 15000;
  std::uint64_t observation_ttl_ms = 500;
  std::uint64_t touch_duration_ms = 400;
};
struct Output {
  State state;
  Face face;
  bool track_head;
  float normalized_x;
};

// Single-owner policy core: serialize calls on one task. All times share one
// boot-local monotonic millisecond clock. This is not a scheduler or driver.
class Interaction {
 public:
  explicit Interaction(Config config = {}) : config_(config) {}
  std::uint64_t start(std::uint64_t now) {
    if (!advance(now)) return 0;
    active_id_ = ++sequence_;
    started_ = now;
    state_ = State::waiting;
    touching_ = false;
    return active_id_;
  }
  bool reply(std::uint64_t id, std::uint64_t now) {
    if (!advance(now)) return false;
    if (state_ != State::waiting || id != active_id_) return false;
    state_ = State::answering;
    touching_ = false;
    return true; // Caller may now publish the separately validated answer.
  }
  bool fail(std::uint64_t id, std::uint64_t now) {
    if (!advance(now)) return false;
    if (state_ != State::waiting || id != active_id_) return false;
    state_ = State::error;
    touching_ = false;
    return true;
  }
  bool cancel(std::uint64_t now) {
    if (!advance(now)) return false;
    if (state_ != State::waiting && state_ != State::answering) return false;
    state_ = State::cancelled;
    touching_ = false;
    return true;
  }
  void finish(std::uint64_t now) {
    if (advance(now) && state_ == State::answering) state_ = State::idle;
  }
  void touch(std::uint64_t now) {
    if (!advance(now)) return;
    if (state_ == State::idle || state_ == State::waiting) {
      touching_ = true;
      touched_ = now;
    }
  }
  bool observe(float x, bool present, std::uint64_t captured,
               std::uint64_t now) {
    if (!advance(now) || captured > now || !std::isfinite(x)) return false;
    if ((have_observation_ && captured < observed_) ||
        now - captured > config_.observation_ttl_ms) return false;
    have_observation_ = true;
    observed_ = captured;
    present_ = present;
    x_ = std::clamp(x, -1.0F, 1.0F);
    return true;
  }
  Output output(std::uint64_t now) {
    advance(now); // A regressing read uses the most recent valid clock value.
    Face face = Face::neutral;
    switch (state_) {
      case State::waiting: face = Face::thinking; break;
      case State::answering: face = Face::answer; break;
      case State::cancelled: face = Face::cancelled; break;
      case State::error: face = Face::error; break;
      case State::idle: break;
    }
    if (touching_ && (state_ == State::idle || state_ == State::waiting))
      face = Face::happy;
    const bool track = have_observation_ && present_ &&
        clock_ - observed_ <= config_.observation_ttl_ms;
    return {state_, face, track, track ? x_ : 0.0F};
  }
 private:
  bool advance(std::uint64_t now) {
    if (now < clock_) return false;
    clock_ = now;
    if (touching_ && now - touched_ >= config_.touch_duration_ms)
      touching_ = false;
    if (state_ == State::waiting && now - started_ >= config_.timeout_ms) {
      state_ = State::error;
      touching_ = false;
    }
    return true;
  }
  Config config_;
  State state_ = State::idle;
  std::uint64_t sequence_ = 0, active_id_ = 0, clock_ = 0;
  std::uint64_t started_ = 0, touched_ = 0, observed_ = 0;
  bool touching_ = false, have_observation_ = false, present_ = false;
  float x_ = 0;
};
} // namespace stackchan
