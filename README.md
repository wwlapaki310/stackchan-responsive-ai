# Stack-chan Responsive AI

**考えていても、そばにいるStack-chan**

クラウド生成AIの回答待ち中も、本体の軽量AIで人を認識し、首・表情・タッチ反応を継続する小型ロボットの研究開発。

> Status: planning / repository scaffold. Firmware and hardware behavior are not implemented or validated yet.

## Goals

- 実行時はStack-chan K151 + Wi-Fi + cloud APIで完結（PCは開発・計測用）。
- 最終課題: 質問選択 → API要求 → 顔追従・タッチ反応 → 回答表示。
- 研究: MCU上の知覚・通信・身体表現の協調を比較評価する。
- 新規性は未確定。自然さ・知覚待ち時間の改善はユーザー実験なしで主張しない。

## Decisions

- Initial runtime: ESP-IDF / FreeRTOS, candidate StackChan-BSP integration.
- Cloud: Sarashina API first; exact endpoint, model version and limits must be verified before integration.
- On-device AI: lightweight face detection; touch and arbitration are rule-based.
- Initial input/output: screen-selected questions and screen-displayed answers.
- Speech input, TTS, NuttX port and multi-provider comparison are stretch goals.
- API keys, Wi-Fi credentials, raw camera data and personal information must not be committed.
- License selection is pending; no third-party code has been copied.


## 最終課題のMVP

Deadline: 2026-11-02 10:00 JST. Submission rules must be checked against the course's current instructions.

## Acceptance criteria

- [ ] Existing firmware backup/recovery procedure documented.
- [ ] Display, head motion and touch work from a minimal custom program.
- [ ] Camera frames reach an on-device face detector.
- [ ] Screen-selected question reaches the cloud API via certificate-verified HTTPS.
- [ ] While waiting, face tracking and touch feedback remain operational.
- [ ] A valid answer replaces waiting behavior and is displayed.
- [ ] Cancel invalidates the active request; late results cannot replace a newer answer.
- [ ] Timeouts and malformed responses produce an explicit recoverable error state.
- [ ] Serial logs record event timestamps without credentials or raw images.
- [ ] 1–3 minute video, explanation PDF and public code repository prepared.

## Demo

Question -> waiting -> person moves -> robot tracks -> touch -> immediate expression -> answer arrives -> transition.
Include a brief comparison against fixed waiting behavior, with measured values only.

## Scope

Required: camera AI, head tracking, touch, cloud request, screen answer, cancellation and logs.
Optional: speech, TTS, provider switching, NuttX.
No firmware behavior has been verified on K151 yet. Investigate BSP and camera/model compatibility first.


---

## Architecture

## Components

Perception publishes face presence, normalized position, confidence and capture timestamp.
Cloud client publishes result/error with request_id.
Interaction state machine owns active request and answer validity.
Arbiter chooses head and display actions independently.
Hardware adapters enforce calibrated mechanical limits.
Logger uses monotonic device timestamps.

## States

IDLE -> WAITING -> ANSWERING -> IDLE.
WAITING -> CANCELLED / ERROR.
Touch feedback is a temporary overlay, not a replacement for request state.

## Arbitration

Head: mechanical limits > valid face tracking > answer gesture > idle motion.
Display: error/cancel > answer > bounded touch feedback > waiting > idle.
Exact priorities are hypotheses to validate, not established best practices.
A gesture must declare its interruptible boundary and maximum duration.

## Cloud contract

Initial integration may accept plain text; optional structured output:
`{"text":"...", "expression":"neutral", "gesture":"nod"}`.
Validate lengths and enum values. Reject unknown motion commands.
The device owns request IDs and cancellation; never rely on model-generated IDs.
A late response may only be displayed if its request is still active.
Face loss alone must not silently cancel a request: use an explicit, documented policy.

## Configuration

Use local ignored credentials or device provisioning. Verify TLS certificates.
Bound queues, input buffers, answer length and inference buffers.
Do not call cloud APIs from control loops.
No host proxy is allowed in the standalone demo; a mock server is permitted only for experiments.


---

## Research plan (provisional)

## Question

Can an MCU social robot maintain sensor-contingent physical responses while awaiting cloud generative AI, without excessive answer handoff latency or resource exhaustion?

## Contribution candidates

1. Resource-bounded on-device perception + cloud dialogue architecture.
2. Per-actuator arbitration and interruptible handoff.
3. Request validity and stale-result handling.
4. Reproducible implementation and benchmark artifacts.

