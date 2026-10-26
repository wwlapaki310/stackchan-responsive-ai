# Responsive While Reasoning: Coordinating On-Device Perception and Cloud Generative AI on a Microcontroller Social Robot

**Working manuscript, 2026-10-26. Proposed system and evaluation protocol. Hardware integration and evaluation are pending. No empirical results are claimed.**

Target under consideration: Frontiers in Robotics and AI, Technology and Code. Final venue, format, license and publication budget remain undecided. Author details are intentionally omitted in this working copy.

## Abstract — provisional, without results

Cloud generative AI can provide open-ended responses to small social robots, but a delayed answer need not imply that the robot stops responding to its immediate environment. A microcontroller robot has insufficient resources to reproduce many computationally intensive dialogue and motion-generation architectures. This manuscript proposes a resource-bounded architecture that separates cloud response generation from on-device perception and coordinates their access to a shared expressive body. The intended realization uses a Stack-chan K151, with lightweight face detection, touch events, screen expressions and two-axis head motion. The architecture associates cloud results with request identities, rejects obsolete results, expires stale observations and arbitrates head and display behavior independently. We specify controlled replay and real-API experiments that distinguish local event responsiveness from cloud answer latency, and measure handoff delay, tracking performance and resource use. The present artifact contains a portable policy prototype; the hardware implementation and planned evaluation remain incomplete. Perceived waiting time and social acceptance are outside the claims of the technical evaluation.

## 1. Introduction

A small embodied assistant must handle two different kinds of work. Answering an open-ended question may require a remote generative model. Turning toward a nearby person or acknowledging a touch concerns the current physical situation and should not depend on completion of that remote computation. When these activities are coupled, a robot may appear inactive while waiting for a reply, or may execute a response after the interaction that motivated it has already changed.

The design problem is particularly concrete on a microcontroller robot. Perception buffers, communication, display rendering and actuator updates share a limited memory and processing budget. A separate host can simplify integration, but it also changes the deployment conditions. We focus on a robot that communicates directly with a cloud API while retaining perception and behavior coordination on the device. This is a deployment constraint rather than a claim that any specific middleware is universally impossible on all microcontrollers.

Earlier work examines conversational fillers and response timing [R1]. More recent work studies latency feedback for LLM-powered virtual agents [R2], contextual prefatory responses on a physical robot [R3], local-to-cloud language continuation [R4], and full-duplex coordination of dialogue and physical motion [R5]. These works motivate the present problem; they also preclude a broad novelty claim based merely on fillers, two processing timescales, or edge/cloud cooperation.

Our narrower question is: **under a measured MCU resource budget, how can a robot preserve sensor-contingent bodily responses during cloud generation while controlling conflicts and the transition to a valid answer?** The proposed study separates three outcomes: responsiveness to new local events, the validity of actions triggered by delayed results, and the extra delay introduced by behavior handoff. A robot can improve one while worsening another, so average end-to-end answer time alone is inadequate.

The intended contributions are (i) a small, inspectable coordination architecture for MCU embodied interaction; (ii) a comparative evaluation against a competent concurrent baseline, including resource and failure conditions; and (iii) software and replay artifacts enabling replication. Whether these contributions constitute sufficient novelty must be established through further literature review and empirical evidence.

## 2. Related Work

### 2.1 Waiting feedback and response timing

Shiwa et al. studied response time and conversational fillers in communication robots [R1]. Maslych et al. examined LLM-powered virtual agents in VR and compared natural conversational fillers with artificial waiting feedback [R2]. Their reported perceptual findings concern the evaluated virtual-agent interactions; they do not establish that the present physical implementation will reduce perceived waiting. Our initial study instead measures whether environmental responses remain available during waiting.

Okafuji et al. propose a two-stage framework for contextual prefatory responses and report a timing tradeoff between initial response and the gap before the main response [R3]. This makes it important to report initial feedback and substantive answer availability separately. A quick acknowledgment must not be reported as faster answer generation.

### 2.2 Edge/cloud cooperation and embodied execution

Cheng et al. investigate micro language models that generate response openers followed by cloud continuation [R4]. Their embedded evaluation uses Orange Pi; our intended device performs lightweight perception rather than local language generation. This hardware distinction alone does not establish a new algorithm. The present evaluation must show what a different decomposition enables within a specified MCU budget.

MIRA integrates turn arbitration, interruption and streaming embodied motion on an Astribot S1 robot [R5]. It is a close architectural comparator, and generic claims about separating fast and slow processes would overlap. We focus on a bounded MCU implementation and explicit event-to-body and answer-handoff measurements. Detailed computational requirements and reproducibility of comparable execution policies require further full-text/code inspection.

