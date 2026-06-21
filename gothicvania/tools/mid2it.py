#!/usr/bin/env python3
"""MIDI -> Impulse Tracker (.it) encoder for PVSnesLib / SNESMod (smconv).

Strategy: synthesize tiny single-cycle waveform samples (public-domain math:
square/saw/etc.), map each MIDI voice to an instrument, voice-allocate the notes
across <=8 mono tracker channels, and write an instrument-mode .it that smconv
can compile to an SPC soundbank.

This file is run in two modes:
  python mid2it.py selftest   -> writes res/test.it (minimal: 1 instrument, a scale)
  python mid2it.py            -> writes res/song.it from assets/Baroque.mid (later)

Start with `selftest` to verify the .it format is acceptable to smconv before
wiring in the real song.
"""
import struct, sys, os
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES  = os.path.join(ROOT, "res")
os.makedirs(RES, exist_ok=True)

# ---------------------------------------------------------------- waveforms ---
def wave_square(n=32, duty=0.5):
    x = np.arange(n) / n
    return np.where(x < duty, 80, -80).astype(np.int8)

def wave_saw(n=32):
    x = np.linspace(80, -80, n, endpoint=False)
    return x.astype(np.int8)

def wave_triangle(n=32):
    half = n // 2
    up   = np.linspace(-80, 80, half, endpoint=False)
    down = np.linspace(80, -80, n - half, endpoint=False)
    return np.concatenate([up, down]).astype(np.int8)

# c5speed for a single-cycle sample so that note C-5 plays it at the right pitch.
# SNESMod/IT: c5speed is the sample rate at which the sample plays back note C-5.
# For an n-sample single cycle to sound as C-5 (523.25 Hz), rate = n * 523.25.
def c5_for_cycle(n):
    return int(round(n * 523.25))

# --------------------------------------------------------------- IT writer ----
def s_name(s, ln):
    b = s.encode("ascii", "ignore")[:ln]
    return b + b"\0" * (ln - len(b))

def sample_header(name, length, c5speed, loop=True):
    flags = 0x01 | (0x10 if loop else 0)         # bit0 = has sample, bit4 = loop on
    cvt   = 0x01                                  # signed samples
    h  = b"IMPS"
    h += s_name("smp", 12)                        # DOS filename
    h += bytes([0x00, 64, flags, 64])             # 00, GvL, Flg, Vol
    h += s_name(name, 26)
    h += bytes([cvt, 32])                         # Cvt, DfP
    h += struct.pack("<I", length)                # length (samples)
    h += struct.pack("<I", 0)                     # loop begin
    h += struct.pack("<I", length)                # loop end
    h += struct.pack("<I", c5speed)               # C5 speed
    h += struct.pack("<I", 0)                     # sustain loop begin
    h += struct.pack("<I", 0)                     # sustain loop end
    # sample-data pointer (filled in by caller via placeholder), then vibrato
    return h                                       # 0x48 bytes so far; caller appends ptr + 4

def instrument_header(name, sample_idx):
    h  = b"IMPI"
    h += s_name("ins", 12)
    h += bytes([0x00, 0x00, 0x00, 0x00])          # 00, NNA=cut, DCT, DCA
    h += struct.pack("<H", 0)                      # fadeout
    h += bytes([0, 60])                            # PPS, PPC
    h += bytes([128, 32, 0, 0])                    # GbV, DfP, RV, RP
    h += struct.pack("<H", 0x0214)                 # TrkVers
    h += bytes([0, 0])                             # NoS, x
    h += s_name(name, 26)
    h += bytes([0, 0, 0, 0]) + struct.pack("<H", 0)  # IFC, IFR, MCh, MPr, MIDIBnk
    # note->sample keyboard: 120 entries (note, sample)
    nsk = bytearray()
    for note in range(120):
        nsk += bytes([note, sample_idx])
    h += bytes(nsk)
    # three envelopes, all disabled (flags=0), 82 bytes each
    env = bytes([0, 0, 0, 0, 0, 0]) + bytes(75) + bytes([0])
    h += env * 3
    return h

