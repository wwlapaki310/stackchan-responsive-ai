#include "interaction.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
using namespace stackchan;
const char* state_name(State s) {
  switch (s) {
    case State::idle: return "idle";
    case State::waiting: return "waiting";
    case State::answering: return "answering";
    case State::cancelled: return "cancelled";
    case State::error: return "error";
  }
  return "invalid";
}
const char* face_name(Face f) {
  switch (f) {
    case Face::neutral: return "neutral";
    case Face::thinking: return "thinking";
    case Face::happy: return "happy";
    case Face::answer: return "answer";
    case Face::cancelled: return "cancelled";
    case Face::error: return "error";
  }
  return "invalid";
}
// Host-only transport: event time [arguments]. One JSON record per input event.
// A hardware adapter must supply its own clock, event queues and physical I/O.
int main() {
  Interaction policy;
  std::uint64_t active = 0, previous = 0;
  std::string line;
  unsigned line_number = 0;
  while (std::getline(std::cin, line)) {
    ++line_number;
    if (line.empty() || line[0] == '#') continue;
    std::istringstream input(line);
    std::string event, extra;
    std::uint64_t now = 0, id = 0, capture = 0;
    bool accepted = true;
    float x = 0;
    int present = 0;
    if (!(input >> event >> now) || now < previous) {
      std::cerr << "Invalid/regressing timestamp on line " << line_number << '\n';
      return 2;
    }
    if (event == "face") {
      if (!(input >> capture >> x >> present) || (present != 0 && present != 1))
        return 2;
    } else if (event == "reply" || event == "fail") {
      if (!(input >> id)) return 2;
    } else if (event != "start" && event != "touch" && event != "cancel" &&
               event != "finish" && event != "tick") {
      std::cerr << "Unknown event on line " << line_number << '\n';
      return 2;
    }
    if (input >> extra) return 2;
    previous = now;
    if (event == "start") { active = policy.start(now); accepted = active != 0; }
    else if (event == "touch") policy.touch(now);
    else if (event == "cancel") accepted = policy.cancel(now);
    else if (event == "finish") policy.finish(now);
    else if (event == "face") accepted = policy.observe(x, present != 0, capture, now);
    else if (event == "reply") accepted = policy.reply(id, now);
    else if (event == "fail") accepted = policy.fail(id, now);
    const auto output = policy.output(now);
    std::cout << std::boolalpha << std::setprecision(7)
      << "{\"schema_version\":1,\"clock_domain\":\"synthetic_ms\","
      << "\"source\":\"host_policy_replay\",\"t_ms\":" << now
      << ",\"event\":\"" << event << "\",\"request_id\":" << active
      << ",\"event_request_id\":" << id
      << ",\"accepted\":" << accepted
      << ",\"state\":\"" << state_name(output.state)
      << "\",\"face\":\"" << face_name(output.face)
      << "\",\"track_head\":" << output.track_head
      << ",\"normalized_x\":" << output.normalized_x << "}\n";
  }
  return 0;
}