A microcontroller/cloud conversational assistant is also reported in [R6]. Only its abstract has been reviewed so far. Before submission, its hardware, local processing, measurements and behavior handling must be checked; no claim that it lacks a feature is made here.

### 2.3 Reproducible HRI infrastructure

RISE addresses accessible and reproducible HRI scenario implementation [R7]. Publishing code is therefore an artifact contribution rather than sufficient conceptual novelty. Our package should include scenario definitions, timing semantics, exact dependency versions and analysis, not only a demonstration video. Technical replication of behavior must be distinguished from replication of human experience.

## 3. Problem Formulation and Design Requirements

The intended platform is a K151 with an ESP32-S3, camera, display, touch input and head actuators [R8]. Actual usable memory, firmware versions and measured processing capacity will be reported after integration. No local language model is required. Cloud requests contain selected textual questions; raw camera frames remain on the device in the initial design.

Inputs consist of timestamped face observations, touches, request starts/cancellations, cloud results/failures and timer ticks. A face observation contains capture time, face presence and normalized horizontal position; confidence gating and multi-face selection belong to the perception adapter. Outputs are desired head targets and display states. These desired outputs are not proof of physical actuation.

Requirements are: local processing continues during network waiting; invalidated requests cannot publish answers; old observations cannot indefinitely drive motion; display overlays do not unnecessarily block head tracking; and timeout is distinguishable from successful reasoning. Face loss does not itself cancel a question. Mechanical limits and speed constraints are mandatory responsibilities of the device adapter.

## 4. Proposed Architecture

### 4.1 Portable policy and device adapters

The portable policy has a single owner. Perception, network and touch tasks deliver events through bounded queues; they do not concurrently mutate policy state. The policy produces desired outputs, while device adapters render expressions and execute calibrated motion. The current C++17 prototype implements request lifecycle, freshness checks and independent display/head decisions. It does not yet implement queues, task scheduling, camera inference, network I/O, motion trajectories or resource adaptation.

This distinction keeps host tests useful without confusing them with MCU validation. The hardware SDK path is unresolved: the official firmware uses ESP-IDF, whereas the separate StackChan-BSP is Arduino-oriented [R8, R9]. Integration must pin a supported combination rather than assuming interchangeable build systems.

### 4.2 Request lifecycle and observation validity

Each request receives a device-assigned identity. Starting a new request supersedes the previous identity. Replies are accepted only for the active waiting request before timeout; duplicate, cancelled and obsolete replies are rejected. The network layer must still release buffers and close completed requests even when their results are discarded.

Face observations expire according to capture time, not inference completion time. Future-dated or out-of-order observations are rejected. A configured freshness threshold is an experimental parameter, not a universal HRI constant. All timestamps use one boot-local monotonic clock. Wall-clock timestamps are retained only for experiment metadata.

### 4.3 Body arbitration and handoff

Head and display are separate resources. In the initial policy, a fresh face observation can drive the head while the display indicates waiting or briefly acknowledges touch. Answer display preempts a touch overlay. Later motion integration will define interruptible boundaries and a maximum commitment duration for gestures; these mechanisms are planned and not yet implemented in the portable prototype.

A touch acknowledgment expresses receipt of touch, not comprehension of the question. Similarly, an API result is not evidence of correctness. Keeping these semantics distinct avoids defining successful interaction through an unverified internal state.

### 4.4 Resource management

The implementation will bound observation queues, response buffers and gesture state. Initial measurements will use a fixed perception frequency; adaptive scheduling will be added only if a concrete resource conflict warrants it. Any adaptive version must report recognition/tracking degradation alongside latency improvements. NuttX is a possible later implementation, not a prerequisite or an assumed performance improvement.

## 5. Planned Evaluation

### 5.1 Research questions

RQ1: Does resource-aware body coordination preserve local event responsiveness during cloud waiting relative to fixed feedback and a competent concurrent implementation?

RQ2: What additional answer-handoff delay and tracking cost does coordination introduce?

RQ3: Across network delay and device-load conditions, where does the approach fail or cease to fit the resource budget?

### 5.2 Comparators and ablations

A uses fixed waiting feedback. B runs perception and networking concurrently with simple priorities and standard request validation. C adds the proposed resource-specific arbitration and bounded handoff. B must not omit standard correctness protections merely to favor C. Freshness thresholds, model, input traces and hardware settings are matched where appropriate. Ablations isolate display/head separation, handoff rules and any later resource adaptation. If B performs equivalently, the contribution must be narrowed rather than inventing an advantage.

### 5.3 Controlled and live trials

