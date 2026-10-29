#!/usr/bin/env python3
"""Synthesises the fixed status phrases in tools/voice_phrases.json with Open JTalk and packs
them into firmware/answer_demo/voice_clips.h (16 kHz, 16-bit, mono).

Voice: HTS Voice "Mei" (MMDAgent Project Team, Nagoya Institute of Technology, CC BY 3.0),
dictionary NAIST-jdic (BSD), engine Open JTalk (modified BSD), via pyopenjtalk-plus (MIT).
The generated clips must carry the attribution in THIRD_PARTY_NOTICES.md. The header is
git-ignored only because it is large; it is reproducible with this script.

  pip install pyopenjtalk-plus numpy
  python tools/gen_voice.py
"""
import json
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np
import pyopenjtalk

ROOT = Path(__file__).resolve().parent
OUT = ROOT.parent / "firmware" / "answer_demo" / "voice_clips.h"
RATE = 16000


def to_16k(x: np.ndarray, sr: int) -> np.ndarray:
    """Windowed-sinc low-pass, then linear interpolation to 16 kHz (no scipy needed)."""
    cutoff = 0.45 * RATE / sr                       # fraction of the input sample rate
    n = 255
    k = np.arange(n) - n // 2
    h = np.sinc(2 * cutoff * k) * 2 * cutoff * np.hamming(n)
    y = np.convolve(x, h / h.sum(), mode="same")
    t = np.arange(0, len(y) / sr, 1 / RATE)
    return np.interp(t * sr, np.arange(len(y)), y)


def main() -> None:
    phrases = json.loads((ROOT / "voice_phrases.json").read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory() as tmp:
        for name, text in phrases.items():
            x, sr = pyopenjtalk.tts(text)           # float samples in int16 range
            y = to_16k(np.asarray(x, dtype=np.float64), sr)
            pcm = np.clip(y, -32768, 32767).astype(np.int16)
            with wave.open(str(Path(tmp) / f"{name}.wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(RATE)
                w.writeframes(pcm.tobytes())
        subprocess.run([sys.executable, str(ROOT / "voice_pack.py"), tmp, ",".join(phrases), str(OUT),
                        "Open JTalk / HTS Voice Mei (CC BY 3.0)"], check=True)


if __name__ == "__main__":
    main()
