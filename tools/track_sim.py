#!/usr/bin/env python3
"""Closed-loop simulation of the Stack-chan head-tracking law (yaw only).

SIMULATION ONLY. It reimplements the control laws of firmware/answer_demo in Python and
runs them against a simple plant. It does not measure the real robot. Parameters marked
ASSUMED were not measured on the device; results are sensitivity analyses over them.

Model
  - A person stands at world angle `face(t)` (deg); the camera is on the head at `head(t)`.
  - Normalised image x = (face - head) / (FOV / 2); the face is detectable only if |x| < 1.
  - Camera frames at FRAME_HZ (measured ~16 fps on the device). Each frame the detector finds
    the face with probability p (measured 0.04-0.5 on the device, uncontrolled conditions),
    with Gaussian noise on x.
  - Controller runs exactly as in the sketch: detections are smoothed and used per detection;
    the servo command is evaluated every 100 ms and sent when it changed enough.
  - Servo: slew-rate limited towards the commanded angle (ASSUMED speed, swept).

Controllers
  prop   firmware before commit d37353a: yaw = x * 15 deg while a fresh detection exists,
         hold when lost, return to 0 after 3 s.
  prop45 same law with the range widened to +-45 deg (separates the law from the range limit).
  int-G  current firmware: yawTarget += int(smoothX * G) (units of 0.1 deg) per detection when
         |smoothX| > 0.08, clipped to +-45 deg; hold 3 s after loss, then 0. G = 40, 80, 160.

Usage: python tools/track_sim.py [--runs 100] [--out docs/sim]
"""
import argparse
import csv
import math
import random
from pathlib import Path

FRAME_HZ = 16.0
DT = 1.0 / FRAME_HZ / 5          # 5 sub-steps per camera frame
SERVO_TICK = 0.1                 # s, kServoIntervalMs
HOLD = 3.0                       # s, kHoldMs
TTL = 0.5                        # s, core observation TTL
DEADBAND = 0.08
NOISE = 0.03                     # ASSUMED std of detected x
T_END = 30.0
CONTROLLERS = ["prop", "prop45", "int-40", "int-80", "int-160"]
P_DETECT = [0.05, 0.1, 0.25, 0.5, 0.9]
SCENARIOS = ["step", "walk", "reappear"]


def face_angle(scn, t):
    """World angle of the person, or None when absent."""
    if scn == "step":
        return 25.0 if t >= 2.0 else None
    if scn == "walk":
        return 30.0 * math.sin(2 * math.pi * t / 20.0)
    if scn == "reappear":
        if t < 8.0:
            return -20.0
        if t < 14.0:
            return None
        return 20.0
    raise ValueError(scn)


def run(ctrl, scn, p, fov, vmax, rng):
    head = 0.0           # deg, true head angle
    cmd = 0               # last command sent, units of 0.1 deg
    target = 0            # integral state, units of 0.1 deg
    smooth = None
    last_det = -1e9       # time of last detection
    last_track = -1e9     # last time the core reported tracking
    errs, centred_at, in_view = [], None, 0
    n_steps = int(T_END / DT)
    t_event = {"step": 2.0, "reappear": 14.0}.get(scn)
    next_tick = SERVO_TICK
    for i in range(n_steps):
        t = i * DT
        fa = face_angle(scn, t)
        # camera frame every 5 sub-steps; the image is taken with the current head angle
        if i % 5 == 0 and fa is not None:
            x = (fa - head) / (fov / 2.0)
            if abs(x) < 1.0 and rng.random() < p:
                x = max(-1.0, min(1.0, x + rng.gauss(0.0, NOISE)))
                last_det = t
                smooth = x if smooth is None or t - last_track > TTL else 0.5 * smooth + 0.5 * x
                if ctrl.startswith("int"):
                    if abs(smooth) > DEADBAND:
                        g = float(ctrl.split("-")[1])
                        target = max(-450, min(450, target + int(smooth * g)))
        tracking = t - last_det <= TTL
        if tracking:
            last_track = t
        if t >= next_tick:
            next_tick += SERVO_TICK
            if ctrl.startswith("prop"):
                want = cmd
                if tracking:
                    want = int(smooth * (150 if ctrl == "prop" else 450))
                elif t - last_track > HOLD:
                    want = 0
                if abs(want - cmd) >= 10:
                    cmd = want
            else:
                if not tracking and t - last_track > HOLD:
                    target = 0
                if abs(target - cmd) >= 5:
                    cmd = target
        goal = cmd / 10.0
        step = vmax * DT
        head += max(-step, min(step, goal - head))
        if fa is not None:
            err = fa - head
            errs.append(err)
            if abs(err) < abs(fov / 2.0):
                in_view += 1
            if t_event is not None and t >= t_event and centred_at is None and abs(err) <= 3.0:
                centred_at = t - t_event
    if not errs:
        return None
    rms = math.sqrt(sum(e * e for e in errs) / len(errs))
    return {"rms": rms, "view": in_view / len(errs), "t_centre": centred_at}