A mock endpoint will return recorded answers under controlled delays, jitter, failures and reordered completions. Proposed delay levels are 0.5, 2, 5 and 10 seconds; final values and trial counts will be frozen after feasibility pilots and before confirmatory comparison. Events include face movement/loss, touch, cancellation and no event. Method order will be randomized, with paired replay traces and independent execution runs. Actual API trials will subsequently check deployment validity and record model ID/version, settings, request date, token counts and any rate-limit failures.

A replayed face event evaluates the coordinator, not the detector. Separate camera-to-actuation trials are needed for the full pipeline. No benchmark server will control the robot during the standalone demonstration.

### 5.4 Measurements and analysis

Log capture, perception completion, event enqueue/dequeue, policy decision, actuator command, physical onset, request submission, first token, validated-answer-ready and answer presentation separately. Record run ID, firmware commit, configuration and clock domain. Physical motion requires feedback or externally synchronized video; screen presentation is not equivalent to setting a state variable.

Report latency distributions, per-run quantiles, tracking error, missed events, obsolete-answer publication, memory high-water marks and sustained perception rate. Report confidence intervals with resampling at the independent-run level, rather than treating correlated events as independent participants. Include failed runs and all timeout outcomes. Measured maxima are not worst-case guarantees. Energy measurements are optional and require an external measurement method with an idle baseline.

### 5.5 Reproducibility and claim scope

Release sanitized traces, configuration, model/dependency identifiers, policy code and analysis. Never publish credentials or private sensor streams. No human study is planned for the first technical evaluation. Accordingly, the manuscript will not infer reduced perceived waiting, naturalness, trust, acceptance or engagement from technical latency. Any later human study requires a separate protocol and appropriate ethical handling before data collection.

## 6. Results

**Reserved for measured results. No values or favorable direction assumed.**

Planned figures: system timeline; local responsiveness by delay/load; handoff latency versus tracking quality; resource envelope; failure/ablation table. Host test pass/fail establishes only policy correctness under tested scenarios and is not a substitute for these measurements.

## 7. Discussion Framework

The final discussion will distinguish computational responsiveness from socially appropriate timing, report resource tradeoffs and explain what can generalize beyond K151. Two-axis motion and screen output limit the range of expressive behaviors. The study will not establish that users interpret expressions as intended. A second MCU implementation or independent replication would strengthen generalizability. Cloud model/service changes may alter deployment results even when controlled replay is stable.

## 8. Conclusion

**To be written after evaluation.** The final conclusion must be restricted to observed effects and conditions; the present draft establishes the question, proposed implementation and planned evidence.

## References and verification status

- [R1] Shiwa et al. How Quickly Should Communication Robots Respond? Journal of the Robotics Society of Japan, 27(1), 87–95, 2009. https://doi.org/10.7210/jrsj.27.87 (publisher abstract checked).
- [R2] Maslych et al. Mitigating Response Delays in Free-Form Conversations with LLM-powered Intelligent Virtual Agents. CUI 2025. https://doi.org/10.1145/3719160.3736636 (publisher text checked).
- [R3] Okafuji, Inoue and Ohira. Low-Latency Turn-Taking via Context-Aware Preface Generation in a Real-World Dialogue Robot. 2026. https://arxiv.org/abs/2607.23204 (author abstract checked; lists ICMI LBR acceptance).
- [R4] Cheng et al. Micro Language Models Enable Instant Responses. 2026. https://arxiv.org/abs/2604.19642 (preprint; HTML checked, including Orange Pi evaluation).
- [R5] Lin et al. MIRA: Real-Time Full-Duplex Human-Robot Interaction for Embodied Companions. 2026. https://arxiv.org/abs/2609.24547 (preprint; HTML checked).
- [R6] Conversational Smart Assistant on a Microcontroller with Cloud-Based LLM Function Offloading. https://ieeexplore.ieee.org/abstract/document/11365064 (abstract only; authors, venue and full methods must be verified).
- [R7] Groß et al. RISE: an open-source architecture for interdisciplinary and reproducible human–robot interaction research. Frontiers in Robotics and AI, 2023. https://doi.org/10.3389/frobt.2023.1245501 (publisher full text checked).
- [R8] M5Stack StackChan documentation and firmware. https://docs.m5stack.com/en/StackChan/ and https://github.com/m5stack/StackChan (version pin pending).
- [R9] M5Stack StackChan-BSP. https://github.com/m5stack/StackChan-BSP (Arduino orientation checked; exact dependency compatibility pending).

## Pre-submission work

Verify R6 full text and MCU competitors; convert references to the venue bibliography format; replace planned implementation with verified methods; add dependency/board/model identifiers; finalize trial design; document data availability, funding and generative-AI assistance according to the venue's current policies. This draft and initial core were developed with AI assistance and require author review.