def pack_pattern(rows, channels_used, cell):
    """cell[(row,chan)] = (note, inst, vol) ; note 255=off, None field = skip."""
    out = bytearray()
    last_mask = {}
    for r in range(rows):
        for c in range(channels_used):
            ev = cell.get((r, c))
            if ev is None:
                continue
            note, inst, vol = ev
            mask = 0
            if note is not None: mask |= 0x01
            if inst is not None: mask |= 0x02
            if vol  is not None: mask |= 0x04
            chanvar = (c + 1)
            if mask != last_mask.get(c):
                chanvar |= 0x80
            out.append(chanvar)
            if chanvar & 0x80:
                out.append(mask)
                last_mask[c] = mask
            if mask & 0x01: out.append(note)
            if mask & 0x02: out.append(inst)
            if mask & 0x04: out.append(vol)
        out.append(0)            # end of row
    return bytes(out)

def write_it(path, songname, samples, instruments, patterns, orders,
             speed, tempo, num_channels):
    # samples: list of (name, int8 ndarray, c5speed, loop)
    # instruments: list of (name, sample_idx 1-based)
    # patterns: list of (rows, packed_bytes)
    insnum, smpnum, patnum, ordnum = len(instruments), len(samples), len(patterns), len(orders)
    hdr  = b"IMPM"
    hdr += s_name(songname, 26)
    hdr += struct.pack("<H", 0x1004)                       # PHilight
    hdr += struct.pack("<HHHH", ordnum, insnum, smpnum, patnum)
    hdr += struct.pack("<HH", 0x0214, 0x0200)              # Cwt, Cmwt
    hdr += struct.pack("<HH", 0x000D, 0x0000)              # flags (stereo+instr+linear), special
    hdr += bytes([128, 48, speed, tempo, 128, 0])          # GV, MV, IS, IT, Sep, PWD
    hdr += struct.pack("<HII", 0, 0, 0)                    # msglen, msgoff, reserved
    pan  = bytes([32] * 64)
    vol  = bytes([64] * 64)
    hdr += pan + vol
    hdr += bytes(orders)                                   # order list (ends with 255)

    # Layout: header + ins-ptrs + smp-ptrs + pat-ptrs, then ins, smp-hdrs, smp-data, pats.
    base = len(hdr) + 4 * insnum + 4 * smpnum + 4 * patnum
    blob = bytearray()
    ins_ptr, smp_ptr, pat_ptr = [], [], []

    for (name, sidx) in instruments:
        ins_ptr.append(base + len(blob))
        blob += instrument_header(name, sidx)

    smp_hdr_offsets = []
    for (name, data, c5, loop) in samples:
        smp_ptr.append(base + len(blob))
        smp_hdr_offsets.append(base + len(blob))
        sh = sample_header(name, len(data), c5, loop)
        blob += sh + b"\0\0\0\0\0\0\0\0"                   # placeholder ptr(4)+vibrato(4)

    # sample data, then patch each sample header's data pointer
    data_offsets = []
    for (name, data, c5, loop) in samples:
        data_offsets.append(base + len(blob))
        blob += (data.astype(np.int8)).tobytes()
    for i, off in enumerate(smp_hdr_offsets):
        rel = off - base
        blob[rel + 0x48:rel + 0x4C] = struct.pack("<I", data_offsets[i])

    for (rows, packed) in patterns:
        pat_ptr.append(base + len(blob))
        blob += struct.pack("<HH", len(packed), rows) + b"\0\0\0\0" + packed

    ptrs = b"".join(struct.pack("<I", p) for p in ins_ptr) \
         + b"".join(struct.pack("<I", p) for p in smp_ptr) \
         + b"".join(struct.pack("<I", p) for p in pat_ptr)
    with open(path, "wb") as f:
        f.write(hdr + ptrs + bytes(blob))
    print(f"wrote {path}: ins={insnum} smp={smpnum} pat={patnum} ord={ordnum} bytes={len(hdr)+len(ptrs)+len(blob)}")