def quantile(xs, q):
    xs = sorted(xs)
    if not xs:
        return float("nan")
    k = (len(xs) - 1) * q
    lo, hi = int(math.floor(k)), int(math.ceil(k))
    return xs[lo] + (xs[hi] - xs[lo]) * (k - lo)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=100)
    ap.add_argument("--out", default="docs/sim")
    ap.add_argument("--fov", type=float, default=60.0, help="ASSUMED horizontal field of view, deg")
    ap.add_argument("--vmax", type=float, default=90.0, help="ASSUMED servo slew rate, deg/s")
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    def sweep(fov, vmax, tag):
        rows = []
        for scn in SCENARIOS:
            for ctrl in CONTROLLERS:
                for p in P_DETECT:
                    rng = random.Random(f"{a.seed}-{scn}-{ctrl}-{p}-{fov}-{vmax}")
                    res = [run(ctrl, scn, p, fov, vmax, rng) for _ in range(a.runs)]
                    res = [r for r in res if r]
                    tc = [r["t_centre"] for r in res if r["t_centre"] is not None]
                    rows.append({
                        "sweep": tag, "scenario": scn, "controller": ctrl, "p_detect": p,
                        "fov_deg": fov, "vmax_deg_s": vmax, "runs": len(res),
                        "rms_err_median": round(quantile([r["rms"] for r in res], 0.5), 2),
                        "rms_err_q25": round(quantile([r["rms"] for r in res], 0.25), 2),
                        "rms_err_q75": round(quantile([r["rms"] for r in res], 0.75), 2),
                        "in_view_median": round(quantile([r["view"] for r in res], 0.5), 3),
                        "centred_fraction": round(len(tc) / len(res), 3) if scn != "walk" else "",
                        "t_centre_median_s": round(quantile(tc, 0.5), 2) if tc else "",
                    })
        return rows

    rows = sweep(a.fov, a.vmax, "main")
    for fov in (50.0, 80.0):                       # sensitivity to the unmeasured field of view
        rows += sweep(fov, a.vmax, "fov")
    for vmax in (45.0, 180.0):                     # sensitivity to the unmeasured servo speed
        rows += sweep(a.fov, vmax, "vmax")
    with open(out / "track_sim.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"wrote {out / 'track_sim.csv'} ({len(rows)} rows, {a.runs} runs each)")

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(1, 3, figsize=(13, 3.8), sharey=True)
    colors = {"prop": "#c0392b", "prop45": "#e59866", "int-40": "#7fb3d5", "int-80": "#1f618d", "int-160": "#117a65"}
    for ax, scn in zip(axes, SCENARIOS):
        for ctrl in CONTROLLERS:
            sel = [r for r in rows if r["sweep"] == "main" and r["scenario"] == scn and r["controller"] == ctrl]
            ax.errorbar([r["p_detect"] for r in sel], [r["rms_err_median"] for r in sel],
                        yerr=[[r["rms_err_median"] - r["rms_err_q25"] for r in sel],
                              [r["rms_err_q75"] - r["rms_err_median"] for r in sel]],
                        label=ctrl, color=colors[ctrl], marker="o", capsize=3)
        ax.set_title(scn)
        ax.set_xlabel("detection probability per frame")
        ax.set_xscale("log")
        ax.grid(alpha=0.3)
    axes[0].set_ylabel("RMS aiming error while the person is present [deg]")
    axes[0].legend(title="controller")
    fig.suptitle(f"Simulation only: FOV {a.fov:.0f} deg and servo {a.vmax:.0f} deg/s are assumed; median and IQR of {a.runs} runs")
    fig.tight_layout()
    fig.savefig(out / "track_sim.png", dpi=130)
    print(f"wrote {out / 'track_sim.png'}")


if __name__ == "__main__":
    main()
