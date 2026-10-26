// Connects the portable Interaction core to the K151 display, touch and head servo.
// Inputs are synthetic: no camera, no network. Single-task ownership: the core is only
// touched from loop(). Build with -I<repo>/include (see HANDOFF.md).
//
// Screen taps (after the first tap arms the synthetic face):
//   left third   -> start a question; a simulated reply arrives after kReplyDelayMs
//   middle third -> cancel the active question (its simulated reply is then late)
//   right third  -> touch feedback
#include <Arduino.h>
#include <M5StackChan.h>
#include "interaction.hpp"

using stackchan::Face;
using stackchan::State;

static constexpr uint32_t kReplyDelayMs = 3000;
static constexpr uint32_t kFaceIntervalMs = 100;   // synthetic camera period
static constexpr uint32_t kServoIntervalMs = 100;
static constexpr int kYawMax = 150;                // 10 = 1 deg, so +-15 deg
static constexpr int kServoSpeed = 300;

static stackchan::Interaction core;
static bool armed = false;
static uint32_t lastFaceMs = 0, lastServoMs = 0, lastDrawMs = 0;
static uint64_t pendingId = 0;
static uint32_t replyDueMs = 0, answerStartMs = 0;
static constexpr uint32_t kAnswerShowMs = 2500;
static uint32_t accepted = 0, rejected = 0;
static const char* lastEvent = "armed: no";

static const char* stateName(State s) {
  switch (s) {
    case State::idle: return "idle";
    case State::waiting: return "waiting";
    case State::answering: return "answering";
    case State::cancelled: return "cancelled";
    case State::error: return "error";
  }
  return "?";
}

static uint16_t faceColor(Face f) {
  switch (f) {
    case Face::thinking: return TFT_CYAN;
    case Face::happy: return TFT_YELLOW;
    case Face::answer: return TFT_GREEN;
    case Face::cancelled: return TFT_ORANGE;
    case Face::error: return TFT_RED;
    default: return TFT_WHITE;
  }
}

static void draw(const stackchan::Output& out) {
  auto& d = M5StackChan.Display();
  d.fillScreen(TFT_BLACK);
  d.fillCircle(d.width() / 2, 70, 45, faceColor(out.face));
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setTextSize(2);
  d.setCursor(8, 130);
  d.printf("state: %s\n", stateName(out.state));
  d.printf("track: %s x=%.2f\n", out.track_head ? "on" : "off", out.normalized_x);
  d.printf("ok:%lu rej:%lu\n", (unsigned long)accepted, (unsigned long)rejected);
  d.printf("%s\n", lastEvent);
}

static void log(const char* what, uint32_t now) {
  lastEvent = what;
  Serial.printf("device_monotonic_ms=%lu event=%s\n", (unsigned long)now, what);
}

void setup() {
  M5StackChan.begin();
  Serial.begin(115200);
  draw(core.output(millis()));
}

void loop() {
  M5StackChan.update();
  const uint32_t now = millis();

  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) {
    const int third = t.x * 3 / M5StackChan.Display().width();
    if (!armed) {
      armed = true;
      log("armed", now);
    } else if (third == 0) {
      pendingId = core.start(now);
      replyDueMs = now + kReplyDelayMs;
      log("start", now);
    } else if (third == 1) {
      log(core.cancel(now) ? "cancel ok" : "cancel ignored", now);
    } else {
      core.touch(now);
      log("touch", now);
    }
  }

  // Simulated reply for the most recent start. Only a live request is accepted.
  if (pendingId != 0 && (int32_t)(now - replyDueMs) >= 0) {
    if (core.reply(pendingId, now)) {
      ++accepted;
      answerStartMs = now;
      log("reply accepted", now);
    } else {
      ++rejected;
      log("reply rejected", now);
    }
    pendingId = 0;
  }

  // Synthetic face observation; captured == now because there is no camera latency here.
  if (armed && now - lastFaceMs >= kFaceIntervalMs) {
    lastFaceMs = now;
    core.observe(0.8F * sinf(now / 2500.0F), true, now, now);
  }

  const auto out = core.output(now);
  if (out.state == State::answering && now - answerStartMs > kAnswerShowMs) core.finish(now);

  if (armed && now - lastServoMs >= kServoIntervalMs) {
    lastServoMs = now;
    const int yaw = out.track_head ? (int)(out.normalized_x * kYawMax) : 0;
    M5StackChan.Motion.moveX(yaw, kServoSpeed);
  }

  if (now - lastDrawMs >= 100) {
    lastDrawMs = now;
    draw(out);
  }
  delay(10);
}
