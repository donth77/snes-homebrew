#!/usr/bin/env python3
"""OGG sound effects -> ONE SNESMod effects module (res/effectssfx.it), combined into the soundbank
by smconv (Makefile AUDIOFILES, with the -f flag that marks the first module as the effects bank).

The CC0 pack ships 5 SFX: jump, attack, hurt (player), kill, rise (enemies -- ready for Phase 3). We
decode each via ffmpeg, trim trailing silence, normalise to int8, and store them as NON-looped 16 kHz
samples in a minimal .it (a tiny pattern plays each once so smconv keeps every sample). At runtime,
spcLoadEffect(SFX_x) loads one and spcEffect(4, SFX_x, vol) plays it (pitch 4 = 16 kHz = original speed).
SFX_* indices in game.h MUST match the SFX list order below.
"""
import os, sys, subprocess, wave
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mid2it import write_it, pack_pattern

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SND  = os.path.join(ROOT, "assets", "gothicvania-cemetery-files", "Assets", "Phaser Demo", "assets", "sounds")
RES  = os.path.join(ROOT, "res")
FF   = "/Users/tomdonohue/Library/Python/3.9/lib/python/site-packages/imageio_ffmpeg/binaries/ffmpeg-macos-aarch64-v7.1"
RATE = 16000                                         # 16 kHz -> spcEffect pitch 4 plays at original speed
SFX  = ["jump", "attack", "hurt", "kill", "rise"]    # ORDER defines the SFX_* indices (see game.h)


def load_sfx(name):
    wav = f"/tmp/sfx_{name}.wav"
    subprocess.run([FF, "-y", "-i", f"{SND}/{name}.ogg", "-ac", "1", "-ar", str(RATE),
                    "-sample_fmt", "s16", wav], capture_output=True)
    w = wave.open(wav)
    d = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(float)
    thr = max(1.0, np.abs(d).max() * 0.02)           # trim trailing near-silence (saves ARAM/ROM)
    nz = np.where(np.abs(d) > thr)[0]
    if len(nz):
        d = d[:nz[-1] + 1]
    pk = np.abs(d).max() or 1.0
    d = np.clip(np.round(d / pk * 118.0), -127, 127).astype(np.int8)   # normalise to int8
    if len(d) % 16:                                  # pad to a whole BRR block (16 samples)
        d = np.concatenate([d, np.zeros(16 - len(d) % 16, np.int8)])
    return d


samples, instruments, cell = [], [], {}
for i, name in enumerate(SFX):
    data = load_sfx(name)
    samples.append((name, data, RATE, False))        # (name, int8 data, c5speed, loop=False)
    instruments.append((name, i + 1))                # instrument i -> sample i+1 (1-based)
    cell[(i * 2, 0)]     = (60, i + 1, 64)           # play each once (C-5) so smconv keeps the sample
    cell[(i * 2 + 1, 0)] = (255, None, None)         # note-off
packed = pack_pattern(len(SFX) * 2, 1, cell)
write_it(os.path.join(RES, "effectssfx.it"), "Effects", samples, instruments,
         [(len(SFX) * 2, packed)], [0, 255], speed=6, tempo=125, num_channels=1)
print("effects: " + ", ".join(f"{i}:{n}({len(samples[i][1])}smp)" for i, n in enumerate(SFX)))
