#include "director.hpp"
#include "interaction.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace stackchan;
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
int main() {
  try {
    Interaction a;
    const auto first = a.start(100);
    require(a.observe(.5F, true, 150, 160), "fresh observation accepted");
    a.touch(170);
    auto view = a.output(180);
    require(view.state == State::waiting && view.face == Face::happy &&
            view.track_head && view.normalized_x == .5F,
            "touch overlay must coexist with pending cloud and tracking");
    require(a.output(570).face == Face::thinking, "touch overlay expires");
    require(!a.output(651).track_head, "old observation must expire");
    require(a.cancel(700), "pending request can be cancelled");
    const auto second = a.start(710);
    require(second > first && !a.reply(first, 720), "reject superseded response");
    a.touch(730);
    require(a.reply(second, 740), "accept current response");
    require(a.output(740).face == Face::answer, "answer preempts touch overlay");
    require(!a.reply(second, 750), "duplicate response rejected");
    a.finish(760);
    require(a.output(760).state == State::idle, "finish returns to idle");

    Interaction b({100, 50, 20});
    const auto timeout_id = b.start(0);
    require(!b.reply(timeout_id, 100), "reply at timeout boundary rejected");
    require(b.output(100).state == State::error, "timeout is explicit error");
    const auto valid = b.start(110);
    require(!b.fail(timeout_id, 111), "old network failure cannot affect new request");
    require(b.observe(2, true, 120, 120), "out-of-range coordinate is clamped");
    require(b.output(120).normalized_x == 1, "clamped target");
    require(!b.observe(0, true, 119, 121), "older observation rejected");
    require(!b.observe(0, true, 130, 122), "future observation rejected");
    require(!b.observe(std::numeric_limits<float>::quiet_NaN(), true, 122, 122),
            "invalid coordinate rejected");
    require(!b.reply(valid, 119), "regressing event clock rejected");
    require(b.observe(0, false, 123, 123) && !b.output(123).track_head,
            "explicit face loss stops tracking but preserves request");
    require(b.output(124).state == State::waiting, "face loss is not cancellation");
    require(b.fail(valid, 125), "current failure accepted");
    require(!b.reply(valid, 126), "reply after failure rejected");

    // Autonomous behaviour policy (director.hpp)
    Director d;
    require(d.step(0, false, State::idle, true) == Act::none, "director starts quiet");
    require(d.step(9000, true, State::idle, true) == Act::greet, "greet when a face appears after a gap");
    require(d.step(10000, true, State::idle, true) == Act::none, "no ask before the face is stable");
    require(d.step(12000, true, State::idle, true) == Act::ask, "ask once the face is stable");
    require(d.step(12100, true, State::idle, true) == Act::none, "ask is not repeated");
    require(d.step(13000, true, State::waiting, true) == Act::none, "no filler right after the request starts");
    require(d.step(14600, true, State::waiting, true) == Act::filler && d.fillers_said() == 1, "first filler");
    require(d.step(16000, true, State::waiting, true) == Act::none, "fillers are spaced");
    require(d.step(17600, true, State::waiting, true) == Act::filler && d.fillers_said() == 2, "second filler");
    require(d.step(30000, true, State::waiting, true) == Act::none, "filler count is capped");
    require(d.step(31000, true, State::idle, true) == Act::none, "ask cooldown holds");
    require(d.step(43000, true, State::idle, true) == Act::ask, "ask again after the cooldown");
    require(d.step(42000, true, State::idle, true) == Act::none, "regressing clock ignored");
    d.step(44000, false, State::idle, true);
    require(d.step(46000, true, State::idle, true) == Act::none, "brief loss does not re-greet");
    require(d.step(100000, false, State::idle, true) == Act::none, "just lost");
    require(d.step(146000, false, State::idle, true) == Act::lonely, "call out after a long absence");
    require(d.step(150000, false, State::idle, true) == Act::none, "lonely cooldown");
    Director off;
    off.step(0, false, State::idle, false);
    require(off.step(9000, true, State::idle, false) == Act::none && off.step(20000, true, State::idle, false) == Act::none,
            "disabled director never acts");
    require(off.step(30000, true, State::idle, true) == Act::ask, "enabling later acts on current state");
    std::cout << "PASS: cancellation, supersession, timeout, tracking freshness, "
                 "clock validation, independent display/head policy and the autonomous director policy\n";
    std::cout << "HOST POLICY TEST ONLY: no physical latency or MCU validation\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
