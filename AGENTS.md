# Development agreement

Read HANDOFF.md and README.md first. Update HANDOFF.md before switching PCs.
The GitHub repository is the shared source of truth. Push committed work before leaving a PC; fetch/pull before resuming elsewhere. Never force push or discard another PC's work.

## Scope

- Initial deliverable: K151 screen question, direct cloud request, on-device face detection, head tracking, touch feedback, answer display and cancellation.
- Keep portable coordination code independent of ESP-IDF, Arduino, providers and hardware.
- Do not introduce ROS, speech/TTS, NuttX or multiple providers into the MVP without revisiting scope.
- The official StackChan-BSP is Arduino-oriented; do not assume it is a drop-in native ESP-IDF component. Hardware adapter/toolchain choice remains open.

## Verification

Run `g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Iinclude tests/host.cpp -o /tmp/stackchan-host` then `/tmp/stackchan-host` (use an appropriate temporary path on Windows).
Host tests validate policy semantics only. No MCU timing, AI inference, physical motion or API access is proven by them.
Never claim a feature is hardware-tested without its board, revision, command and evidence.

## Research and data

Keep planned and implemented methods distinct. No invented measurements, citations or novelty claims. The manuscript is a working draft, not a completed study.
Do not commit API keys, Wi-Fi credentials, raw images, private conversations, device dumps or course PDFs. Use synthetic fixtures and sanitized logs.
Preserve third-party licenses. Choose a project license before public release.
