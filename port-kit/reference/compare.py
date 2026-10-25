#!/usr/bin/env python3
"""Compare a port's render with a reference render: is it the same sound?

    python compare.py renders/default_C3.wav my_port_default_C3.wav [--gain-match]

Either file may be float32 or 16/24/32-bit integer PCM WAV, mono or stereo (left channel is used); both must be 44.1 kHz.
Integer files are read as full scale = 1.0, so a Q31 render written as 32-bit PCM compares directly; use --gain-match
when the port's output level is scaled differently on purpose (the overall offset is then reported and removed).

Reports, over the part of the reference above -60 dB of its peak:
  level    the 10 ms RMS envelope, port minus reference, dB (median and worst 99th percentile)
  spectrum the long-term spectrum in 1/3-octave bands 40 Hz .. 16 kHz, port minus reference, dB (worst band)
  onset    the first 50 ms spectrum, worst 1/3-octave band, dB (DROP, STRIKE and the attack live here)
and a verdict against the suggested tolerances (level 1 dB median / 3 dB p99, spectrum 1.5 dB, onset 3 dB).
Needs numpy.
"""
import sys, struct
import numpy as np

def read_wav(path):
    data = open(path, 'rb').read()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        sys.exit(f'{path}: not a WAV file')
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack('<I', data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHIIHH', body[:16])
            if fmt[0] == 0xFFFE and len(body) >= 26:          # WAVE_FORMAT_EXTENSIBLE: the real tag is in the subformat
                fmt = (struct.unpack('<H', body[24:26])[0],) + fmt[1:]
        elif cid == b'data':
            raw = body
        pos += 8 + size + (size & 1)
    if fmt is None or raw is None:
        sys.exit(f'{path}: no fmt or data chunk')
    tag, ch, rate, _, _, bits = fmt
    if tag == 3 and bits == 32:
        x = np.frombuffer(raw, '<f4').astype(np.float64)
    elif tag == 1 and bits == 16:
        x = np.frombuffer(raw, '<i2') / 32768.0
    elif tag == 1 and bits == 24:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        x = np.where(v >= 1 << 23, v - (1 << 24), v) / float(1 << 23)
    elif tag == 1 and bits == 32:
        x = np.frombuffer(raw, '<i4') / 2147483648.0
    else:
        sys.exit(f'{path}: unsupported format (tag {tag}, {bits} bits)')
    return x.reshape(-1, ch)[:, 0], rate

def db(x):
    return 20 * np.log10(np.maximum(x, 1e-12))

def bands(x, rate):
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / rate)
    centres = 1000 * 2 ** (np.arange(-14, 13) / 3)               # 1/3 octaves, ~62 Hz .. 16 kHz
    centres = centres[(centres >= 40) & (centres <= 16000)]
    out = []
    for c in centres:
        m = (f >= c * 2 ** (-1 / 6)) & (f < c * 2 ** (1 / 6))
        out.append(np.sqrt(spec[m].sum() + 1e-30))
    return centres, np.array(out)

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) != 2:
        sys.exit(__doc__)
    ref, ra = read_wav(args[0])
    port, rb = read_wav(args[1])
    if ra != 44100 or rb != 44100:
        sys.exit(f'both files must be 44.1 kHz (got {ra} and {rb})')
    n = min(len(ref), len(port))
    ref, port = ref[:n], port[:n]
    offset = 0.0
    if '--gain-match' in sys.argv:
        offset = db(np.sqrt(np.mean(port ** 2))) - db(np.sqrt(np.mean(ref ** 2)))
        port = port / 10 ** (offset / 20)
    w = 441                                                     # 10 ms
    m = n // w
    er = np.sqrt(np.mean(ref[:m * w].reshape(m, w) ** 2, axis=1))
    ep = np.sqrt(np.mean(port[:m * w].reshape(m, w) ** 2, axis=1))
    live = er > er.max() * 10 ** (-60 / 20)
    d = db(ep[live]) - db(er[live])
    lvl_med, lvl_p99 = float(np.median(np.abs(d))), float(np.percentile(np.abs(d), 99))
    c, br = bands(ref, 44100)
    _, bp = bands(port, 44100)
    keep = br > br.max() * 10 ** (-50 / 20)
    sd = db(bp[keep]) - db(br[keep])
    spec_worst = float(np.max(np.abs(sd))) if keep.any() else 0.0
    worst_band = float(c[keep][np.argmax(np.abs(sd))]) if keep.any() else 0.0
    o = 2205                                                    # 50 ms
    co, bro = bands(ref[:o], 44100)
    _, bpo = bands(port[:o], 44100)
    keep_o = bro > bro.max() * 10 ** (-50 / 20)
    onset_worst = float(np.max(np.abs(db(bpo[keep_o]) - db(bro[keep_o])))) if keep_o.any() else 0.0
    print(f'peak     reference {db(np.max(np.abs(ref))):6.1f} dBFS   port {db(np.max(np.abs(port))):6.1f} dBFS'
          + (f'   (gain-matched: port was {offset:+.2f} dB)' if offset else ''))
    print(f'level    median {lvl_med:5.2f} dB   p99 {lvl_p99:5.2f} dB   (10 ms RMS envelope, where the reference is above -60 dB)')
    print(f'spectrum worst band {spec_worst:5.2f} dB at {worst_band:.0f} Hz   (1/3 octave, whole render)')
    print(f'onset    worst band {onset_worst:5.2f} dB   (first 50 ms)')
    ok = lvl_med <= 1.0 and lvl_p99 <= 3.0 and spec_worst <= 1.5 and onset_worst <= 3.0
    print('verdict  ' + ('PASS: same sound within the suggested tolerances' if ok else 'CHECK: outside the suggested tolerances; listen and compare'))
    return 0 if ok else 2

if __name__ == '__main__':
    sys.exit(main())