These are candidates, not established novelty claims. Generic multitasking, fillers and request cancellation are prior art.

## Baselines

A: fixed waiting animation.
B: competent concurrent perception/network/control with simple priorities and standard request validation.
C: proposed arbitration/handoff and resource policy.
Do not weaken B artificially. If B already solves a problem, report it and narrow the contribution.
Ablate each proposed mechanism separately. Use identical models, inputs and limits.

## Measurements

- Event-to-command and event-to-physical-motion latency: median, p95, p99.
- Answer-ready-to-display latency, separate from API time-to-first-token.
- Tracking error and reacquisition.
- Event loss, motion conflicts, stale-response errors.
- Heap high-water use, task stack use, inference rate and long-run stability.
- Physical onset verified via servo feedback or video, not command timestamps alone.

## Trials

Start with controlled delay replay (0.5/2/5/10 seconds), events (movement/touch/cancel/none), and varying inference/display load.
Use identical traces across methods, randomize execution order, record seeds/build/model/config.
Then validate with real API calls; log provider/model/date and token counts.
Repeated runs are system trials, not independent human participants.
Report distributions and uncertainty, failures and tradeoffs; measured maxima are not hard real-time guarantees.

## Claim boundary

Without a user study, claim technical responsiveness and operational continuity only.
Do not claim reduced perceived waiting, naturalness, trust or engagement.

## Publication

Working target: Frontiers in Robotics and AI, Technology and Code.
RA-L if a sufficiently novel robotics method and strong comparative evidence emerge.
THRI if later work establishes a clear HRI contribution.
Check current author rules, fees, ethics, prior publication and anonymization before submitting.
NuttX is an optional second implementation; isolate OS effects from algorithm effects.


---

## Related work — initial reading list

This is an initial scoping list, not a systematic review. Publication status and detailed overlap must be checked.

- Maslych et al. (2025), Mitigating Response Delays in Free-Form Conversations with LLM-powered Intelligent Virtual Agents. CUI. https://doi.org/10.1145/3719160.3736636
  LLM waiting feedback in VR; read for filler baselines and limits of perceptual claims.
- Okafuji et al. (2026), Low-Latency Turn-Taking via Context-Aware Preface Generation in a Real-World Dialogue Robot. Author page lists ICMI LBR acceptance. https://arxiv.org/abs/2607.23204
  Read for timing and prefatory response tradeoffs.
- Cheng et al. (2026), Micro Language Models Enable Instant Responses. Preprint. https://arxiv.org/abs/2604.19642
  Local language opener + cloud continuation; embedded evaluation on Orange Pi differs from an MCU.
- Lin et al. (2026), MIRA: Real-Time Full-Duplex Human-Robot Interaction for Embodied Companions. Preprint. https://arxiv.org/abs/2609.24547
  Close architectural overlap: turn arbitration, interruptible motion and embodied response.
- Conversational Smart Assistant on a Microcontroller with Cloud-Based LLM Function Offloading. https://ieeexplore.ieee.org/abstract/document/11365064
  Abstract reviewed only. Full text needed before claiming differences.
- Groß et al. (2023), RISE. https://doi.org/10.3389/frobt.2023.1245501
  Reproducible HRI software architecture.

## Remaining review tasks

- Find MCU implementations combining perception, expressive behavior and cloud LLM.
- Inspect available Stack-chan firmware for existing concurrent tracking and waiting behavior.
- Review behavior arbitration, asynchronous cancellation and edge/cloud runtime literature.
- Build an evidence-backed overlap matrix before asserting novelty.


---

## Implementation backlog

## First vertical slice

1. Inspect official StackChan firmware/BSP licenses, build instructions and K151 compatibility. Pin revisions.
2. Document factory firmware backup, recovery and mechanical limits.
3. Build a minimal display + touch + servo example on hardware.
4. Add monotonic timestamp logging.
5. Verify camera/model compatibility and memory budget; measure face detection.
6. Implement interaction state and fake asynchronous cloud response.
7. Test cancellation followed by a newer request and out-of-order responses.
8. Add direct HTTPS Sarashina request and bounded response parsing.
9. Add actuator arbitration and measure against a concurrent baseline.
10. Capture demo and course deliverables; prepare public release.

## Research follow-up

- Controlled replay server and recorded scenarios.
- Analysis scripts and parameterized experiment runner.
- Strong baseline and mechanism ablations.
- Target-journal requirements and additional literature review.
- Optional NuttX feasibility/porting investigation.

## Completion policy

Mark work done only after recording evidence. Hardware-only claims require hardware validation.