# ------------------------------------------------------------------ selftest --
def selftest():
    sq = wave_square(32, 0.5)
    samples = [("square", sq, c5_for_cycle(32), True)]
    instruments = [("synth", 1)]
    rows = 32
    cell = {}
    scale = [60, 62, 64, 65, 67, 69, 71, 72]   # C-5 major scale (MIDI numbers)
    for i, midi in enumerate(scale):
        note = midi                              # IT note = MIDI note (C-5 = 60)
        cell[(i * 4, 0)] = (note, 1, 64)         # note, instrument 1, vol 64
        cell[(i * 4 + 3, 0)] = (255, None, None) # note-off
    packed = pack_pattern(rows, 1, cell)
    write_it(os.path.join(RES, "test.it"), "selftest",
             samples, instruments, [(rows, packed)], [0, 255], speed=6, tempo=125, num_channels=1)

def convert_midi(path, out_name="baroque.it", song_name="Gothic Baroque", tempo=145):
    import mido
    mid = mido.MidiFile(path)
    tpb = mid.ticks_per_beat                      # 960
    ROWS_PER_BEAT = 12
    tpr = tpb / ROWS_PER_BEAT                      # ticks per tracker row
    PAT_ROWS, NUM_CH = 64, 6

    # gather (start_tick, end_tick, pitch, vel) per track
    track_notes = {}
    for ti, tr in enumerate(mid.tracks):
        t, active, notes = 0, {}, []
        for m in tr:
            t += m.time
            if m.type == 'note_on' and m.velocity > 0:
                active[m.note] = (t, m.velocity)
            elif m.type == 'note_off' or (m.type == 'note_on' and m.velocity == 0):
                if m.note in active:
                    st, vel = active.pop(m.note)
                    notes.append((st, t, m.note, vel))
        if notes:
            track_notes[ti] = notes

    # track -> (instrument#, channel pool). Sized to each voice's max polyphony.
    layout = {1: (1, [0]), 2: (2, [1, 2, 3]), 3: (3, [4]), 4: (4, [5])}

    max_tick = max(n[1] for ns in track_notes.values() for n in ns)
    total_rows = ((int(np.ceil(max_tick / tpr)) + PAT_ROWS - 1) // PAT_ROWS) * PAT_ROWS
    npat = total_rows // PAT_ROWS

    onmap, offmap = {}, {}
    for ti, notes in track_notes.items():
        if ti not in layout:
            continue
        inst, pool = layout[ti]
        free_at = {c: -1 for c in pool}
        for st, et, pitch, vel in sorted(notes, key=lambda x: x[0]):
            r0 = int(round(st / tpr)); r1 = max(r0 + 1, int(round(et / tpr)))
            ch = min(pool, key=lambda c: free_at[c])      # least-recently-used channel
            onmap[(r0, ch)] = (pitch, inst, min(64, vel // 2))
            offmap[(r1, ch)] = True
            free_at[ch] = r1

    cell = {}
    for k, ev in onmap.items():
        cell[k] = ev
    for k in offmap:
        if k not in onmap:                                # note-on always wins
            cell[k] = (255, None, None)

    patterns = []
    for p in range(npat):
        pcell = {(r - p * PAT_ROWS, c): ev for (r, c), ev in cell.items()
                 if p * PAT_ROWS <= r < (p + 1) * PAT_ROWS}
        patterns.append((PAT_ROWS, pack_pattern(PAT_ROWS, NUM_CH, pcell)))

    orders = list(range(npat)) + [255]
    samples = [
        ("harpsi", wave_square(32, 0.22), c5_for_cycle(32), True),  # bright pluck
        ("organ",  wave_square(32, 0.50), c5_for_cycle(32), True),  # full square
        ("bass",   wave_triangle(32),     c5_for_cycle(32), True),  # round bass
        ("string", wave_saw(32),          c5_for_cycle(32), True),  # bright pad
    ]
    instruments = [("harpsi", 1), ("organ", 2), ("bass", 3), ("string", 4)]
    write_it(os.path.join(RES, out_name), song_name,
             samples, instruments, patterns, orders, speed=2, tempo=tempo, num_channels=NUM_CH)
    print(f"  {npat} patterns x {PAT_ROWS} rows ({total_rows} rows), {ROWS_PER_BEAT} rows/beat, 6 channels")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "selftest":
        selftest()
    elif len(sys.argv) > 1 and sys.argv[1] == "title":
        convert_midi(os.path.join(ROOT, "assets", "Title.mid"), "title.it", "Gothic Title")
    else:
        convert_midi(os.path.join(ROOT, "assets", "Baroque.mid"))
